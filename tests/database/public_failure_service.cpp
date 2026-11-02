// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>

#include "redis/redis_plugin.hpp"

#if defined(ARKNET_ENABLE_SSL)
#include "mysql/mysql_plugin.hpp"
#endif

namespace {

using namespace std::chrono_literals;
unsigned checks{};

class acceptance_failure : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

void require(bool condition, const std::string& message) {
    ++checks;
    if (!condition)
    {
        throw acceptance_failure(message);
    }
}

std::string environment(const char* name, bool allow_empty = false) {
    const auto* value = std::getenv(name);
    require(value && (allow_empty || *value), std::string("Required test environment variable: ") + name);
    return value;
}

std::uint16_t port(const char* name) {
    const auto text = environment(name);
    unsigned number{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), number);
    require(parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && number > 0 && number <= 65535,
        std::string(name) + " must be an integer between 1 and 65535");
    return static_cast<std::uint16_t>(number);
}

// This timer throttles observer polling; server-visible state, never a guessed
// delay, establishes whether the slow command reached the server.
ark::task<> observer_interval() {
    asio::steady_timer timer(co_await asio::this_coro::executor);
    timer.expires_after(20ms);
    co_await timer.async_wait(asio::use_awaitable);
}

template<class module_type>
struct module_owner
{
    ark::plugin_manager manager;
    std::unique_ptr<module_type> database_owner;
    module_type& database;
    ark::module consumer;
    ark::task_group* tasks{};
    std::shared_ptr<arknet::manual_runtime> runtime;
    const std::thread::id thread{std::this_thread::get_id()};
    std::exception_ptr failure;
    unsigned completions{};
    bool completions_on_owner{true};
    bool stopped{};

    explicit module_owner(std::unique_ptr<module_type> instance)
        : database_owner(std::move(instance))
        , database(*database_owner) {
        database.set_plugin_manager(&manager);
        database.set_name("database");
        require(manager.add_module("database", &database) && manager.add_module("consumer", &consumer),
            "Cannot install public database failure test modules");
        require(manager.add_update_module(&database), "Cannot install public database update hook");
        runtime = manager.get_task_runtime();
        tasks = &manager.create_task_group(consumer);
        require(database.init(), "Cannot initialize public database module");
    }

    ~module_owner() {
        if (!stopped)
        {
            try
            { finish(); }
            catch (...)
            {
                std::cerr << "Public database failure test could not drain\n" << std::flush;
                std::_Exit(1);
            }
        }
    }

    void check_executor() const {
        require(std::this_thread::get_id() == thread && runtime->get_executor().running_in_this_thread(),
            "Failure coroutine escaped its owner thread/shared manual runtime");
    }

    void start(ark::task<> workflow) {
        require(tasks->start(std::move(workflow),
                    [this](std::exception_ptr error) {
                        completions_on_owner = completions_on_owner && std::this_thread::get_id() == thread &&
                                               runtime->get_executor().running_in_this_thread();
                        if (error && !failure)
                        {
                            failure = std::move(error);
                        }
                        ++completions;
                    }) != 0,
            "Failure workflow was not accepted");
    }

    void check_failure() const {
        if (failure)
        {
            std::rethrow_exception(failure);
        }
    }

    void preflight(ark::task<> workflow) {
        start(std::move(workflow));
        const auto deadline = std::chrono::steady_clock::now() + 20s;
        while (tasks->pending())
        {
            require(std::chrono::steady_clock::now() < deadline, "Failure preflight did not complete");
            require(manager.update(0), "Failure preflight update failed");
            std::this_thread::sleep_for(1ms);
        }
        check_failure();
        require(completions == 1 && completions_on_owner, "Preflight completion escaped its owner or repeated");
    }

    void finish() {
        const auto deadline = std::chrono::steady_clock::now() + 30s;
        // Finish the slow command and independent observer before cleanup. This
        // preserves server-side no-replay evidence without relying on shutdown grace.
        while (tasks->pending())
        {
            require(std::chrono::steady_clock::now() < deadline, "Failure workflows did not complete");
            require(manager.update(0), "Failure workflow owner update failed");
            std::this_thread::sleep_for(1ms);
        }
        while (!(stopped = manager.stop(0)))
        {
            require(manager.get_shutdown_state() != ark::plugin_manager::shutdown_state::failed &&
                        std::chrono::steady_clock::now() < deadline,
                "Failure workflows did not retire native resources");
            require(manager.update(0), "Failure workflow update failed during drain");
            std::this_thread::sleep_for(1ms);
        }
    }

    void verify() {
        require(tasks->pending() == 2, "Slow request and independent observer were not both admitted");
        finish();
        check_failure();
        require(completions == 3 && completions_on_owner, "Failure completions repeated or escaped their owner");
        require(tasks->closed() && tasks->pending() == 0, "Manager stopped with pending failure tasks");
        require(manager.stop(0), "Manager stop was not idempotent after failures");
    }
};

