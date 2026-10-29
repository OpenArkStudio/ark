// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#if defined(ARKNET_ENABLE_SSL)
#include "ark/mysql/mysql_module.hpp"
#endif
#include "ark/plugin_manager.hpp"
#include "ark/redis/redis_module.hpp"
#include "ark/task.hpp"

namespace {

using namespace std::chrono_literals;

class acceptance_failure : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

void require(bool condition, const char* message) {
    if (!condition)
    {
        throw acceptance_failure(message);
    }
}

std::string environment(const char* name, bool allow_empty = false) {
    const auto* value = std::getenv(name);
    require(value && (allow_empty || *value), "Missing database restart acceptance environment");
    return value;
}

std::uint16_t port(const char* name) {
    const auto text = environment(name);
    unsigned number{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), number);
    require(parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && number && number <= 65535,
        "Database restart port must be an integer between 1 and 65535");
    return static_cast<std::uint16_t>(number);
}

struct completion
{
    unsigned count{};
    bool on_owner{};
    std::exception_ptr error;
};

// Public interfaces only: no implementation/factory object is linked into this host.
class plugin_host
{
public:
    ark::plugin_manager manager;
    ark::task_group* tasks{};

    explicit plugin_host(const char* library) {
        ark::logger::init(nullptr);
        ark::logger::instance()->set_log_func([](spdlog::level::level_enum, const std::string&) {});
        require(manager.start(0, ARK_DATABASE_PLUGIN_DIR, "", {{library, ""}}), "Database DSO startup failed");
        require(manager.add_module("restart_consumer", &consumer_), "Cannot register restart consumer");
        runtime_ = manager.get_task_runtime();
        tasks = &manager.create_task_group(consumer_);
    }

    ~plugin_host() {
        if (!stopped_)
        {
            try
            { stop(); }
            catch (...)
            {
                std::cerr << "Database restart acceptance could not naturally drain\n" << std::flush;
                std::_Exit(1);
            }
        }
    }

    void check_executor() const {
        require(std::this_thread::get_id() == owner_ && runtime_->get_executor().running_in_this_thread(),
            "DSO restart continuation escaped owner thread/shared executor");
    }

    ark::task_group::task_id start(ark::task<> workflow, completion& record) {
        const auto id = tasks->start(std::move(workflow), [this, &record](std::exception_ptr error) {
            record.on_owner = std::this_thread::get_id() == owner_ && runtime_->get_executor().running_in_this_thread();
            record.error = std::move(error);
            ++record.count;
        });
        require(id != 0, "Database restart workflow admission failed");
        return id;
    }

    static void check_completion(const completion& record) {
        if (record.error)
        {
            std::rethrow_exception(record.error);
        }
        require(record.count == 1 && record.on_owner, "DSO root completion repeated or escaped its owner");
    }

    void pump_until(const std::function<bool()>& done) {
        const auto deadline = std::chrono::steady_clock::now() + 25s;
        while (!done())
        {
            require(std::chrono::steady_clock::now() < deadline, "Database restart acceptance exceeded bound");
            require(manager.update(0), "Database restart owner update failed");
            // Throttling only; server-visible state establishes restart readiness.
            std::this_thread::sleep_for(1ms);
        }
    }

    void preflight(ark::task<> workflow) {
        require(tasks->pending() == 0, "Preflight started with pending work");
        preflight_completion_ = {};
        auto& record = preflight_completion_;
        start(std::move(workflow), record);
        pump_until([&] { return record.count != 0; });
        check_completion(record);
        require(tasks->pending() == 0, "Preflight did not retire naturally");
    }

    void stop() {
        const auto deadline = std::chrono::steady_clock::now() + 30s;
        while (!(stopped_ = manager.stop(0)))
        {
            require(manager.get_shutdown_state() != ark::plugin_manager::shutdown_state::failed &&
                        std::chrono::steady_clock::now() < deadline,
                "Interrupted DSO requests did not naturally retire within the shutdown bound");
            require(manager.update(0), "Database restart drain update failed");
            std::this_thread::sleep_for(1ms);
        }
        require(manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::stopped,
            "Database restart shutdown did not reach stopped");
        require(tasks->closed() && tasks->pending() == 0 && manager.pending_tasks() == 0,
            "DSO restart stop retained pending managed tasks");
        require(manager.stop(0), "Repeated database restart stop was not idempotent");
        manager.remove_module("restart_consumer");
    }

private:
    completion preflight_completion_;
    ark::module consumer_;
    std::shared_ptr<arknet::manual_runtime> runtime_;
    const std::thread::id owner_{std::this_thread::get_id()};
    bool stopped_{};
};

