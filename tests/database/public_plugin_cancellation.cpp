// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "ark/plugin_manager.hpp"
#include "ark/redis/redis_module.hpp"
#include "ark/task.hpp"

#if defined(ARKNET_ENABLE_SSL)
#include "ark/mysql/mysql_module.hpp"
#endif

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
    require(value && (allow_empty || *value), "Missing database cancellation acceptance environment");
    return value;
}

std::uint16_t port(const char* name) {
    const auto text = environment(name);
    unsigned number{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), number);
    require(parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && number && number <= 65535,
        "Database cancellation port must be an integer between 1 and 65535");
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
        require(manager.add_module("cancel_consumer", &consumer_), "Cannot register cancellation consumer");
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
                std::cerr << "Database cancellation acceptance could not naturally drain\n" << std::flush;
                std::_Exit(1);
            }
        }
    }

    void check_executor() const {
        require(std::this_thread::get_id() == owner_ && runtime_->get_executor().running_in_this_thread(),
            "DSO cancellation continuation escaped owner thread/shared executor");
    }

    ark::task_group::task_id start(ark::task<> workflow, completion& record) {
        const auto id = tasks->start(std::move(workflow), [this, &record](std::exception_ptr error) {
            record.on_owner = std::this_thread::get_id() == owner_ && runtime_->get_executor().running_in_this_thread();
            record.error = std::move(error);
            ++record.count;
        });
        require(id != 0, "Database cancellation workflow admission failed");
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
            require(std::chrono::steady_clock::now() < deadline, "Database cancellation acceptance exceeded bound");
            require(manager.update(0), "Database cancellation owner update failed");
            // Throttling only; server-visible state establishes cancellation readiness.
            std::this_thread::sleep_for(1ms);
        }
    }

    void preflight(ark::task<> workflow) {
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
                "Cancelled DSO requests did not naturally retire within the shutdown bound");
            require(manager.update(0), "Database cancellation drain update failed");
            std::this_thread::sleep_for(1ms);
        }
        require(manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::stopped,
            "Database cancellation shutdown did not reach stopped");
        require(tasks->closed() && tasks->pending() == 0 && manager.pending_tasks() == 0,
            "DSO cancellation stop retained pending managed tasks");
        require(manager.stop(0), "Repeated database cancellation stop was not idempotent");
        manager.remove_module("cancel_consumer");
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

struct cancellation_state
{
    ark::task_group::task_id request_id{};
    completion request, observer;
    bool observed{};
    bool cancelled{};
    bool recovered{};
    bool retired_on_server{};
};

void cancel_observed(plugin_host& host, cancellation_state& state) {
    require(!state.cancelled && state.observed && state.request.count == 0, "Cancellation preceded active observation");
    require(host.tasks->cancel(state.request_id) && host.tasks->cancel(state.request_id),
        "Accepted task cancellation or repeated pending cancellation failed");
    require(state.request.count == 0, "Cancellation fabricated synchronous root completion");
    state.cancelled = true;
}

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
        if (client_field(line, "name") == tag && client_field(line, "cmd") == "blpop" &&
            client_field(line, "flags").find('b') != std::string::npos)
        {
            require(!found, "Redis observer saw concurrent replay of its tagged pipeline");
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
    arknet::redis::command remove_keys{"DEL", tag + "_counter", tag + "_list"};
    auto removed = co_await database.command("observer", std::move(remove_keys), 5s);
    host.check_executor();
    redis_success(removed);
    require(removed.replies[0].type == arknet::redis::value_type::integer && removed.replies[0].integer == 0,
        "Exclusive Redis cancellation keys already existed");
    for (const auto* name : {"primary", "observer"})
    {
        arknet::redis::command ping_command{"PING"};
        auto ping = co_await database.command(name, std::move(ping_command), 5s);
        host.check_executor();
        redis_success(ping);
        require(ping.replies[0].text == "PONG", "Redis cancellation preflight failed");
    }
}

ark::task<> redis_request(
    plugin_host& host, ark::redis_module& database, std::string tag, std::optional<arknet::redis::result>& result) {
    std::vector<arknet::redis::command> commands{{"INCR", tag + "_counter"}, {"BLPOP", tag + "_list", "30"}};
    result = co_await database.pipeline("primary", std::move(commands), 20s);
    host.check_executor();
}

