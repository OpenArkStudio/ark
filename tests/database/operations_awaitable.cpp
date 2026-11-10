// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "../network/doctest.h"

#include "redis/detail/redis_awaitable.hpp"

#if defined(ARKNET_ENABLE_SSL)
#include "mysql/detail/mysql_awaitable.hpp"
#endif

namespace {

using namespace std::chrono_literals;
using ark::detail::database_awaitable;
using ark::detail::module_task;
using ark::detail::module_task_group;

// Controlled service callbacks exercise the operation/bridge contract without
// a network endpoint. They do not simulate a database or establish live results.
struct redis_service_stub {
    using callback = std::function<void(ark::detail::redis_service_result)>;
    std::vector<arknet::redis::command> submitted;
    std::chrono::milliseconds timeout{};
    callback completion;
    std::vector<std::uint64_t> cancelled;
    bool throw_on_start{};
    bool inline_reply{};
    ark::detail::redis_service_result immediate;
    unsigned starts{};

    std::uint64_t start(arknet::redis::command command, callback handler, std::chrono::milliseconds limit) {
        std::vector<arknet::redis::command> commands;
        commands.push_back(std::move(command));
        return start_pipeline(std::move(commands), std::move(handler), limit);
    }

    std::uint64_t start_pipeline(
        std::vector<arknet::redis::command> commands, callback handler, std::chrono::milliseconds limit) {
        ++starts;
        submitted = std::move(commands);
        timeout = limit;
        if (throw_on_start) {
            throw std::runtime_error("Redis initiation failed locally");
        }
        if (inline_reply) {
            handler(std::move(immediate));
            return 0;
        }
        completion = std::move(handler);
        return 41;
    }

    void cancel(std::uint64_t id) {
        cancelled.push_back(id);
    }

    void finish(ark::detail::redis_service_result result) {
        auto handler = std::move(completion);
        handler(std::move(result));
    }
};

template<class result_type>
struct operation_owner {
    std::shared_ptr<arknet::manual_runtime> runtime = std::make_shared<arknet::manual_runtime>();
    module_task_group tasks{runtime, 1};
    database_awaitable<result_type> bridge{runtime, 1};
    std::optional<result_type> result;
    std::exception_ptr error;
    unsigned completed{};
    bool resumed_on_executor{};

    ~operation_owner() {
        tasks.close();
    }

    module_task<> collect(module_task<result_type> operation) {
        result.emplace(co_await std::move(operation));
        resumed_on_executor = runtime->get_executor().running_in_this_thread();
    }

    module_task_group::task_id start(module_task<result_type> operation) {
        const auto owner = std::this_thread::get_id();
        return tasks.start(collect(std::move(operation)), [this, owner](std::exception_ptr failure) {
            CHECK(std::this_thread::get_id() == owner);
            error = std::move(failure);
            ++completed;
        });
    }

    void drain() {
        for (unsigned step = 0; step < 16 && tasks.pending(); ++step) {
            bridge.dispatch();
            tasks.poll();
        }
        CHECK(tasks.pending() == 0);
        CHECK(bridge.pending() == 0);
    }
};

arknet::redis::value redis_value(arknet::redis::value_type type, std::string text) {
    arknet::redis::value result;
    result.type = type;
    result.text = std::move(text);
    return result;
}

template<class service_type>
void check_redis_inline_refusal() {
    service_type service(arknet::redis::pool_options{}, 1);
    operation_owner<arknet::redis::result> owner;
    owner.start(ark::detail::redis_command(service, owner.bridge, {"PING"}, 0ms));
    owner.tasks.poll();
    CHECK(owner.completed == 0);
    CHECK(owner.bridge.pending() == 1);
    owner.drain();
    CHECK_FALSE(owner.error);
    REQUIRE(owner.result);
    CHECK(owner.result->error == arknet::redis::client_errc::invalid_command);
    CHECK(owner.resumed_on_executor);
    owner.result.reset();
    owner.start(ark::detail::redis_pipeline(service, owner.bridge, {{"PING"}, {"ECHO", "owned"}}, 0ms));
    owner.tasks.poll();
    owner.drain();
    CHECK_FALSE(owner.error);
    REQUIRE(owner.result);
    CHECK(owner.result->error == arknet::redis::client_errc::invalid_command);
    CHECK(owner.completed == 2);
    service.stop();
}

} // namespace