ark::task<> observer_interval() {
    asio::steady_timer timer(co_await asio::this_coro::executor);
    timer.expires_after(20ms);
    co_await timer.async_wait(asio::use_awaitable);
}

// The shell actor owns the service and signal authority. The host publishes
// observed server state and waits for independently verified service readiness.
class fault_barrier
{
public:
    explicit fault_barrier(std::string protocol)
        : directory_(environment("ARK_DATABASE_FAULT_DIR"))
        , token_(environment("ARK_DATABASE_FAULT_TOKEN"))
        , protocol_(std::move(protocol)) {
        require(directory_.is_absolute() && std::filesystem::is_directory(directory_), "Fault directory must exist");
        require(token_.size() == 64 && token_.find_first_not_of("0123456789abcdef") == std::string::npos,
            "Fault token must be a fresh 256-bit hexadecimal value");
        require(read(directory_ / "ownership") == token_, "Private fault ownership marker did not match");
        require(!std::filesystem::exists(path("ready")) && !std::filesystem::exists(path("fired")) &&
                    !std::filesystem::exists(path("down")) && !std::filesystem::exists(path("restarted")),
            "Fault barrier must not reuse prior markers");
    }

    void observed(const std::string& id) {
        require(!announced_ && !id.empty(), "Fault barrier repeated or lost server request identity");
        const auto temporary = path("ready.tmp");
        {
            std::ofstream output(temporary);
            output << token_ << '\n' << id << '\n';
            output.close();
            require(bool(output), "Cannot publish server-active fault marker");
        }
        std::filesystem::rename(temporary, path("ready"));
        announced_ = true;
        std::cout << protocol_ << " server-observed active request id=" << id << "; fault barrier published\n"
                  << std::flush;
    }

    void outage_observed() {
        require(fired(), "Restart fault receipt missing");
        const auto temporary = path("down.tmp");
        std::ofstream output(temporary);
        output << token_ << '\n';
        output.close();
        require(bool(output), "Cannot publish outage result marker");
        std::filesystem::rename(temporary, path("down"));
    }

    bool restarted() const {
        return std::filesystem::exists(path("restarted")) && read(path("restarted")) == token_;
    }

    bool fired() const {
        return announced_ && std::filesystem::exists(path("fired")) && read(path("fired")) == token_;
    }

private:
    static std::string read(const std::filesystem::path& path) {
        std::ifstream input(path);
        std::string line;
        std::getline(input, line);
        require(bool(input), "Cannot read private fault marker");
        return line;
    }

    std::filesystem::path path(const char* suffix) const {
        return directory_ / (protocol_ + "." + suffix);
    }

    std::filesystem::path directory_;
    std::string token_, protocol_;
    bool announced_{};
};

void redis_success(const arknet::redis::result& result) {
    require(bool(result) && result.replies.size() == 1, "Explicit Redis observer/recovery operation failed");
}

std::string client_field(std::string_view line, std::string_view name) {
    const auto marker = std::string(name) + "=";
    for (std::size_t position{}; position < line.size();)
    {
        const auto end = line.find(' ', position);
        const auto field = line.substr(position, end == std::string_view::npos ? end : end - position);
        if (field.starts_with(marker))
        {
            return std::string(field.substr(marker.size()));
        }
        if (end == std::string_view::npos)
        {
            break;
        }
        position = end + 1;
    }
    return {};
}

std::optional<std::string> blocked_client(const arknet::redis::result& listing, const std::string& tag) {
    redis_success(listing);
    const auto& reply = listing.replies[0];
    require(reply.type == arknet::redis::value_type::bulk_string ||
                reply.type == arknet::redis::value_type::verbatim_string,
        "Redis CLIENT LIST result was not text");
    std::string_view remaining = reply.text;
    if (reply.type == arknet::redis::value_type::verbatim_string)
    {
        require(remaining.starts_with("txt:"), "Redis CLIENT LIST verbatim result had an unexpected format");
        remaining.remove_prefix(4);
    }
    std::optional<std::string> found;
    while (!remaining.empty())
    {
        const auto end = remaining.find('\n');
        const auto line = remaining.substr(0, end);
        if (client_field(line, "name") == tag && client_field(line, "cmd") == "xread" &&
            client_field(line, "flags").find('b') != std::string::npos)
        {
            require(!found, "Redis observer saw concurrent replay of its tagged read-only command");
            found = client_field(line, "id");
            require(!found->empty(), "Redis observer could not identify the blocked client");
        }
        if (end == std::string_view::npos)
        {
            break;
        }
        remaining.remove_prefix(end + 1);
    }
    return found;
}

