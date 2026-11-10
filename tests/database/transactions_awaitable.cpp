// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "../network/doctest.h"

#include "mysql/detail/mysql_transaction_awaitable.hpp"

namespace {

using namespace std::chrono_literals;
using namespace ark::detail;
using arknet::mysql::connection_errc;

static_assert(!std::is_copy_constructible_v<mysql_transaction_reply>);
static_assert(std::is_nothrow_move_constructible_v<mysql_transaction_reply>);

// Controlled callback endpoints check only the factory/bridge contract. They
// neither simulate a database nor establish lease/deadline/commit correctness.
struct transaction_stub {
    using callback = std::function<void(mysql_transaction_reply)>;
    std::vector<std::string> submitted;
    mysql_transaction_options options{};
    std::string sql;
    std::vector<arknet::mysql::field_value> parameters;
    callback completion;
    std::vector<std::uint64_t> cancelled;
    mysql_transaction_reply immediate;
    bool inline_reply{};
    bool throw_on_start{};
    bool in_callback{};
    unsigned active{};
    unsigned peak{};

    std::uint64_t start(mysql_transaction_options limits, callback handler) {
        options = limits;
        return submit("begin", std::move(handler));
    }

    std::uint64_t start_query(std::string text, callback handler) {
        sql = std::move(text);
        return submit("query", std::move(handler));
    }

    std::uint64_t start_commit(callback handler) {
        return submit("commit", std::move(handler));
    }

    std::uint64_t start_execute(std::string text, std::vector<arknet::mysql::field_value> values, callback handler) {
        sql = std::move(text);
        parameters = std::move(values);
        return submit("execute", std::move(handler));
    }

    std::uint64_t start_rollback(callback handler) {
        return submit("rollback", std::move(handler));
    }

    std::uint64_t submit(std::string operation, callback handler) {
        CHECK_FALSE(in_callback);
        CHECK(active == 0);
        submitted.push_back(std::move(operation));
        if (throw_on_start) {
            throw std::runtime_error("Transaction initiation threw before acceptance");
        }
        if (inline_reply) {
            handler(std::move(immediate));
            return 0;
        }
        completion = std::move(handler);
        ++active;
        peak = std::max(peak, active);
        return 73;
    }

    bool cancel(std::uint64_t id) {
        cancelled.push_back(id);
        return true;
    }

    void finish(mysql_transaction_reply reply = {}) {
        REQUIRE(active == 1);
        auto handler = std::move(completion);
        in_callback = true;
        handler(std::move(reply));
        in_callback = false;
        --active;
    }
};

struct transaction_owner {
    std::shared_ptr<arknet::manual_runtime> runtime = std::make_shared<arknet::manual_runtime>();
    module_task_group tasks{runtime, 1};
    database_awaitable<mysql_transaction_reply> bridge{runtime, 1};
    std::optional<mysql_transaction_reply> result;
    std::exception_ptr error;
    unsigned completed{};
    bool resumed_on_executor{};

    ~transaction_owner() {
        tasks.close();
    }

    module_task<> collect(module_task<mysql_transaction_reply> operation) {
        result.emplace(co_await std::move(operation));
        resumed_on_executor = runtime->get_executor().running_in_this_thread();
    }

    module_task_group::task_id start(module_task<mysql_transaction_reply> operation) {
        return tasks.start(collect(std::move(operation)), [this, owner = std::this_thread::get_id()](auto failure) {
            CHECK(std::this_thread::get_id() == owner);
            error = std::move(failure);
            ++completed;
        });
    }

    void deliver() {
        bridge.dispatch();
        tasks.poll();
    }