using redis_owner = module_owner<ark::redis_module>;

void redis_success(const arknet::redis::result& result) {
    require(bool(result) && result.replies.size() == 1, "Explicit Redis recovery/observer request failed");
}

ark::task<> redis_preflight(redis_owner& owner, std::string key) {
    ark::redis_module& database = owner.database;
    arknet::redis::command rejected_ping{"PING"};
    auto rejected = co_await database.command("wrong-password", std::move(rejected_ping), 5s);
    owner.check_executor();
    require(rejected.error == arknet::redis::client_errc::handshake_failed && !rejected.command_may_have_run,
        "Wrong Redis password lost its known-unsent handshake failure");
    require(!rejected.replies.empty() && rejected.replies[0].type == arknet::redis::value_type::error,
        "Wrong Redis password lost the native server error reply");
    arknet::redis::command recovery_ping{"PING"};
    auto recovered = co_await database.command("primary", std::move(recovery_ping), 5s);
    owner.check_executor();
    redis_success(recovered);
    require(recovered.replies[0].text == "PONG", "Explicit Redis request did not recover with valid credentials");
    arknet::redis::command check_key{"EXISTS", std::move(key)};
    auto absent = co_await database.command("primary", std::move(check_key), 5s);
    owner.check_executor();
    redis_success(absent);
    require(absent.replies[0].type == arknet::redis::value_type::integer && absent.replies[0].integer == 0,
        "Redis slow-test key already exists; refusing a destructive pop");
    arknet::redis::command observer_ping{"PING"};
    auto observer = co_await database.command("observer", std::move(observer_ping), 5s);
    owner.check_executor();
    redis_success(observer);
    require(observer.replies[0].text == "PONG", "Redis observer could not authenticate");
}

struct redis_slow_state
{
    std::optional<arknet::redis::result> result;
    bool observed{};
    bool recovered{};
};