ark::task<> redis_preflight(plugin_host& host, ark::redis_module& database, const std::string& tag) {
    arknet::redis::command exists{"EXISTS", tag};
    auto absent = co_await database.command("observer", std::move(exists), 5s);
    host.check_executor();
    redis_success(absent);
    require(absent.replies[0].type == arknet::redis::value_type::integer && absent.replies[0].integer == 0,
        "Exclusive Redis restart stream already exists");
    arknet::redis::command ping{"PING"};
    auto ready = co_await database.command("primary", std::move(ping), 5s);
    host.check_executor();
    redis_success(ready);
    require(ready.replies[0].text == "PONG", "Redis restart preflight failed");
}

ark::task<> redis_request(
    plugin_host& host, ark::redis_module& database, std::string tag, std::optional<arknet::redis::result>& result) {
    arknet::redis::command command{"XREAD", "BLOCK", "60000", "STREAMS", tag, "$"};
    result = co_await database.command("primary", std::move(command), 20s);
    host.check_executor();
}

ark::task<> redis_observe(plugin_host& host, ark::redis_module& database, fault_barrier& barrier,
    const completion& request, const std::optional<arknet::redis::result>& result, std::string tag) {
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    for (;;)
    {
        require(!request.count && !result && std::chrono::steady_clock::now() < deadline,
            "Redis request retired before server-active observation or observation exceeded bound");
        arknet::redis::command clients{"CLIENT", "LIST"};
        auto listing = co_await database.command("observer", std::move(clients), 3s);
        host.check_executor();
        if (const auto id = blocked_client(listing, tag))
        {
            require(!request.count && !result, "Redis request completed before fault readiness");
            barrier.observed(*id);
            co_return;
        }
        co_await observer_interval();
        host.check_executor();
    }
}

void redis_failure(const arknet::redis::result& result) {
    require(result.error == arknet::redis::client_errc::disconnected && result.command_may_have_run &&
                result.transport_error && result.transport_error != asio::error::operation_aborted &&
                result.transport_error != asio::error::timed_out && result.replies.empty(),
        "Redis server restart did not preserve disconnected/sent native transport failure");
}

ark::task<> redis_down(plugin_host& host, ark::redis_module& database, std::optional<arknet::redis::result>& result) {
    arknet::redis::command ping{"PING"};
    result = co_await database.command("primary", std::move(ping), 3s);
    host.check_executor();
    require(result->error == arknet::redis::client_errc::connection_failed && result->transport_error &&
                !result->command_may_have_run && result->replies.empty(),
        "Redis outage did not return a known-unsent connection failure");
}

ark::task<> redis_recover(
    plugin_host& host, ark::redis_module& database, std::optional<arknet::redis::result>& result, std::string payload) {
    arknet::redis::command ping{"PING"};
    auto ready = co_await database.command("primary", std::move(ping), 5s);
    host.check_executor();
    redis_success(ready);
    require(ready.replies[0].text == "PONG", "Explicit Redis PING did not reconnect to restarted endpoint");
    arknet::redis::command echo{"ECHO", payload};
    result = co_await database.command("primary", std::move(echo), 5s);
    host.check_executor();
    redis_success(*result);
    require(result->replies[0].text == payload, "Redis binary result changed after restart");
}