    void drain() {
        for (unsigned step = 0; step < 16 && tasks.pending(); ++step) {
            deliver();
        }
        CHECK(tasks.pending() == 0);
        CHECK(bridge.pending() == 0);
    }
};

module_task<mysql_transaction_reply> operation(
    unsigned kind, transaction_stub& endpoint, database_awaitable<mysql_transaction_reply>& bridge) {
    switch (kind) {
    case 0:
        return mysql_transaction_begin(endpoint, bridge, {83ms, 19ms});
    case 1:
        return mysql_transaction_query(endpoint, bridge, "UPDATE entries SET n=n+1");
    case 2:
        return mysql_transaction_commit(endpoint, bridge);
    case 4:
        return mysql_transaction_execute(endpoint, bridge, "SELECT ?", {std::int64_t{7}});
    default:
        return mysql_transaction_rollback(endpoint, bridge);
    }
}

module_task<> sequential_transaction(
    transaction_stub& endpoint, database_awaitable<mysql_transaction_reply>& bridge, bool rollback, unsigned& steps) {
    auto begun = co_await mysql_transaction_begin(endpoint, bridge, {83ms, 19ms});
    REQUIRE(begun.transaction);
    ++steps;
    auto queried = co_await mysql_transaction_query(endpoint, bridge, "SELECT owned");
    CHECK(bool(queried));
    ++steps;
    auto ended =
        co_await (rollback ? mysql_transaction_rollback(endpoint, bridge) : mysql_transaction_commit(endpoint, bridge));
    CHECK(ended.transaction_status.outcome ==
          (rollback ? mysql_transaction_outcome::rolled_back : mysql_transaction_outcome::committed));
    ++steps;
}

} // namespace

TEST_CASE("Transaction factories own options and SQL before lazy start and drop without submission") {
    transaction_stub endpoint;
    transaction_owner owner;
    for (unsigned kind = 0; kind < 5; ++kind) {
        auto unstarted = operation(kind, endpoint, owner.bridge);
    }
    CHECK(endpoint.submitted.empty());
    CHECK(owner.bridge.pending() == 0);
    auto beginning = [&] {
        mysql_transaction_options limits{83ms, 19ms};
        auto pending = mysql_transaction_begin(endpoint, owner.bridge, limits);
        limits = {1ms, 2ms};
        return pending;
    }();
    CHECK(owner.start(std::move(beginning)) != 0);
    CHECK(endpoint.submitted.empty());
    owner.tasks.poll();
    CHECK(endpoint.options.timeout == 83ms);
    CHECK(endpoint.options.cleanup_timeout == 19ms);
    mysql_transaction_reply begun;
    begun.transaction.emplace();
    endpoint.finish(std::move(begun));
    CHECK(owner.completed == 0);
    owner.drain();
    REQUIRE(owner.result);
    REQUIRE(owner.result->transaction);
    CHECK_FALSE(owner.error);
    CHECK(owner.resumed_on_executor);
    owner.result.reset();
    const std::string sql("SELECT 'own\0ed'", 15);
    auto querying = [&] {
        std::string original = sql;
        auto pending = mysql_transaction_query(endpoint, owner.bridge, original);
        original.assign("changed after factory call");
        return pending;
    }();
    CHECK(endpoint.submitted.size() == 1);
    owner.start(std::move(querying));
    owner.tasks.poll();
    CHECK(endpoint.sql == sql);
    endpoint.finish();
    owner.drain();
    CHECK(owner.completed == 2);
    owner.result.reset();
    const std::string parameter("param\0eter", 10);
    auto executing = [&] {
        std::string original_sql = "SELECT ?";
        std::vector<arknet::mysql::field_value> original_parameters{parameter};
        auto pending = mysql_transaction_execute(endpoint, owner.bridge, original_sql, original_parameters);
        original_sql.assign("mutated");
        std::get<std::string>(original_parameters[0]).assign("changed");
        return pending;
    }();
    owner.start(std::move(executing));
    owner.tasks.poll();
    CHECK(endpoint.sql == "SELECT ?");
    REQUIRE(endpoint.parameters.size() == 1);
    CHECK(std::get<std::string>(endpoint.parameters[0]) == parameter);
    endpoint.finish();
    owner.drain();
    CHECK(owner.completed == 3);
}