ark::task<> redis_observe(plugin_host& host, ark::redis_module& database, cancellation_state& state,
    std::optional<arknet::redis::result>& result, std::string tag) {
    std::optional<std::string> original;
    std::optional<std::chrono::steady_clock::time_point> quiet_until;
    const auto deadline = std::chrono::steady_clock::now() + 15s;
    for (;;)
    {
        require(!state.request.error && std::chrono::steady_clock::now() < deadline,
            "Redis cancellation raised a coroutine exception or exceeded the observation bound");
        arknet::redis::command clients{"CLIENT", "LIST"};
        auto listing = co_await database.command("observer", std::move(clients), 3s);
        host.check_executor();
        const auto blocked = blocked_client(listing, tag);
        if (blocked)
        {
            require(!quiet_until && (!original || *original == *blocked), "Redis replayed its tagged pipeline");
            original = *blocked;
            state.observed = true;
            if (!state.cancelled)
            {
                arknet::redis::command get_counter{"GET", tag + "_counter"};
                auto counter = co_await database.command("observer", std::move(get_counter), 3s);
                host.check_executor();
                redis_success(counter);
                require(counter.replies[0].text == "1", "Redis pre-cancel write was not applied exactly once");
                cancel_observed(host, state);
                require(!result, "Redis cancellation fabricated an immediate native result");
                std::cout << "redis server-visible tagged BLPOP active; task cancellation requested\n";
            }
        }
        if (result && !state.recovered)
        {
            std::cout << "redis native cancellation code=" << static_cast<int>(result->error)
                      << " may_have_run=" << result->command_may_have_run
                      << " transport=" << result->transport_error.value() << " partial=" << result->replies.size()
                      << '\n';
            require(state.observed && state.cancelled && result->error == arknet::redis::client_errc::cancelled &&
                        result->command_may_have_run && !result->transport_error && result->replies.size() <= 1,
                "Redis cancellation lost its original sent/uncertain native result semantics");
            if (!result->replies.empty())
            {
                require(
                    result->replies[0].type == arknet::redis::value_type::integer && result->replies[0].integer == 1,
                    "Redis cancellation changed its retained partial reply");
            }
            arknet::redis::command recovery_ping{"PING"};
            auto recovered = co_await database.command("primary", std::move(recovery_ping), 5s);
            host.check_executor();
            redis_success(recovered);
            require(recovered.replies[0].text == "PONG", "Redis explicit follow-up could not recover");
            state.recovered = true;
        }
        if (!blocked && state.recovered)
        {
            state.retired_on_server = true;
            if (!quiet_until)
            {
                quiet_until = std::chrono::steady_clock::now() + 500ms;
            }
            if (std::chrono::steady_clock::now() >= *quiet_until)
            {
                break;
            }
        }
        co_await observer_interval();
        host.check_executor();
    }
    arknet::redis::command get_counter{"GET", tag + "_counter"};
    auto counter = co_await database.command("observer", std::move(get_counter), 3s);
    host.check_executor();
    redis_success(counter);
    require(counter.replies[0].text == "1", "Redis implicitly replayed the cancelled pipeline write");
    arknet::redis::command remove_keys{"DEL", tag + "_counter", tag + "_list"};
    auto removed = co_await database.command("observer", std::move(remove_keys), 3s);
    host.check_executor();
    redis_success(removed);
    std::cout << "redis cancelled result, server retirement and counter=1 verified\n";
}

void check_redis() {
    ark::redis_options options;
    options.pool.connection.host = environment("ARK_DATABASE_REDIS_HOST");
    require(options.pool.connection.host == "127.0.0.1", "Redis cancellation requires an owned IPv4 loopback service");
    options.pool.connection.port = std::to_string(port("ARK_DATABASE_REDIS_PORT"));
    options.pool.connection.password = environment("ARK_DATABASE_REDIS_PASSWORD");
    if (const auto* user = std::getenv("ARK_DATABASE_REDIS_USER"))
    {
        options.pool.connection.username = user;
    }
    options.pool.max_connections = 1;
    options.pool.client_configuration.connect_timeout = 3s;
    options.pool.client_configuration.drain_timeout = 100ms;
    const auto tag = "ark_dso_cancel_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    options.pool.connection.client_name = tag;
    cancellation_state state;
    std::optional<arknet::redis::result> result;
    {
        plugin_host host("ark_database_redis_plugin");
        auto* database = host.manager.find_module<ark::redis_module>();
        require(database && database->configure("primary", options), "Cannot configure Redis cancellation primary");
        options.pool.connection.client_name = tag + "_observer";
        require(database->configure("observer", options), "Cannot configure Redis cancellation observer");
        host.preflight(redis_preflight(host, *database, tag));
        state.request_id = host.start(redis_request(host, *database, tag, result), state.request);
        host.start(redis_observe(host, *database, state, result, tag), state.observer);
        host.pump_until([&] { return state.request.count && state.observer.count; });
        plugin_host::check_completion(state.request);
        plugin_host::check_completion(state.observer);
        require(!host.tasks->cancel(state.request_id), "Retired Redis task still accepted cancellation");
        host.stop();
        require(state.request.count == 1 && state.observer.count == 1, "Redis completion repeated during shutdown");
    }
    require(state.observed && state.cancelled && state.recovered && state.retired_on_server && result &&
                result->error == arknet::redis::client_errc::cancelled && result->command_may_have_run,
        "Redis native cancellation result did not survive module/manager destruction");
}

