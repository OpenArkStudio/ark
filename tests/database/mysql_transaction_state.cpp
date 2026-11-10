// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <thread>
#include <type_traits>
#include <vector>

#include "../network/doctest.h"

#include "mysql/detail/mysql_transaction.hpp"

namespace {

using namespace std::chrono_literals;
using namespace ark::detail;

static_assert(!std::is_copy_constructible_v<mysql_transaction>);
static_assert(std::is_nothrow_move_constructible_v<mysql_transaction>);
static_assert(std::is_nothrow_move_assignable_v<mysql_transaction>);
static_assert(std::is_nothrow_move_constructible_v<mysql_transaction_reply>);

} // namespace

TEST_CASE("Private MySQL transactions require a supplied pool and explicit nonzero limits") {
    CHECK_THROWS_AS(mysql_transaction_service(nullptr, {1, 1024}), std::invalid_argument);
    auto pool = std::make_shared<arknet::mysql::pool>(arknet::mysql::pool_options{});
    CHECK_THROWS_AS(mysql_transaction_service(pool, {0, 1024}), std::invalid_argument);
    CHECK_THROWS_AS(mysql_transaction_service(pool, {1, 0}), std::invalid_argument);
    CHECK(pool->close_and_wait());
}

TEST_CASE("Prepared MySQL transactions allocate without a pool and bind without allocating another owner") {
    {
        mysql_transaction_service prepared(mysql_transaction_service::prepared_t{}, {2, 1024});
        CHECK(prepared.quiescent());
        CHECK(prepared.active() == 0);
        CHECK(prepared.retained_input_bytes() == 0);
    }
    auto pool = std::make_shared<arknet::mysql::pool>(arknet::mysql::pool_options{});
    {
        mysql_transaction_service prepared(mysql_transaction_service::prepared_t{}, {2, 1024});
        static_assert(noexcept(prepared.bind_pool(pool)));
        CHECK(pool.use_count() == 1);
        prepared.bind_pool(pool);
        CHECK(pool.use_count() == 2);
        CHECK(prepared.quiescent());
        prepared.stop_accepting();
        CHECK(prepared.logically_drained());
    }
    CHECK(pool.use_count() == 1);
    CHECK(pool->close_and_wait());
}

TEST_CASE("Invalid transaction deadlines reject inline before acquiring a connection") {
    auto pool = std::make_shared<arknet::mysql::pool>(arknet::mysql::pool_options{});
    {
        mysql_transaction_service service(pool, {1, 1024});
        unsigned calls{};
        for (const auto options :
            {mysql_transaction_options{0ms, 1s}, mysql_transaction_options{1s, 0ms},
                mysql_transaction_options{-1ms, 1s}, mysql_transaction_options{std::chrono::milliseconds::max(), 1s}}) {
            CHECK(service.start(options, [&](auto result) {
                ++calls;
                CHECK(result.result.status.code == arknet::mysql::connection_errc::invalid_options);
                CHECK_FALSE(result.transaction.has_value());
                CHECK_FALSE(result.result.status.outcome_unknown);
            }) == 0);
        }
        CHECK(calls == 4);
        CHECK(service.active() == 0);
        CHECK(service.retained_input_bytes() == 0);
        CHECK(service.update() == 0);
        bool wrong_thread_rejected{};
        std::thread worker([&] {
            try {
                service.update();
            } catch (const std::logic_error&) {
                wrong_thread_rejected = true;
            }
        });
        worker.join();
        CHECK(wrong_thread_rejected);
    }
    CHECK(pool->close_and_wait());
}

TEST_CASE("An empty transaction handle reports unavailable without sending a command") {
    mysql_transaction transaction;
    unsigned calls{};
    CHECK(transaction.start_query("SELECT 1", [&](auto result) {
        ++calls;
        CHECK(result.result.status.code == arknet::mysql::connection_errc::not_connected);
        CHECK_FALSE(result.result.status.outcome_unknown);
    }) == 0);
    CHECK(calls == 1);
    CHECK(transaction.start_execute("SELECT ?", {std::int64_t{1}}, [&](auto result) {
        ++calls;
        CHECK(result.result.status.code == arknet::mysql::connection_errc::not_connected);
        CHECK_FALSE(result.result.status.outcome_unknown);
        CHECK_FALSE(result.statement_close);
    }) == 0);
    CHECK(calls == 2);
    CHECK_FALSE(transaction.cancel(1));
    transaction.abort();
    CHECK(transaction.snapshot().outcome == mysql_transaction_outcome::not_started);
}