TEST_CASE("All transaction factories deliver ID zero refusals on the owner executor") {
    for (unsigned kind = 0; kind < 5; ++kind) {
        CAPTURE(kind);
        transaction_stub endpoint;
        transaction_owner owner;
        endpoint.inline_reply = true;
        endpoint.immediate.result.status.code = connection_errc::queue_full;
        const auto id = owner.start(operation(kind, endpoint, owner.bridge));
        owner.tasks.poll();
        CHECK(owner.completed == 0);
        CHECK(owner.bridge.pending() == 1);
        CHECK(owner.tasks.cancel(id));
        CHECK(endpoint.cancelled.empty());
        owner.drain();
        REQUIRE(owner.result);
        CHECK_FALSE(owner.error);
        CHECK(owner.result->result.status.code == connection_errc::queue_full);
        CHECK_FALSE(bool(*owner.result));
        CHECK(owner.resumed_on_executor);
        CHECK(owner.completed == 1);
    }
}

TEST_CASE("Transaction initiation throws remain distinct from the reply initiation diagnostic") {
    for (unsigned kind = 0; kind < 5; ++kind) {
        CAPTURE(kind);
        transaction_stub endpoint;
        transaction_owner owner;
        endpoint.throw_on_start = true;
        owner.start(operation(kind, endpoint, owner.bridge));
        owner.tasks.poll();
        CHECK(owner.completed == 0);
        owner.drain();
        REQUIRE(owner.error);
        CHECK_THROWS_AS(std::rethrow_exception(owner.error), std::runtime_error);
        CHECK_FALSE(owner.result);
        CHECK(endpoint.cancelled.empty());
    }
    transaction_stub endpoint;
    transaction_owner owner;
    owner.start(mysql_transaction_query(endpoint, owner.bridge, "SELECT 1"));
    owner.tasks.poll();
    mysql_transaction_reply reply;
    reply.initiation_exception = std::make_exception_ptr(std::logic_error("Native initiation diagnostic"));
    reply.result.status.code = connection_errc::transport;
    reply.result.status.outcome_unknown = true;
    reply.result.status.transport_error = asio::error::connection_reset;
    reply.result.status.message = "Uncertain transaction result";
    reply.result.affected_rows = 7;
    reply.result.last_insert_id = 23;
    reply.result.rows = {{std::string("result\0value", 12)}};
    reply.rollback.emplace();
    reply.statement_close.emplace();
    reply.statement_close->code = connection_errc::transport;
    reply.statement_close->message = "Independent statement cleanup";
    reply.statement_close_exception = std::make_exception_ptr(std::runtime_error("Statement close diagnostic"));
    reply.rollback->status.code = connection_errc::timeout;
    reply.rollback->status.outcome_unknown = true;
    reply.transaction_status.outcome = mysql_transaction_outcome::unconfirmed;
    reply.transaction_status.rollback_code = connection_errc::timeout;
    reply.transaction_status.rollback_outcome_unknown = true;
    reply.transaction_status.cleanup_exception = std::make_exception_ptr(std::runtime_error("Cleanup diagnostic"));
    reply.transaction_status.cancellation_exception = std::make_exception_ptr(std::runtime_error("Cancel diagnostic"));
    reply.transaction_status.lease_return_exception = std::make_exception_ptr(std::runtime_error("Return diagnostic"));
    reply.transaction_status.discard_requested = true;
    reply.transaction.emplace();
    endpoint.finish(std::move(reply));
    owner.drain();
    CHECK_FALSE(owner.error);
    REQUIRE(owner.result);
    CHECK_FALSE(bool(*owner.result));
    REQUIRE(owner.result->initiation_exception);
    CHECK_THROWS_AS(std::rethrow_exception(owner.result->initiation_exception), std::logic_error);
    CHECK(owner.result->result.status.code == connection_errc::transport);
    CHECK(owner.result->result.status.outcome_unknown);
    CHECK(owner.result->result.status.transport_error == asio::error::connection_reset);
    CHECK(owner.result->result.status.message == "Uncertain transaction result");
    CHECK(owner.result->result.affected_rows == 7);
    CHECK(owner.result->result.last_insert_id == 23);
    REQUIRE(owner.result->result.rows.size() == 1);
    REQUIRE(owner.result->result.rows[0].size() == 1);
    CHECK(std::get<std::string>(owner.result->result.rows[0][0]) == std::string("result\0value", 12));
    REQUIRE(owner.result->rollback);
    REQUIRE(owner.result->statement_close);
    CHECK(owner.result->statement_close->code == connection_errc::transport);
    CHECK(owner.result->statement_close->message == "Independent statement cleanup");
    REQUIRE(owner.result->statement_close_exception);
    CHECK_THROWS_AS(std::rethrow_exception(owner.result->statement_close_exception), std::runtime_error);
    CHECK(owner.result->rollback->status.code == connection_errc::timeout);
    CHECK(owner.result->rollback->status.outcome_unknown);
    CHECK(owner.result->transaction_status.outcome == mysql_transaction_outcome::unconfirmed);
    CHECK(owner.result->transaction_status.rollback_code == connection_errc::timeout);
    CHECK(owner.result->transaction_status.rollback_outcome_unknown);
    REQUIRE(owner.result->transaction_status.cleanup_exception);
    CHECK_THROWS_AS(std::rethrow_exception(owner.result->transaction_status.cleanup_exception), std::runtime_error);
    REQUIRE(owner.result->transaction_status.cancellation_exception);
    CHECK_THROWS_AS(
        std::rethrow_exception(owner.result->transaction_status.cancellation_exception), std::runtime_error);
    REQUIRE(owner.result->transaction_status.lease_return_exception);
    CHECK_THROWS_AS(
        std::rethrow_exception(owner.result->transaction_status.lease_return_exception), std::runtime_error);
    CHECK(owner.result->transaction_status.discard_requested);
    REQUIRE(owner.result->transaction);
}

