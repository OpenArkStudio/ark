// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "detail/database_awaitable.hpp"
#include "redis/detail/redis_awaitable.hpp"

#if defined(ARKNET_ENABLE_SSL)
#include "mysql/detail/mysql_awaitable.hpp"
#endif

namespace {

using namespace std::chrono_literals;
using ark::detail::database_awaitable;
using ark::detail::module_task;
using ark::detail::module_task_group;
using ark::detail::redis_command;
#if defined(ARKNET_ENABLE_SSL)
using ark::detail::mysql_execute;
using ark::detail::mysql_query;
#endif
unsigned checks{};

void require(bool condition, const char* message) {
    ++checks;
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::string environment(const char* name, bool allow_empty = false) {
    const auto* value = std::getenv(name);
    if (!value || (!allow_empty && !*value)) {
        throw std::invalid_argument(std::string("Required test environment variable: ") + name);
    }
    return value;
}

std::uint16_t port(const char* name) {
    const auto text = environment(name);
    unsigned number{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), number);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || number == 0 || number > 65535) {
        throw std::invalid_argument(std::string(name) + " must be an integer between 1 and 65535");
    }
    return static_cast<std::uint16_t>(number);
}

template<class service_type, class result_type>
void pump(service_type& service, database_awaitable<result_type>& bridge, module_task_group& group) {
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (group.pending() && std::chrono::steady_clock::now() < deadline) {
        service.update();
        bridge.dispatch();
        group.poll();
        std::this_thread::sleep_for(1ms);
    }
    if (group.pending() || bridge.pending()) {
        // A failed drain cannot safely destroy its suspended task owner. Make
        // that failed test explicit without pretending destructors can join.
        std::cerr << "Native coroutine did not drain before the test deadline\n" << std::flush;
        std::_Exit(1);
    }
}

using redis_bridge = database_awaitable<arknet::redis::result>;

void check_redis() {
    arknet::redis::pool_options options;
    options.connection.host = environment("ARK_DATABASE_REDIS_HOST");
    options.connection.port = std::to_string(port("ARK_DATABASE_REDIS_PORT"));
    if (const auto* user = std::getenv("ARK_DATABASE_REDIS_USER")) {
        options.connection.username = user;
    }
    if (const auto* password = std::getenv("ARK_DATABASE_REDIS_PASSWORD")) {
        options.connection.password = password;
    }
    options.max_connections = 1;
    options.max_waiters = 1;
    auto runtime = std::make_shared<arknet::manual_runtime>();
    module_task_group group(runtime, 1);
    redis_bridge bridge(runtime, 1);
    ark::detail::redis_service service(std::move(options), 1);
    const auto owner = std::this_thread::get_id();
    unsigned completions{};
    std::exception_ptr task_error;
    auto done = [&](std::exception_ptr error) {
        require(std::this_thread::get_id() == owner, "Redis task completion escaped its owner");
        task_error = std::move(error);
        ++completions;
    };
    auto sequence = [&]() -> module_task<> {
        const auto binary = std::string("ark\0coroutine", 13);
        arknet::redis::command echo{"ECHO", binary};
        auto first = co_await redis_command(service, bridge, std::move(echo), 3s);
        require(runtime->get_executor().running_in_this_thread(), "Redis resumed outside its manual executor");
        require(bool(first) && first.replies.size() == 1 && first.replies[0].text == binary,
            "Redis coroutine lost the native binary reply");
        arknet::redis::command ping{"PING"};
        auto second = co_await redis_command(service, bridge, std::move(ping), 3s);
        require(bool(second) && second.replies.size() == 1 && second.replies[0].text == "PONG",
            "Redis second operation failed with a one-operation quota");
        require(first.replies[0].text == binary, "Redis owning result changed after the next operation");
        arknet::redis::command empty;
        auto refused = co_await redis_command(service, bridge, std::move(empty), 3s);
        require(refused.error == arknet::redis::client_errc::invalid_command,
            "Redis inline rejection was not returned as its native error");
    };
    require(group.start(sequence(), done) != 0, "Redis coroutine task was not accepted");
    pump(service, bridge, group);
    if (task_error) {
        std::rethrow_exception(task_error);
    }
    require(completions == 1, "Redis task did not complete exactly once");

    auto cancel_pending = [&]() -> module_task<> {
        const auto stream =
            "ark_coroutine_cancel_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        arknet::redis::command read{"XREAD", "BLOCK", "20000", "STREAMS", stream, "$"};
        auto cancelled = co_await redis_command(service, bridge, std::move(read), 3s);
        require(cancelled.error == arknet::redis::client_errc::cancelled,
            "Redis accepted coroutine did not drain its native cancellation terminal");
        require(runtime->get_executor().running_in_this_thread(), "Cancelled Redis task escaped its manual executor");
    };
    require(group.start(cancel_pending(), done) != 0, "Redis cancellation task was not accepted");
    group.poll();
    require(bridge.pending() == 1, "Redis cancellation task did not reach its native wait");
    service.stop_accepting();
    bridge.stop_accepting();
    group.close();
    require(!group.closed(), "Redis close silently detached a pending coroutine");
    pump(service, bridge, group);
    if (task_error) {
        std::rethrow_exception(task_error);
    }
    require(group.closed() && completions == 2, "Redis close did not drain every accepted task");
    service.stop();
}