void check_redis() {
    fault_barrier barrier("redis");
    ark::redis_options options;
    options.pool.connection.host = environment("ARK_DATABASE_REDIS_HOST");
    require(options.pool.connection.host == "127.0.0.1", "Redis restart requires an owned IPv4 loopback service");
    options.pool.connection.port = std::to_string(port("ARK_DATABASE_REDIS_PORT"));
    options.pool.connection.password = environment("ARK_DATABASE_REDIS_PASSWORD");
    options.pool.max_connections = 1;
    options.pool.client_configuration.connect_timeout = 3s;
    const auto tag = "ark_dso_restart_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    options.pool.connection.client_name = tag;
    completion request, observer, recovery;
    std::optional<arknet::redis::result> interrupted, unavailable, recovered;
    const std::string payload("restart\0binary", 14);
    {
        plugin_host host("ark_database_redis_plugin");
        auto* database = host.manager.find_module<ark::redis_module>();
        require(database && database->configure("primary", options), "Cannot configure Redis restart primary");
        options.pool.connection.client_name = tag + "_observer";
        require(database->configure("observer", options), "Cannot configure Redis restart observer");
        host.preflight(redis_preflight(host, *database, tag));
        const auto request_id = host.start(redis_request(host, *database, tag, interrupted), request);
        host.start(redis_observe(host, *database, barrier, request, interrupted, tag), observer);
        host.pump_until([&] { return request.count && observer.count && barrier.fired(); });
        plugin_host::check_completion(observer);
        plugin_host::check_completion(request);
        require(interrupted.has_value(), "Redis interrupted read lost native result");
        redis_failure(*interrupted);
        require(!host.tasks->cancel(request_id), "Retired Redis read still accepted cancellation");
        // A new explicit request while the server is absent must fail unsent.
        host.preflight(redis_down(host, *database, unavailable));
        barrier.outage_observed();
        host.pump_until([&] { return barrier.restarted(); });
        // Same public connection name and pool; no reconfigure, retry or replay.
        host.start(redis_recover(host, *database, recovered, payload), recovery);
        require(host.tasks->pending() == 1, "Recovery was not accepted before natural drain");
        host.stop();
        plugin_host::check_completion(recovery);
        require(request.count == 1 && observer.count == 1, "Interrupted completion repeated during recovery/drain");
    }
    redis_failure(*interrupted);
    require(unavailable->error == arknet::redis::client_errc::connection_failed && !unavailable->command_may_have_run &&
                recovered && recovered->replies[0].text == payload,
        "Redis restart result ownership changed after teardown");
    std::cout << "redis restart PASS: disconnected sent read; known-unsent outage; explicit same-pool PING/ECHO; "
                 "owner completions once; natural idempotent drain; retained results\n";
}

#if defined(ARKNET_ENABLE_SSL)
ark::task<> mysql_preflight(plugin_host& host, ark::mysql_module& database) {
    for (const auto* name : {"primary", "observer"})
    {
        auto ready = co_await database.query(name, "SELECT 1", 5s);
        host.check_executor();
        require(bool(ready), "MySQL restart preflight failed");
    }
}

ark::task<> mysql_request(
    plugin_host& host, ark::mysql_module& database, std::string tag, std::optional<ark::mysql_reply>& result) {
    result = co_await database.query("primary", "SELECT SLEEP(60) /*" + tag + "*/", 20s);
    host.check_executor();
}

ark::task<> mysql_observe(plugin_host& host, ark::mysql_module& database, fault_barrier& barrier,
    const completion& request, const std::optional<ark::mysql_reply>& result, std::string tag) {
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    const auto sql = "SELECT CAST(ID AS SIGNED), STATE FROM information_schema.processlist "
                     "WHERE ID <> CONNECTION_ID() AND INFO LIKE '%" +
                     tag + "%'";
    for (;;)
    {
        require(!request.count && !result && std::chrono::steady_clock::now() < deadline,
            "MySQL request retired before server-active observation or observation exceeded bound");
        auto listing = co_await database.query("observer", sql, 3s);
        host.check_executor();
        require(bool(listing) && listing.result.rows.size() <= 1, "MySQL independent active observer failed");
        if (!listing.result.rows.empty())
        {
            require(listing.result.rows[0].size() == 2, "MySQL observer lost its ID/state fields");
            const auto* phase = std::get_if<std::string>(&listing.result.rows[0][1]);
            if (phase && *phase == "User sleep")
            {
                const auto id = std::get<std::int64_t>(listing.result.rows[0][0]);
                require(!request.count && !result, "MySQL request completed before fault readiness");
                barrier.observed(std::to_string(id));
                co_return;
            }
        }
        co_await observer_interval();
        host.check_executor();
    }
}

void mysql_failure(const ark::mysql_reply& result) {
    const auto& status = result.result.status;
    require(status.code == arknet::mysql::connection_errc::transport && status.outcome_unknown &&
                status.transport_error && status.transport_error != asio::error::operation_aborted &&
                status.transport_error != asio::error::timed_out && !status.server_code && result.result.rows.empty(),
        "MySQL server restart did not preserve transport/unknown native failure");
}

void mysql_unavailable(const ark::mysql_reply& result) {
    const auto& status = result.result.status;
    require(status.code == arknet::mysql::connection_errc::transport &&
                status.transport_error == asio::error::connection_refused && !status.outcome_unknown &&
                !status.server_code && result.result.rows.empty(),
        "MySQL outage did not return a known-unsent native connection refusal");
}

ark::task<> mysql_down(plugin_host& host, ark::mysql_module& database, std::optional<ark::mysql_reply>& result) {
    result = co_await database.query("primary", "SELECT 1", 3s);
    host.check_executor();
    mysql_unavailable(*result);
}