TEST_CASE("Redis operation owns inputs before a lazy task starts and retains ordered pipeline replies") {
    redis_service_stub service;
    operation_owner<arknet::redis::result> owner;
    { auto discarded = ark::detail::redis_command(service, owner.bridge, {"PING"}, 1ms); }
    CHECK(service.starts == 0);
    CHECK(owner.bridge.pending() == 0);
    const std::string binary("first\0value", 11);
    auto operation = [&] {
        std::vector<arknet::redis::command> commands{{"ECHO", binary}, {"GET", "missing"}, {"INCR", "counter"}};
        auto pending = ark::detail::redis_pipeline(service, owner.bridge, commands, 127ms);
        commands[0][1] = "changed after factory returned";
        return pending;
    }();
    CHECK(service.starts == 0);
    CHECK(owner.start(std::move(operation)) != 0);
    CHECK(service.starts == 0);
    owner.tasks.poll();
    CHECK(service.starts == 1);
    CHECK(service.timeout == 127ms);
    CHECK(service.submitted ==
          std::vector<arknet::redis::command>{{"ECHO", binary}, {"GET", "missing"}, {"INCR", "counter"}});
    arknet::redis::result reply;
    reply.replies = {redis_value(arknet::redis::value_type::bulk_string, binary),
        redis_value(arknet::redis::value_type::null, {}),
        redis_value(arknet::redis::value_type::error, "ERR wrong kind")};
    service.finish(std::move(reply));
    CHECK(owner.completed == 0);
    owner.drain();
    CHECK_FALSE(owner.error);
    REQUIRE(owner.result);
    REQUIRE(owner.result->replies.size() == 3);
    CHECK(owner.result->replies[0].text == binary);
    CHECK(owner.result->replies[1].type == arknet::redis::value_type::null);
    CHECK(owner.result->replies[2].type == arknet::redis::value_type::error);
    CHECK(owner.result->replies[2].text == "ERR wrong kind");
    CHECK(bool(*owner.result));
    CHECK(owner.resumed_on_executor);
    auto retained = std::move(*owner.result);
    owner.result.reset();
    auto command = arknet::redis::command{"PING"};
    owner.start(ark::detail::redis_command(service, owner.bridge, command, 29ms));
    command[0] = "changed";
    owner.tasks.poll();
    CHECK(service.submitted == std::vector<arknet::redis::command>{{"PING"}});
    CHECK(service.timeout == 29ms);
    service.finish({});
    owner.drain();
    CHECK(owner.completed == 2);
    CHECK(retained.replies[0].text == binary);
}

TEST_CASE("Redis inline rejection and local exceptions use owner delivery rather than a synthetic success") {
    for (unsigned mode = 0; mode < 3; ++mode) {
        CAPTURE(mode);
        redis_service_stub service;
        operation_owner<arknet::redis::result> owner;
        service.inline_reply = true;
        service.throw_on_start = mode == 2;
        if (mode == 0) {
            arknet::redis::result rejection;
            rejection.error = arknet::redis::client_errc::queue_full;
            service.immediate = std::move(rejection);
        } else {
            service.immediate = std::make_exception_ptr(std::runtime_error("Redis callback failed locally"));
        }
        owner.start(ark::detail::redis_command(service, owner.bridge, {"PING"}, 18ms));
        owner.tasks.poll();
        CHECK(owner.completed == 0);
        CHECK(owner.bridge.pending() == 1);
        owner.drain();
        CHECK(owner.completed == 1);
        if (mode == 0) {
            CHECK_FALSE(owner.error);
            REQUIRE(owner.result);
            CHECK(owner.result->error == arknet::redis::client_errc::queue_full);
        } else {
            CHECK_FALSE(owner.result);
            REQUIRE(owner.error);
            CHECK_THROWS_AS(std::rethrow_exception(owner.error), std::runtime_error);
        }
        CHECK(service.cancelled.empty());
    }
}

TEST_CASE("Redis cancellation waits for the native uncertain terminal without replaying the pipeline") {
    redis_service_stub service;
    operation_owner<arknet::redis::result> owner;
    const auto task = owner.start(ark::detail::redis_pipeline(service, owner.bridge, {{"INCR", "counter"}}, 85ms));
    owner.tasks.poll();
    CHECK(owner.tasks.cancel(task));
    CHECK(service.cancelled == std::vector<std::uint64_t>{41});
    CHECK(owner.completed == 0);
    CHECK(owner.bridge.pending() == 1);
    CHECK_FALSE(owner.result);
    arknet::redis::result terminal;
    terminal.error = arknet::redis::client_errc::cancelled;
    terminal.transport_error = asio::error::connection_reset;
    terminal.command_may_have_run = true;
    terminal.replies = {redis_value(arknet::redis::value_type::integer, "retained partial reply")};
    service.finish(std::move(terminal));
    owner.drain();
    CHECK_FALSE(owner.error);
    REQUIRE(owner.result);
    CHECK(owner.result->error == arknet::redis::client_errc::cancelled);
    CHECK(owner.result->transport_error == asio::error::connection_reset);
    CHECK(owner.result->command_may_have_run);
    REQUIRE(owner.result->replies.size() == 1);
    CHECK(owner.result->replies[0].text == "retained partial reply");
    CHECK(service.starts == 1);
}