TEST_CASE("All transaction cancellations retain the record until the native uncertain terminal") {
    for (unsigned kind = 0; kind < 5; ++kind) {
        CAPTURE(kind);
        transaction_stub endpoint;
        transaction_owner owner;
        const auto id = owner.start(operation(kind, endpoint, owner.bridge));
        owner.tasks.poll();
        CHECK(owner.tasks.cancel(id));
        CHECK(endpoint.cancelled == std::vector<std::uint64_t>{73});
        CHECK(owner.completed == 0);
        CHECK(owner.bridge.pending() == 1);
        CHECK_FALSE(owner.result);
        owner.deliver();
        CHECK(owner.completed == 0);
        CHECK(owner.bridge.pending() == 1);
        mysql_transaction_reply terminal;
        terminal.result.status.code = connection_errc::cancelled;
        terminal.result.status.outcome_unknown = true;
        terminal.transaction_status.outcome = mysql_transaction_outcome::unconfirmed;
        terminal.transaction_status.discard_requested = true;
        endpoint.finish(std::move(terminal));
        owner.drain();
        CHECK_FALSE(owner.error);
        REQUIRE(owner.result);
        CHECK(owner.result->result.status.code == connection_errc::cancelled);
        CHECK(owner.result->result.status.outcome_unknown);
        CHECK(owner.result->transaction_status.outcome == mysql_transaction_outcome::unconfirmed);
        CHECK(owner.result->transaction_status.discard_requested);
        CHECK(endpoint.submitted.size() == 1);
        CHECK(owner.completed == 1);
    }
}