ark::task<> redis_slow(redis_owner& owner, redis_slow_state& state, std::string key) {
    // An absent unique key and no producer keep the server blocked for 30 seconds.
    // The independently warmed client deadline is two seconds.
    const auto started = std::chrono::steady_clock::now();
    arknet::redis::command block{"BLPOP", std::move(key), "30"};
    state.result = co_await owner.database.command("primary", std::move(block), 2s);
    owner.check_executor();
    require(std::chrono::steady_clock::now() - started < 5s,
        "Redis client deadline was not delivered well before its server block timeout");
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

std::optional<std::string> blocked_redis_client(const arknet::redis::result& listing, const std::string& tag) {
    redis_success(listing);
    const auto& reply = listing.replies[0];
    require(reply.type == arknet::redis::value_type::bulk_string ||
                reply.type == arknet::redis::value_type::verbatim_string,
        "Redis CLIENT LIST did not return a bulk/verbatim string");
    std::string_view remaining = reply.text;
    if (reply.type == arknet::redis::value_type::verbatim_string)
    {
        // Native RESP3 results retain their format tag. Parse a view without
        // altering the owning result or accepting unrelated format encodings.
        require(remaining.starts_with("txt:"), "Redis CLIENT LIST returned a non-text verbatim string");
        remaining.remove_prefix(4);
    }
    std::optional<std::string> blocked;
    while (!remaining.empty())
    {
        const auto newline = remaining.find('\n');
        const auto line = remaining.substr(0, newline);
        if (client_field(line, "name") == tag && client_field(line, "cmd") == "blpop" &&
            client_field(line, "flags").find('b') != std::string::npos)
        {
            require(!blocked, "Redis observer saw concurrent replay of the uniquely named BLPOP");
            blocked = client_field(line, "id");
            require(!blocked->empty(), "Redis observer could not identify its blocked client");
        }
        if (newline == std::string_view::npos)
        {
            break;
        }
        remaining.remove_prefix(newline + 1);
    }
    return blocked;
}

ark::task<> redis_observe(redis_owner& owner, redis_slow_state& state, std::string tag) {
    std::optional<std::string> original;
    std::optional<std::chrono::steady_clock::time_point> quiet_until;
    const auto deadline = std::chrono::steady_clock::now() + 12s;
    for (;;)
    {
        owner.check_failure();
        require(std::chrono::steady_clock::now() < deadline, "Redis observer did not see deadline and recovery");
        arknet::redis::command clients{"CLIENT", "LIST"};
        auto listing = co_await owner.database.command("observer", std::move(clients), 3s);
        owner.check_executor();
        const auto blocked = blocked_redis_client(listing, tag);
        if (blocked)
        {
            require(!quiet_until, "Redis observer saw a new blocked command after the original drained");
            require(!original || *original == *blocked, "Redis observer saw BLPOP replayed on a new connection");
            original = *blocked;
            state.observed = true;
        }
        if (state.result && !state.recovered)
        {
            require(state.observed, "Redis deadline completed before server-visible BLPOP observation");
            require(state.result->error == arknet::redis::client_errc::timeout && state.result->command_may_have_run,
                "Sent Redis deadline lost its timeout/unknown execution state");
            arknet::redis::command recovery_ping{"PING"};
            auto recovered = co_await owner.database.command("primary", std::move(recovery_ping), 5s);
            owner.check_executor();
            redis_success(recovered);
            require(recovered.replies[0].text == "PONG", "Explicit new Redis request did not recover after deadline");
            state.recovered = true;
        }
        if (!blocked && state.recovered)
        {
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
        owner.check_executor();
    }
}

void check_redis() {
    ark::redis_options options;
    options.pool.connection.host = environment("ARK_DATABASE_REDIS_HOST");
    options.pool.connection.port = std::to_string(port("ARK_DATABASE_REDIS_PORT"));
    options.pool.connection.password = environment("ARK_DATABASE_REDIS_PASSWORD");
    if (const auto* user = std::getenv("ARK_DATABASE_REDIS_USER"))
    {
        options.pool.connection.username = user;
    }
    options.pool.max_connections = 1;
    options.pool.client_configuration.connect_timeout = 3s;
    options.pool.client_configuration.drain_timeout = 100ms;
    const auto tag = "ark_failure_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    options.pool.connection.client_name = tag;
    redis_slow_state state;
    {
        redis_owner owner(ark::detail::make_redis_module());
        require(owner.database.configure("primary", options), "Cannot configure Redis primary");
        auto wrong_password = options;
        wrong_password.pool.connection.password += "-ark-rejected-" + tag;
        require(owner.database.configure("wrong-password", std::move(wrong_password)),
            "Cannot configure rejected Redis credentials");
        options.pool.connection.client_name = tag + "_observer";
        require(owner.database.configure("observer", options), "Cannot configure Redis observer");
        owner.preflight(redis_preflight(owner, tag));
        owner.start(redis_slow(owner, state, tag));
        owner.start(redis_observe(owner, state, tag));
        owner.verify();
    }
    require(state.observed && state.recovered && state.result && state.result->command_may_have_run,
        "Redis deadline result/lifecycle evidence did not survive module destruction");
}

#if defined(ARKNET_ENABLE_SSL)
using mysql_owner = module_owner<ark::mysql_module>;

void mysql_success(const ark::mysql_reply& reply) {
    require(bool(reply), "Explicit MySQL recovery/observer request failed (code " +
                             std::to_string(static_cast<int>(reply.result.status.code)) + ", server code " +
                             std::to_string(reply.result.status.server_code) + ")");
}

ark::task<> mysql_preflight(mysql_owner& owner) {
    ark::mysql_module& database = owner.database;
    auto rejected = co_await database.query("wrong-password", "SELECT 1", 5s);
    owner.check_executor();
    require(rejected.result.status.code == arknet::mysql::connection_errc::authentication &&
                rejected.result.status.server_code == 1045 && rejected.result.status.sql_state == "28000" &&
                !rejected.result.status.outcome_unknown && rejected.result.rows.empty(),
        "Wrong MySQL password lost its known-unsent authentication error");
    auto recovered = co_await database.query("primary", "SELECT 7", 5s);
    owner.check_executor();
    mysql_success(recovered);
    require(recovered.result.rows.size() == 1 && std::get<std::int64_t>(recovered.result.rows[0][0]) == 7,
        "Explicit MySQL request did not recover with valid credentials");
    auto untrusted = co_await database.query("untrusted-ca", "SELECT 1", 5s);
    owner.check_executor();
    require(untrusted.result.status.code == arknet::mysql::connection_errc::tls &&
                !untrusted.result.status.outcome_unknown && untrusted.result.rows.empty(),
        "Untrusted MySQL CA lost its known-unsent TLS error");
    auto verified = co_await database.query("primary", "SELECT 8", 5s);
    owner.check_executor();
    mysql_success(verified);
    require(verified.result.rows.size() == 1 && std::get<std::int64_t>(verified.result.rows[0][0]) == 8,
        "Explicit MySQL request did not recover with a trusted CA");
    auto observer = co_await database.query("observer", "SELECT 1", 5s);
    owner.check_executor();
    mysql_success(observer);
}

struct mysql_slow_state
{
    std::optional<ark::mysql_reply> result;
    bool observed{};
    bool recovered{};
};

ark::task<> mysql_slow(mysql_owner& owner, mysql_slow_state& state, std::string tag) {
    const auto started = std::chrono::steady_clock::now();
    state.result = co_await owner.database.query("primary", "SELECT SLEEP(10) /*" + tag + "*/", 2s);
    owner.check_executor();
    require(std::chrono::steady_clock::now() - started < 5s,
        "MySQL client deadline was not delivered well before its server sleep ended");
}

ark::task<> mysql_observe(mysql_owner& owner, mysql_slow_state& state, std::string tag) {
    std::optional<std::int64_t> original;
    std::optional<std::chrono::steady_clock::time_point> quiet_until;
    const auto deadline = std::chrono::steady_clock::now() + 18s;
    const auto sql = "SELECT CAST(ID AS SIGNED) FROM information_schema.processlist "
                     "WHERE ID <> CONNECTION_ID() AND STATE = 'User sleep' AND INFO LIKE '%" +
                     tag + "%'";
    for (;;)
    {
        owner.check_failure();
        require(std::chrono::steady_clock::now() < deadline, "MySQL observer did not see deadline and recovery");
        auto listing = co_await owner.database.query("observer", sql, 3s);
        owner.check_executor();
        mysql_success(listing);
        require(listing.result.rows.size() <= 1, "MySQL observer saw concurrent replay of its tagged SLEEP");
        if (!listing.result.rows.empty())
        {
            require(!quiet_until, "MySQL observer saw a new SLEEP after the original drained");
            const auto id = std::get<std::int64_t>(listing.result.rows[0][0]);
            require(!original || *original == id, "MySQL observer saw SLEEP replayed on a new connection");
            original = id;
            state.observed = true;
        }
        if (state.result && !state.recovered)
        {
            require(state.observed, "MySQL deadline completed before server-visible SLEEP observation");
            require(state.result->result.status.code == arknet::mysql::connection_errc::timeout &&
                        state.result->result.status.outcome_unknown,
                "Sent MySQL deadline lost its timeout/unknown execution state");
            auto recovered = co_await owner.database.query("primary", "SELECT 9", 5s);
            owner.check_executor();
            mysql_success(recovered);
            require(recovered.result.rows.size() == 1 && std::get<std::int64_t>(recovered.result.rows[0][0]) == 9,
                "Explicit new MySQL request did not recover after deadline");
            state.recovered = true;
        }
        if (listing.result.rows.empty() && state.recovered)
        {
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
        owner.check_executor();
    }
}

void check_mysql() {
    ark::mysql_options options;
    options.pool.connection.host = environment("ARK_DATABASE_MYSQL_HOST");
    options.pool.connection.port = port("ARK_DATABASE_MYSQL_PORT");
    options.pool.connection.username = environment("ARK_DATABASE_MYSQL_USER");
    options.pool.connection.password = environment("ARK_DATABASE_MYSQL_PASSWORD", true);
    options.pool.connection.database = environment("ARK_DATABASE_MYSQL_DATABASE");
    options.pool.connection.ca_file = environment("ARK_DATABASE_MYSQL_CA");
    options.pool.connection.tls = arknet::mysql::tls_mode::required;
    options.pool.connection.timeout = 5s;
    options.pool.max_connections = 1;
    const auto tag = "ark_failure_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    mysql_slow_state state;
    {
        mysql_owner owner(ark::detail::make_mysql_module());
        require(owner.database.configure("primary", options), "Cannot configure MySQL primary");
        auto wrong_password = options;
        wrong_password.pool.connection.password += "-ark-rejected-" + tag;
        require(owner.database.configure("wrong-password", std::move(wrong_password)),
            "Cannot configure rejected MySQL credentials");
        auto untrusted = options;
        untrusted.pool.connection.ca_file = ARK_DATABASE_UNTRUSTED_CA;
        require(owner.database.configure("untrusted-ca", std::move(untrusted)), "Cannot configure untrusted MySQL CA");
        require(owner.database.configure("observer", options), "Cannot configure MySQL observer");
        owner.preflight(mysql_preflight(owner));
        owner.start(mysql_slow(owner, state, tag));
        owner.start(mysql_observe(owner, state, tag));
        owner.verify();
    }
    require(state.observed && state.recovered && state.result && state.result->result.status.outcome_unknown,
        "MySQL deadline result/lifecycle evidence did not survive module destruction");
}
#endif

} // namespace

int main(int argc, char** argv) {
    try
    {
        require(argc == 2, "Usage: ark_database_public_failure_service_tests redis|mysql");
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
            throw acceptance_failure("Requested failure-test protocol is not enabled");
        }
        std::cout << protocol << " public module real-service failure checks passed: " << checks << '\n';
        return 0;
    }
    catch (const acceptance_failure& error)
    { std::cerr << error.what() << '\n'; }
    catch (...)
    {
        // Never emit authentication configuration or server-controlled messages.
        std::cerr << "Public database failure acceptance raised an unexpected exception\n";
    }
    return 1;
}