#if defined(ARKNET_ENABLE_SSL)
void mysql_success(const ark::mysql_reply& reply) {
    require(bool(reply), "Explicit MySQL observer/recovery operation failed");
}

ark::task<> mysql_preflight(plugin_host& host, ark::mysql_module& database, std::string table) {
    auto created =
        co_await database.query("primary", "CREATE TABLE `" + table + "` (id INT PRIMARY KEY,n BIGINT NOT NULL)", 5s);
    host.check_executor();
    mysql_success(created);
    auto seeded = co_await database.query("primary", "INSERT INTO `" + table + "` VALUES (1,0)", 5s);
    host.check_executor();
    mysql_success(seeded);
    auto observer = co_await database.query("observer", "SELECT 1", 5s);
    host.check_executor();
    mysql_success(observer);
}

ark::task<> mysql_request(
    plugin_host& host, ark::mysql_module& database, std::string tag, std::optional<ark::mysql_reply>& result) {
    result = co_await database.query(
        "primary", "UPDATE `" + tag + "` SET n=n+1 WHERE id=1 AND SLEEP(10)=0 /*" + tag + "*/", 20s);
    host.check_executor();
}

ark::task<> mysql_observe(plugin_host& host, ark::mysql_module& database, cancellation_state& state,
    std::optional<ark::mysql_reply>& result, std::string tag) {
    std::optional<std::int64_t> original;
    std::optional<std::chrono::steady_clock::time_point> quiet_until;
    const auto deadline = std::chrono::steady_clock::now() + 18s;
    const auto sql = "SELECT CAST(ID AS SIGNED), STATE FROM information_schema.processlist "
                     "WHERE ID <> CONNECTION_ID() AND INFO LIKE '%" +
                     tag + "%'";
    for (;;)
    {
        require(!state.request.error && std::chrono::steady_clock::now() < deadline,
            "MySQL cancellation raised a coroutine exception or exceeded the server-retirement bound");
        auto listing = co_await database.query("observer", sql, 3s);
        host.check_executor();
        mysql_success(listing);
        require(listing.result.rows.size() <= 1, "MySQL observer saw concurrent replay of its tagged UPDATE");
        if (!listing.result.rows.empty())
        {
            require(listing.result.rows[0].size() == 2, "MySQL observer lost its ID/state fields");
            const auto id = std::get<std::int64_t>(listing.result.rows[0][0]);
            require(!quiet_until && (!original || *original == id), "MySQL replayed its tagged UPDATE");
            original = id;
            const auto* phase = std::get_if<std::string>(&listing.result.rows[0][1]);
            if (!state.cancelled && phase && *phase == "User sleep")
            {
                state.observed = true;
                cancel_observed(host, state);
                require(!result, "MySQL cancellation fabricated an immediate native result");
                std::cout << "mysql server-visible tagged UPDATE active; task cancellation requested\n";
            }
        }
        if (result && !state.recovered)
        {
            std::cout << "mysql native cancellation code=" << static_cast<int>(result->result.status.code)
                      << " outcome_unknown=" << result->result.status.outcome_unknown
                      << " transport=" << result->result.status.transport_error.value() << '\n';
            require(state.observed && state.cancelled &&
                        result->result.status.code == arknet::mysql::connection_errc::cancelled &&
                        result->result.status.outcome_unknown &&
                        result->result.status.transport_error == asio::error::operation_aborted,
                "MySQL cancellation lost its original sent/unknown native result semantics");
            auto recovered = co_await database.query("primary", "SELECT 9", 5s);
            host.check_executor();
            mysql_success(recovered);
            require(recovered.result.rows.size() == 1 && std::get<std::int64_t>(recovered.result.rows[0][0]) == 9,
                "MySQL explicit follow-up could not recover");
            state.recovered = true;
        }
        if (listing.result.rows.empty() && state.recovered)
        {
            state.retired_on_server = true;
            if (!quiet_until)
            {
                quiet_until = std::chrono::steady_clock::now() + 500ms;
            }
            if (std::chrono::steady_clock::now() >= *quiet_until)
            {
                break;
            }
        }
        co_await observer_interval();
        host.check_executor();
    }
    // Cancellation is not rollback. Inspect effects only after the server query retired.
    auto counter = co_await database.query("observer", "SELECT n FROM `" + tag + "` WHERE id=1", 3s);
    host.check_executor();
    mysql_success(counter);
    require(state.retired_on_server && counter.result.rows.size() == 1 && counter.result.rows[0].size() == 1,
        "MySQL final effect was inspected before tagged query retirement");
    const auto value = std::get<std::int64_t>(counter.result.rows[0][0]);
    require(value == 0 || value == 1, "MySQL implicitly replayed the cancelled UPDATE");
    auto dropped = co_await database.query("observer", "DROP TABLE `" + tag + "`", 3s);
    host.check_executor();
    mysql_success(dropped);
    std::cout << "mysql cancelled unknown result, server retirement and final counter=" << value << " verified\n";
}