TEST_CASE("Transaction sequences retire callback and bridge capacity before the next step at quota one") {
    for (const auto rollback : {false, true}) {
        CAPTURE(rollback);
        transaction_stub endpoint;
        transaction_owner owner;
        unsigned steps{};
        owner.tasks.start(sequential_transaction(endpoint, owner.bridge, rollback, steps), [&](auto error) {
            CHECK_FALSE(error);
            ++owner.completed;
        });
        owner.tasks.poll();
        CHECK(endpoint.submitted == std::vector<std::string>{"begin"});
        mysql_transaction_reply begun;
        begun.transaction.emplace();
        endpoint.finish(std::move(begun));
        CHECK(steps == 0);
        owner.deliver();
        CHECK(steps == 1);
        CHECK(owner.bridge.pending() == 1);
        CHECK(endpoint.submitted == std::vector<std::string>{"begin", "query"});
        endpoint.finish();
        owner.deliver();
        CHECK(steps == 2);
        CHECK(endpoint.submitted == std::vector<std::string>{"begin", "query", rollback ? "rollback" : "commit"});
        mysql_transaction_reply terminal;
        terminal.transaction_status.outcome =
            rollback ? mysql_transaction_outcome::rolled_back : mysql_transaction_outcome::committed;
        endpoint.finish(std::move(terminal));
        owner.drain();
        CHECK(steps == 3);
        CHECK(owner.completed == 1);
        CHECK(endpoint.peak == 1);
        CHECK(endpoint.active == 0);
    }
}

TEST_CASE("Native transaction service and empty handles instantiate all factories without a database") {
    auto pool = std::make_shared<arknet::mysql::pool>(arknet::mysql::pool_options{});
    {
        mysql_transaction_service service(pool, {1, 1024});
        mysql_transaction transaction;
        transaction_owner owner;
        owner.start(mysql_transaction_begin(service, owner.bridge, {0ms, 1s}));
        owner.tasks.poll();
        CHECK(owner.completed == 0);
        owner.drain();
        REQUIRE(owner.result);
        CHECK(owner.result->result.status.code == connection_errc::invalid_options);
        CHECK_FALSE(owner.error);
        CHECK(service.active() == 0);
        CHECK(service.retained_input_bytes() == 0);
        owner.result.reset();
        owner.start(mysql_transaction_query(transaction, owner.bridge, "SELECT 1"));
        owner.tasks.poll();
        owner.drain();
        REQUIRE(owner.result);
        CHECK(owner.result->result.status.code == connection_errc::not_connected);
        owner.result.reset();
        owner.start(mysql_transaction_execute(transaction, owner.bridge, "SELECT ?", {std::int64_t{1}}));
        owner.tasks.poll();
        owner.drain();
        REQUIRE(owner.result);
        CHECK(owner.result->result.status.code == connection_errc::not_connected);
        owner.result.reset();
        owner.start(mysql_transaction_commit(transaction, owner.bridge));
        owner.tasks.poll();
        owner.drain();
        REQUIRE(owner.result);
        CHECK(owner.result->result.status.code == connection_errc::not_connected);
        owner.result.reset();
        owner.start(mysql_transaction_rollback(transaction, owner.bridge));
        owner.tasks.poll();
        owner.drain();
        REQUIRE(owner.result);
        CHECK(owner.result->result.status.code == connection_errc::not_connected);
        CHECK_FALSE(owner.error);
        CHECK(owner.completed == 5);
    }
    CHECK(pool->close_and_wait());
}

TEST_CASE("Transaction factories preserve the bridge executor guard before native submission") {
    transaction_stub endpoint;
    transaction_owner owner;
    auto other_runtime = std::make_shared<arknet::manual_runtime>();
    module_task_group other_tasks(other_runtime, 1);
    std::exception_ptr error;
    unsigned completed{};
    other_tasks.start(owner.collect(mysql_transaction_begin(endpoint, owner.bridge, {83ms, 19ms})), [&](auto failure) {
        error = std::move(failure);
        ++completed;
    });
    other_tasks.poll();
    CHECK(other_tasks.pending() == 0);
    CHECK(completed == 1);
    REQUIRE(error);
    CHECK_THROWS_AS(std::rethrow_exception(error), std::logic_error);
    CHECK_FALSE(owner.result);
    CHECK(owner.bridge.pending() == 0);
    CHECK(endpoint.submitted.empty());
    other_tasks.close();
    CHECK(other_tasks.closed());
}