TEST_CASE("Real plain and TLS Redis services instantiate the facade and reject invalid deadlines without a server") {
    check_redis_inline_refusal<ark::detail::redis_service>();
#if defined(ARKNET_ENABLE_SSL)
    check_redis_inline_refusal<ark::detail::tls_redis_service>();
#endif
}

#if defined(ARKNET_ENABLE_SSL)
namespace {

struct mysql_service_stub {
    using callback = std::function<void(ark::detail::mysql_service_result)>;
    std::string sql;
    std::vector<arknet::mysql::field_value> parameters;
    std::chrono::milliseconds timeout{};
    callback completion;
    std::vector<std::uint64_t> cancelled;
    bool throw_on_start{};
    bool inline_reply{};
    bool prepared{};
    unsigned starts{};
    ark::detail::mysql_service_result immediate;

    std::uint64_t start_query(std::string text, callback handler, std::chrono::milliseconds limit) {
        prepared = false;
        return start(std::move(text), {}, std::move(handler), limit);
    }

    std::uint64_t start_execute(std::string text, std::vector<arknet::mysql::field_value> values, callback handler,
        std::chrono::milliseconds limit) {
        prepared = true;
        return start(std::move(text), std::move(values), std::move(handler), limit);
    }

    std::uint64_t start(std::string text, std::vector<arknet::mysql::field_value> values, callback handler,
        std::chrono::milliseconds limit) {
        ++starts;
        sql = std::move(text);
        parameters = std::move(values);
        timeout = limit;
        if (throw_on_start) {
            throw std::runtime_error("MySQL initiation failed locally");
        }
        if (inline_reply) {
            handler(std::move(immediate));
            return 0;
        }
        completion = std::move(handler);
        return 52;
    }

    void cancel(std::uint64_t id) {
        cancelled.push_back(id);
    }

    void finish(ark::detail::mysql_service_result result) {
        auto handler = std::move(completion);
        handler(std::move(result));
    }
};

} // namespace

TEST_CASE("MySQL query and execute own their lazy inputs and preserve execution and cleanup independently") {
    mysql_service_stub service;
    operation_owner<ark::detail::mysql_service_reply> owner;
    const std::string binary("mysql\0parameter", 15);
    auto operation = [&] {
        std::string sql = "INSERT INTO entries VALUES (?, ?)";
        std::vector<arknet::mysql::field_value> values{std::int64_t{-9}, binary};
        auto pending = ark::detail::mysql_execute(service, owner.bridge, sql, values, 71ms);
        sql = "changed after factory returned";
        values.clear();
        return pending;
    }();
    CHECK(service.starts == 0);
    owner.start(std::move(operation));
    CHECK(service.starts == 0);
    owner.tasks.poll();
    CHECK(service.prepared);
    CHECK(service.sql == "INSERT INTO entries VALUES (?, ?)");
    CHECK(service.parameters == std::vector<arknet::mysql::field_value>{std::int64_t{-9}, binary});
    CHECK(service.timeout == 71ms);
    ark::detail::mysql_service_reply reply;
    reply.result.affected_rows = 1;
    reply.result.last_insert_id = 19;
    reply.statement_close.emplace();
    reply.statement_close->code = arknet::mysql::connection_errc::timeout;
    reply.statement_close_exception =
        std::make_exception_ptr(std::runtime_error("Statement cleanup submission failed"));
    service.finish(std::move(reply));
    owner.drain();
    CHECK_FALSE(owner.error);
    REQUIRE(owner.result);
    CHECK(bool(owner.result->result));
    CHECK(owner.result->result.affected_rows == 1);
    CHECK(owner.result->result.last_insert_id == 19);
    REQUIRE(owner.result->statement_close);
    CHECK(owner.result->statement_close->code == arknet::mysql::connection_errc::timeout);
    REQUIRE(owner.result->statement_close_exception);
    CHECK_THROWS_AS(std::rethrow_exception(owner.result->statement_close_exception), std::runtime_error);
    CHECK(owner.resumed_on_executor);
    auto retained = std::move(*owner.result);
    owner.result.reset();
    std::string sql = "SELECT 1";
    owner.start(ark::detail::mysql_query(service, owner.bridge, sql, 37ms));
    sql.clear();
    owner.tasks.poll();
    CHECK_FALSE(service.prepared);
    CHECK(service.sql == "SELECT 1");
    CHECK(service.parameters.empty());
    CHECK(service.timeout == 37ms);
    service.finish({});
    owner.drain();
    CHECK(owner.completed == 2);
    CHECK(retained.result.affected_rows == 1);
    CHECK(retained.statement_close->code == arknet::mysql::connection_errc::timeout);
}