TEST_CASE("Prepared transaction admission charges parameter vector and string capacities before native work") {
    auto pool = std::make_shared<arknet::mysql::pool>(arknet::mysql::pool_options{});
    {
        auto owner = std::make_shared<mysql_transaction_owner>(
            pool, mysql_transaction_limits{1, 1024}, arknet::mysql::query_options{});
        auto state = std::make_shared<mysql_transaction_state>();
        state->phase = mysql_transaction_phase::idle;
        state->deadline = std::chrono::steady_clock::now() + 1s;
        owner->states[0] = state;
        unsigned rejected{};
        for (const bool vector_capacity : {false, true}) {
            std::vector<arknet::mysql::field_value> parameters;
            if (vector_capacity) {
                parameters.reserve(1024);
            } else {
                std::string binary("x\0y", 3);
                binary.reserve(2048);
                parameters.emplace_back(std::move(binary));
            }
            CHECK(owner->submit(
                      state, "SELECT ?", mysql_transaction_phase::preparing,
                      [&](auto result) {
                          ++rejected;
                          CHECK(result.result.status.code == arknet::mysql::connection_errc::queue_full);
                          CHECK_FALSE(result.result.status.outcome_unknown);
                      },
                      std::move(parameters)) == 0);
            CHECK(state->phase == mysql_transaction_phase::idle);
            CHECK(state->native_id == 0);
            CHECK(owner->retained_input->load() == 0);
            CHECK(state->deliveries == 0);
        }
        CHECK(rejected == 2);
        owner->close();
    }
    CHECK(pool->close_and_wait());
}

TEST_CASE("Statement cleanup preserves business results and uses the original transaction deadline") {
    using arknet::mysql::connection_errc;
    auto pool = std::make_shared<arknet::mysql::pool>(arknet::mysql::pool_options{});
    for (const bool exception : {false, true}) {
        auto owner = std::make_shared<mysql_transaction_owner>(
            pool, mysql_transaction_limits{1, 1024}, arknet::mysql::query_options{});
        auto state = std::make_shared<mysql_transaction_state>();
        owner->states[0] = state;
        state->phase = mysql_transaction_phase::closing_statement;
        state->status.outcome = mysql_transaction_outcome::active;
        state->reply.result.affected_rows = 7;
        state->reply.result.rows = {{std::string("own\0ed", 6)}};
        state->deadline = std::chrono::steady_clock::now() - 1ms;
        state->cleanup_deadline = std::chrono::steady_clock::now() + 1s;
        std::optional<mysql_transaction_reply> delivered;
        REQUIRE(owner->reserve(state, 0, [&](auto reply) { delivered = std::move(reply); }));
        if (exception) {
            owner->advance(state, std::make_exception_ptr(std::runtime_error("Statement cleanup initiation")));
        } else {
            owner->issue(state, {}, mysql_transaction_phase::closing_statement);
        }
        owner->update(1);
        REQUIRE(delivered);
        CHECK(bool(*delivered));
        CHECK(delivered->result.affected_rows == 7);
        CHECK(std::get<std::string>(delivered->result.rows[0][0]) == std::string("own\0ed", 6));
        CHECK_FALSE(delivered->initiation_exception);
        CHECK_FALSE(delivered->rollback);
        CHECK(delivered->transaction_status.outcome == mysql_transaction_outcome::unconfirmed);
        CHECK(delivered->transaction_status.discard_requested);
        if (exception) {
            REQUIRE(delivered->statement_close_exception);
            CHECK_THROWS_AS(std::rethrow_exception(delivered->statement_close_exception), std::runtime_error);
            CHECK_FALSE(delivered->statement_close);
        } else {
            REQUIRE(delivered->statement_close);
            CHECK(delivered->statement_close->code == connection_errc::timeout);
            CHECK_FALSE(delivered->statement_close_exception);
        }
        CHECK(owner->retained_input->load() == 0);
        owner->close();
    }
    CHECK(pool->close_and_wait());
}