void check_mysql() {
    ark::mysql_options options;
    options.pool.connection.host = environment("ARK_DATABASE_MYSQL_HOST");
    require(options.pool.connection.host == "127.0.0.1", "MySQL cancellation requires an owned IPv4 loopback service");
    options.pool.connection.port = port("ARK_DATABASE_MYSQL_PORT");
    options.pool.connection.username = environment("ARK_DATABASE_MYSQL_USER");
    options.pool.connection.password = environment("ARK_DATABASE_MYSQL_PASSWORD", true);
    options.pool.connection.database = environment("ARK_DATABASE_MYSQL_DATABASE");
    options.pool.connection.ca_file = environment("ARK_DATABASE_MYSQL_CA");
    options.pool.connection.tls = arknet::mysql::tls_mode::required;
    options.pool.connection.timeout = 5s;
    options.pool.max_connections = 1;
    const auto tag = "ark_dso_cancel_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    cancellation_state state;
    std::optional<ark::mysql_reply> result;
    {
        plugin_host host("ark_database_mysql_plugin");
        auto* database = host.manager.find_module<ark::mysql_module>();
        require(database && database->configure("primary", options), "Cannot configure MySQL cancellation primary");
        require(database->configure("observer", options), "Cannot configure MySQL cancellation observer");
        host.preflight(mysql_preflight(host, *database, tag));
        state.request_id = host.start(mysql_request(host, *database, tag, result), state.request);
        host.start(mysql_observe(host, *database, state, result, tag), state.observer);
        host.pump_until([&] { return state.request.count && state.observer.count; });
        plugin_host::check_completion(state.request);
        plugin_host::check_completion(state.observer);
        require(!host.tasks->cancel(state.request_id), "Retired MySQL task still accepted cancellation");
        host.stop();
        require(state.request.count == 1 && state.observer.count == 1, "MySQL completion repeated during shutdown");
    }
    require(state.observed && state.cancelled && state.recovered && state.retired_on_server && result &&
                result->result.status.code == arknet::mysql::connection_errc::cancelled &&
                result->result.status.outcome_unknown,
        "MySQL native cancellation result did not survive module/manager destruction");
}
#endif

} // namespace

int main(int argc, char** argv) {
    try
    {
        require(argc == 2, "Usage: ark_database_public_plugin_cancellation_tests redis|mysql");
        const std::string_view protocol(argv[1]);
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
            throw acceptance_failure("Requested database cancellation protocol is not enabled");
        }
        std::cout << protocol << " real DSO request cancellation, owner completion and natural retirement passed\n";
        return 0;
    }
    catch (const acceptance_failure& error)
    { std::cerr << error.what() << '\n'; }
    catch (...)
    { std::cerr << "Database DSO cancellation acceptance raised an unexpected coroutine/native exception\n"; }
    return 1;
}