TEST_CASE("MySQL inline rejection and local exceptions are preserved through both operation entry points") {
    for (unsigned mode = 0; mode < 3; ++mode) {
        CAPTURE(mode);
        mysql_service_stub service;
        operation_owner<ark::detail::mysql_service_reply> owner;
        service.inline_reply = true;
        service.throw_on_start = mode == 2;
        if (mode == 0) {
            ark::detail::mysql_service_reply rejected;
            rejected.result.status.code = arknet::mysql::connection_errc::queue_full;
            service.immediate = std::move(rejected);
        } else {
            service.immediate = std::make_exception_ptr(std::runtime_error("MySQL callback failed locally"));
        }
        owner.start(mode == 1 ? ark::detail::mysql_query(service, owner.bridge, "SELECT 1", 18ms)
                              : ark::detail::mysql_execute(service, owner.bridge, "SELECT ?", {}, 18ms));
        owner.tasks.poll();
        CHECK(owner.completed == 0);
        CHECK(owner.bridge.pending() == 1);
        owner.drain();
        CHECK(owner.completed == 1);
        if (mode == 0) {
            CHECK_FALSE(owner.error);
            REQUIRE(owner.result);
            CHECK(owner.result->result.status.code == arknet::mysql::connection_errc::queue_full);
        } else {
            CHECK_FALSE(owner.result);
            REQUIRE(owner.error);
            CHECK_THROWS_AS(std::rethrow_exception(owner.error), std::runtime_error);
        }
        CHECK(service.cancelled.empty());
    }
}

TEST_CASE("MySQL cancellation retains the native unknown outcome and never replays a write") {
    mysql_service_stub service;
    operation_owner<ark::detail::mysql_service_reply> owner;
    const auto task = owner.start(ark::detail::mysql_query(service, owner.bridge, "UPDATE entries SET n=n+1", 85ms));
    owner.tasks.poll();
    CHECK(owner.tasks.cancel(task));
    CHECK(service.cancelled == std::vector<std::uint64_t>{52});
    CHECK(owner.completed == 0);
    CHECK(owner.bridge.pending() == 1);
    ark::detail::mysql_service_reply terminal;
    terminal.result.status.code = arknet::mysql::connection_errc::cancelled;
    terminal.result.status.outcome_unknown = true;
    terminal.result.status.transport_error = asio::error::connection_reset;
    terminal.result.status.message = "Native terminal after cancellation";
    service.finish(std::move(terminal));
    owner.drain();
    CHECK_FALSE(owner.error);
    REQUIRE(owner.result);
    CHECK(owner.result->result.status.code == arknet::mysql::connection_errc::cancelled);
    CHECK(owner.result->result.status.outcome_unknown);
    CHECK(owner.result->result.status.transport_error == asio::error::connection_reset);
    CHECK(owner.result->result.status.message == "Native terminal after cancellation");
    CHECK_FALSE(owner.result->statement_close);
    CHECK(service.starts == 1);
}

TEST_CASE("Real MySQL service instantiates both operations and rejects invalid deadlines without a server") {
    ark::detail::mysql_service service(arknet::mysql::pool_options{}, 1);
    operation_owner<ark::detail::mysql_service_reply> owner;
    owner.start(ark::detail::mysql_query(service, owner.bridge, "SELECT 1", 0ms));
    owner.tasks.poll();
    CHECK(owner.completed == 0);
    owner.drain();
    CHECK_FALSE(owner.error);
    REQUIRE(owner.result);
    CHECK(owner.result->result.status.code == arknet::mysql::connection_errc::invalid_options);
    owner.result.reset();
    owner.start(ark::detail::mysql_execute(service, owner.bridge, "SELECT ?", {}, 0ms));
    owner.tasks.poll();
    owner.drain();
    CHECK_FALSE(owner.error);
    REQUIRE(owner.result);
    CHECK(owner.result->result.status.code == arknet::mysql::connection_errc::invalid_options);
    CHECK(owner.completed == 2);
    service.stop();
}
#endif