TEST_CASE("Cancellation between prepared native completion and owner polling prevents the next business stage") {
    auto pool = std::make_shared<arknet::mysql::pool>(arknet::mysql::pool_options{});
    {
        auto owner = std::make_shared<mysql_transaction_owner>(
            pool, mysql_transaction_limits{1, 1024}, arknet::mysql::query_options{});
        for (const auto phase : {mysql_transaction_phase::preparing, mysql_transaction_phase::executing,
                 mysql_transaction_phase::closing_statement}) {
            auto state = std::make_shared<mysql_transaction_state>();
            state->phase = phase;
            state->logical_id = 7;
            state->completed.emplace(arknet::mysql::query_result{});
            CHECK(owner->cancel(state, 7));
            CHECK(state->abort_requested);
            CHECK(state->cancellation_requested);
            CHECK_FALSE(state->status.cancellation_exception);
            CHECK_FALSE(owner->cancel(state, 7));
        }
        owner->close();
    }
    CHECK(pool->close_and_wait());
}

namespace ark::detail {

struct database_service_test_access {
    static auto owner(mysql_transaction_service& service) {
        return service.owner_;
    }
};

} // namespace ark::detail

namespace {

struct transaction_capture {
    std::weak_ptr<mysql_transaction_owner> owner;
    unsigned& destroyed;

    transaction_capture(std::weak_ptr<mysql_transaction_owner> owner, unsigned& destroyed)
        : owner(std::move(owner))
        , destroyed(destroyed) {
    }

    ~transaction_capture() {
        ++destroyed;
        auto active = owner.lock();
        REQUIRE(active);
        CHECK(active->activity->depth > 0);
        CHECK_FALSE(active->quiescent());
        CHECK_FALSE(active->logically_drained());
    }
};

} // namespace

TEST_CASE("Transaction drain seals only BEGIN and never closes the borrowed pool") {
    auto pool = std::make_shared<arknet::mysql::pool>(arknet::mysql::pool_options{});
    {
        mysql_transaction_service service(pool, {1, 1024});
        CHECK(service.quiescent());
        CHECK_FALSE(service.logically_drained());
        service.stop_accepting();
        CHECK(service.logically_drained());
        unsigned destroyed{};
        auto capture = std::make_shared<transaction_capture>(database_service_test_access::owner(service), destroyed);
        CHECK(service.start({1s, 1s}, [&, capture = std::move(capture)](auto result) {
            CHECK(result.result.status.code == arknet::mysql::connection_errc::not_connected);
            CHECK_FALSE(service.quiescent());
            CHECK_FALSE(service.logically_drained());
        }) == 0);
        CHECK(destroyed == 1);
        CHECK(service.logically_drained());
    }
    std::atomic<bool> acquired{};
    CHECK(pool->async_acquire([&](auto) { acquired = true; }, 100ms) != 0);
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!acquired && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(1ms);
    }
    CHECK(acquired);
    CHECK(pool->close_and_wait());
}

TEST_CASE("Accepted transaction commands remain admitted after new BEGIN is sealed") {
    auto pool = std::make_shared<arknet::mysql::pool>(arknet::mysql::pool_options{});
    for (const auto phase :
        {mysql_transaction_phase::query, mysql_transaction_phase::committing, mysql_transaction_phase::rolling_back}) {
        auto owner = std::make_shared<mysql_transaction_owner>(
            pool, mysql_transaction_limits{1, 1024}, arknet::mysql::query_options{});
        auto state = std::make_shared<mysql_transaction_state>();
        state->owner = owner;
        state->phase = mysql_transaction_phase::idle;
        state->status.outcome = mysql_transaction_outcome::active;
        state->deadline = std::chrono::steady_clock::now() + 1s;
        state->cleanup_timeout = 1s;
        owner->states[0] = state;
        owner->stop_accepting();
        CHECK_FALSE(owner->quiescent());
        CHECK_FALSE(owner->logically_drained());
        unsigned calls{};
        CHECK(owner->submit(state, "SELECT 1", phase, [&](auto) { ++calls; }) != 0);
        CHECK_FALSE(owner->logically_drained());
        owner->update(1);
        CHECK(calls == 1);
        CHECK(owner->logically_drained());
        owner->close();
        CHECK_FALSE(owner->quiescent());
        CHECK_FALSE(owner->logically_drained());
    }
    CHECK(pool->close_and_wait());
}