#if defined(ARKNET_ENABLE_SSL)
using mysql_bridge = database_awaitable<ark::detail::mysql_service_reply>;

void check_mysql() {
    arknet::mysql::pool_options options;
    options.connection.host = environment("ARK_DATABASE_MYSQL_HOST");
    options.connection.port = port("ARK_DATABASE_MYSQL_PORT");
    options.connection.username = environment("ARK_DATABASE_MYSQL_USER");
    options.connection.password = environment("ARK_DATABASE_MYSQL_PASSWORD", true);
    options.connection.database = environment("ARK_DATABASE_MYSQL_DATABASE");
    options.connection.ca_file = environment("ARK_DATABASE_MYSQL_CA");
    options.max_connections = 1;
    options.max_waiters = 1;
    auto runtime = std::make_shared<arknet::manual_runtime>();
    module_task_group group(runtime, 1);
    mysql_bridge bridge(runtime, 1);
    ark::detail::mysql_service service(std::move(options), 1);
    const auto owner = std::this_thread::get_id();
    unsigned completions{};
    std::exception_ptr task_error;
    auto done = [&](std::exception_ptr error) {
        require(std::this_thread::get_id() == owner, "MySQL task completion escaped its owner");
        task_error = std::move(error);
        ++completions;
    };
    auto sequence = [&]() -> module_task<> {
        auto text = co_await mysql_query(service, bridge, "SELECT -7, 'native coroutine'", 3s);
        require(runtime->get_executor().running_in_this_thread(), "MySQL resumed outside its manual executor");
        require(bool(text.result.status) && text.result.rows.size() == 1 && text.result.rows[0].size() == 2,
            "MySQL text coroutine lost its native result shape");
        require(std::get<std::int64_t>(text.result.rows[0][0]) == -7 &&
                    std::get<std::string>(text.result.rows[0][1]) == "native coroutine",
            "MySQL native text field types/values changed");
        const auto binary = std::string("mysql\0coroutine", 15);
        std::vector<arknet::mysql::field_value> parameters;
        parameters.emplace_back(std::int64_t{-9});
        parameters.emplace_back(binary);
        auto prepared =
            co_await mysql_execute(service, bridge, "SELECT CAST(? AS SIGNED), ?", std::move(parameters), 3s);
        require(bool(prepared.result.status) && prepared.result.rows.size() == 1 &&
                    std::get<std::int64_t>(prepared.result.rows[0][0]) == -9 &&
                    std::get<std::string>(prepared.result.rows[0][1]) == binary,
            "MySQL prepared coroutine lost typed owning results");
        require(prepared.statement_close && bool(*prepared.statement_close) && !prepared.statement_close_exception,
            "MySQL prepared cleanup was not reported separately after execution");
        require(std::get<std::string>(text.result.rows[0][1]) == "native coroutine",
            "MySQL previous owning result changed after the next operation");
        auto refused = co_await mysql_query(service, bridge, "SELECT 1", 0ms);
        require(refused.result.status.code == arknet::mysql::connection_errc::invalid_options,
            "MySQL inline rejection was not returned as its native error");
    };
    require(group.start(sequence(), done) != 0, "MySQL coroutine task was not accepted");
    pump(service, bridge, group);
    if (task_error) {
        std::rethrow_exception(task_error);
    }
    require(completions == 1, "MySQL task did not complete exactly once");

    auto cancel_pending = [&]() -> module_task<> {
        auto cancelled = co_await mysql_query(service, bridge, "SELECT SLEEP(20)", 3s);
        require(cancelled.result.status.code == arknet::mysql::connection_errc::cancelled,
            "MySQL accepted coroutine did not drain its native cancellation terminal");
        require(runtime->get_executor().running_in_this_thread(), "Cancelled MySQL task escaped its manual executor");
    };
    require(group.start(cancel_pending(), done) != 0, "MySQL cancellation task was not accepted");
    group.poll();
    require(bridge.pending() == 1, "MySQL cancellation task did not reach its native wait");
    service.stop_accepting();
    bridge.stop_accepting();
    group.close();
    require(!group.closed(), "MySQL close silently detached a pending coroutine");
    pump(service, bridge, group);
    if (task_error) {
        std::rethrow_exception(task_error);
    }
    require(group.closed() && completions == 2, "MySQL close did not drain every accepted task");
    service.stop();
}
#endif

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            throw std::invalid_argument("Usage: ark_database_service_awaitable_tests redis|mysql");
        }
        const std::string_view protocol(argv[1]);
        if (protocol == "redis") {
            check_redis();
        }
#if defined(ARKNET_ENABLE_SSL)
        else if (protocol == "mysql") {
            check_mysql();
        }
#endif
        else {
            throw std::invalid_argument("Requested protocol is not enabled in this test executable");
        }
        std::cout << protocol << " native coroutine checks passed: " << checks << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