void mysql_recovered(const ark::mysql_reply& result, const std::string& payload) {
    require(bool(result) && result.result.rows.size() == 1 && result.result.rows[0].size() == 2 &&
                std::get<std::int64_t>(result.result.rows[0][0]) == 42 &&
                std::get<std::string>(result.result.rows[0][1]) == payload,
        "Explicit MySQL recovery query lost its owning scalar/binary result");
}

ark::task<> mysql_recover(
    plugin_host& host, ark::mysql_module& database, std::optional<ark::mysql_reply>& result, std::string payload) {
    result = co_await database.query("primary", "SELECT CAST(42 AS SIGNED), CONCAT('restart', CHAR(0), 'binary')", 5s);
    host.check_executor();
    mysql_recovered(*result, payload);
}

void check_mysql() {
    fault_barrier barrier("mysql");
    ark::mysql_options options;
    options.pool.connection.host = environment("ARK_DATABASE_MYSQL_HOST");
    require(options.pool.connection.host == "127.0.0.1", "MySQL restart requires an owned IPv4 loopback service");
    options.pool.connection.port = port("ARK_DATABASE_MYSQL_PORT");
    options.pool.connection.username = environment("ARK_DATABASE_MYSQL_USER");
    options.pool.connection.password = environment("ARK_DATABASE_MYSQL_PASSWORD");
    options.pool.connection.database = environment("ARK_DATABASE_MYSQL_DATABASE");
    options.pool.connection.ca_file = environment("ARK_DATABASE_MYSQL_CA");
    options.pool.connection.tls = arknet::mysql::tls_mode::required;
    options.pool.connection.timeout = 5s;
    options.pool.max_connections = 1;
    const auto tag = "ark_dso_restart_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    completion request, observer, recovery;
    std::optional<ark::mysql_reply> interrupted, unavailable, recovered;
    const std::string payload("restart\0binary", 14);
    {
        plugin_host host("ark_database_mysql_plugin");
        auto* database = host.manager.find_module<ark::mysql_module>();
        require(database && database->configure("primary", options), "Cannot configure MySQL restart primary");
        require(database->configure("observer", options), "Cannot configure MySQL restart observer");
        host.preflight(mysql_preflight(host, *database));
        const auto request_id = host.start(mysql_request(host, *database, tag, interrupted), request);
        host.start(mysql_observe(host, *database, barrier, request, interrupted, tag), observer);
        host.pump_until([&] { return request.count && observer.count && barrier.fired(); });
        plugin_host::check_completion(observer);
        plugin_host::check_completion(request);
        require(interrupted.has_value(), "MySQL interrupted read lost native result");
        mysql_failure(*interrupted);
        require(!host.tasks->cancel(request_id), "Retired MySQL read still accepted cancellation");
        host.preflight(mysql_down(host, *database, unavailable));
        barrier.outage_observed();
        host.pump_until([&] { return barrier.restarted(); });
        // Same public connection name and pool; explicitly new query, no replay.
        host.start(mysql_recover(host, *database, recovered, payload), recovery);
        require(host.tasks->pending() == 1, "MySQL recovery was not accepted before natural drain");
        host.stop();
        plugin_host::check_completion(recovery);
        require(request.count == 1 && observer.count == 1, "MySQL interrupted completion repeated during drain");
    }
    mysql_failure(*interrupted);
    require(unavailable && !unavailable->result.status.outcome_unknown && recovered,
        "MySQL restart failure results changed after teardown");
    mysql_unavailable(*unavailable);
    mysql_recovered(*recovered, payload);
    std::cout << "mysql restart PASS: transport/unknown sent read; known-unsent outage; explicit same-pool query; "
                 "owner completions once; natural idempotent drain; retained results\n";
}
#endif

} // namespace

int main(int argc, char** argv) {
    try
    {
        require(argc <= 2, "Usage: ark_database_public_plugin_restart_tests [redis|mysql]");
        const std::string_view protocol(argc == 1 ? "redis" : argv[1]);
        if (protocol == "redis")
        {
            check_redis();
        }
#if defined(ARKNET_ENABLE_SSL)
        else if (protocol == "mysql")
        {
            check_mysql();
        }
#endif
        else
        {
            throw acceptance_failure("Unsupported database restart protocol");
        }
        return 0;
    }
    catch (const acceptance_failure& error)
    { std::cerr << error.what() << '\n'; }
    catch (...)
    { std::cerr << "Database restart acceptance failed with an unexpected exception\n"; }
    return 1;
}