TEST_CASE("Idle transaction deadlines and undispatched native events prevent drain") {
    auto pool = std::make_shared<arknet::mysql::pool>(arknet::mysql::pool_options{});
    {
        auto owner = std::make_shared<mysql_transaction_owner>(
            pool, mysql_transaction_limits{1, 1024}, arknet::mysql::query_options{});
        auto state = std::make_shared<mysql_transaction_state>();
        state->phase = mysql_transaction_phase::idle;
        state->status.outcome = mysql_transaction_outcome::active;
        state->deadline = std::chrono::steady_clock::now() + 1s;
        state->cleanup_timeout = 1s;
        owner->states[0] = state;
        owner->stop_accepting();
        CHECK(owner->update(1) == 0);
        CHECK_FALSE(owner->logically_drained());
        state->deadline = std::chrono::steady_clock::now() - 1ms;
        CHECK(owner->update(1) == 0);
        CHECK(state->phase == mysql_transaction_phase::terminal);
        CHECK(state->status.discard_requested);
        CHECK(owner->logically_drained());

        state = std::make_shared<mysql_transaction_state>();
        state->phase = mysql_transaction_phase::acquiring;
        state->deadline = std::chrono::steady_clock::now() + 1s;
        owner->states[0] = state;
        CHECK_FALSE(owner->quiescent());
        state->publish(0, arknet::mysql::pool::acquire_result{});
        CHECK_FALSE(owner->logically_drained());
        CHECK(owner->update(1) == 0);
        CHECK(owner->logically_drained());
        owner->close();
    }
    CHECK(pool->close_and_wait());
}

TEST_CASE("Transaction drain waits for terminal delivery and its callback captures") {
    auto pool = std::make_shared<arknet::mysql::pool>(arknet::mysql::pool_options{});
    for (const bool throwing : {false, true}) {
        auto owner = std::make_shared<mysql_transaction_owner>(
            pool, mysql_transaction_limits{1, 1024}, arknet::mysql::query_options{});
        auto state = std::make_shared<mysql_transaction_state>();
        state->phase = mysql_transaction_phase::terminal;
        owner->states[0] = state;
        unsigned destroyed{};
        auto capture = std::make_shared<transaction_capture>(owner, destroyed);
        REQUIRE(owner->reserve(state, 32, [&, capture = std::move(capture)](auto) {
            CHECK_FALSE(owner->quiescent());
            CHECK_FALSE(owner->logically_drained());
            if (throwing) {
                throw std::runtime_error("expected transaction completion failure");
            }
        }));
        owner->stop_accepting();
        CHECK_FALSE(owner->logically_drained());
        owner->deliver(state);
        CHECK_FALSE(owner->logically_drained());
        if (throwing) {
            CHECK_THROWS_AS(owner->update(1), std::runtime_error);
            CHECK_FALSE(owner->logically_drained());
            CHECK(owner->update(1) == 0);
        } else {
            CHECK(owner->update(1) == 1);
        }
        CHECK(destroyed == 1);
        CHECK(owner->retained_input->load() == 0);
        CHECK(owner->logically_drained());
        owner->close();
    }
    CHECK(pool->close_and_wait());
}

TEST_CASE("Transaction activity survives facade destruction from inline and queued completions") {
    auto pool = std::make_shared<arknet::mysql::pool>(arknet::mysql::pool_options{});
    for (const bool queued : {false, true}) {
        for (const bool throwing : {false, true}) {
            auto service = std::make_unique<mysql_transaction_service>(pool, mysql_transaction_limits{1, 1024});
            std::weak_ptr owner = database_service_test_access::owner(*service);
            unsigned destroyed{};
            auto capture = std::make_shared<transaction_capture>(owner, destroyed);
            auto completion = [&, capture = std::move(capture)](auto) {
                CHECK_FALSE(service->logically_drained());
                service.reset();
                auto active = owner.lock();
                REQUIRE(active);
                CHECK(active->activity->depth > 0);
                CHECK_FALSE(active->logically_drained());
                if (throwing) {
                    throw std::runtime_error("expected self-destruction completion failure");
                }
            };
            if (queued) {
                auto active = owner.lock();
                auto state = std::make_shared<mysql_transaction_state>();
                state->phase = mysql_transaction_phase::terminal;
                active->states[0] = state;
                REQUIRE(active->reserve(state, 0, std::move(completion)));
                active->deliver(state);
            }
            service->stop_accepting();
            if (throwing) {
                CHECK_THROWS_AS(
                    queued ? service->update() : service->start({1s, 1s}, std::move(completion)), std::runtime_error);
            } else {
                CHECK(
                    (queued ? service->update() : service->start({1s, 1s}, std::move(completion))) == (queued ? 1 : 0));
            }
            CHECK_FALSE(service);
            CHECK(destroyed == 1);
            CHECK(owner.expired());
        }
    }
    CHECK(pool->close_and_wait());
}
