// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <atomic>
#include <chrono>
#include <cstddef>
#include <exception>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

#include "mysql/detail/mysql_transaction.hpp"
#include "mysql/mysql_plugin.hpp"
#include "mysql_test_peer.hpp"

namespace {
using namespace std::chrono_literals;
using namespace mysql_test;

struct observed_commands {
    std::atomic<unsigned> queries{}, prepares{}, executes{}, closes{}, commits{}, rollbacks{};
    std::atomic<bool> allow_rollback{true};
};

struct rollback_guard {
    observed_commands& seen;

    ~rollback_guard() {
        seen.allow_rollback = true;
    }
};

asio::awaitable<void> serve(mysql_peer& server, observed_commands& seen, bool tls = false) {
    if (tls) {
        co_await server.send(greeting("mysql_native_password", true));
        co_await server.receive();
        co_await server.upgrade();
        co_await server.receive();
        co_await server.send(ok_packet);
    } else {
        co_await authenticate(server);
    }
    bool active{};
    try {
        while (true) {
            const auto command = co_await receive_command(server);
            REQUIRE_FALSE(command.empty());
            const auto kind = static_cast<unsigned char>(command[0]);
            if (kind == 0x1f) {
                active = false;
                co_await server.send(ok_packet);
            } else if (kind == 0x03) {
                const auto sql = command.substr(1);
                if (sql.starts_with("USE ")) {
                    co_await server.send(ok_packet);
                } else if (sql == "START TRANSACTION") {
                    active = true;
                    auto ok = ok_packet;
                    ok[3] = 3;
                    co_await server.send(std::move(ok));
                } else if (sql.starts_with("COMMIT")) {
                    ++seen.commits;
                    active = false;
                    co_await server.send(ok_packet);
                } else if (sql.starts_with("ROLLBACK")) {
                    ++seen.rollbacks;
                    asio::steady_timer pause(co_await asio::this_coro::executor);
                    while (!seen.allow_rollback) {
                        pause.expires_after(1ms);
                        co_await pause.async_wait(asio::use_awaitable);
                    }
                    active = false;
                    co_await server.send(ok_packet);
                } else {
                    ++seen.queries;
                    if (sql == "slow") {
                        co_await server.wait_for_eof();
                        co_return;
                    }
                    if (active) {
                        auto ok = ok_packet;
                        ok[3] = 3;
                        co_await server.send(std::move(ok));
                    } else {
                        co_await one_integer(server, "42", true);
                    }
                }
            } else if (kind == 0x16) {
                ++seen.prepares;
                std::string response(12, '\0');
                response[1] = 7;
                response[7] = 1;
                co_await server.send(std::move(response));
                co_await server.send(column_packet("parameter"));
                co_await server.send(eof_packet);
            } else if (kind == 0x17) {
                ++seen.executes;
                REQUIRE(command.size() >= 22);
                CHECK(static_cast<unsigned char>(command[1]) == 7);
                auto ok = ok_packet;
                if (active) {
                    ok[3] = 3;
                }
                co_await server.send(std::move(ok));
            } else if (kind == 0x19) {
                ++seen.closes;
            } else {
                FAIL("Unexpected public MySQL command");
                co_return;
            }
        }
    } catch (const arknet::system_error& error) {
        CHECK((error.code() == asio::error::eof || error.code() == asio::error::connection_reset ||
               error.code() == asio::ssl::error::stream_truncated));
    }
}

struct module_owner {
    ark::plugin_manager manager;
    std::unique_ptr<ark::mysql_module> database_owner{ark::detail::make_mysql_module()};
    ark::mysql_module& database{*database_owner};

    module_owner() {
        database.set_name("mysql_module");
        database.set_plugin_manager(&manager);
        REQUIRE(manager.add_module(database.get_name(), &database));
        REQUIRE(manager.add_update_module(&database));
        REQUIRE(database.init());
    }

    void configure(mysql_peer& peer, bool tls = false, std::size_t max_waiters = 8) {
        ark::mysql_options options;
        options.pool.connection = options_for(peer, tls);
        options.pool.max_connections = 1;
        options.pool.max_waiters = max_waiters;
        options.max_transactions = 2;
        REQUIRE(database.configure("primary", std::move(options)));
    }

    void drain() {
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (manager.pending_tasks() && std::chrono::steady_clock::now() < deadline) {
            REQUIRE(manager.update(0));
            std::this_thread::sleep_for(1ms);
        }
        REQUIRE(manager.pending_tasks() == 0);
    }

    void stop() {
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (!manager.stop(0) && std::chrono::steady_clock::now() < deadline) {
            REQUIRE(manager.get_shutdown_state() != ark::plugin_manager::shutdown_state::failed);
            REQUIRE(manager.update(0));
            std::this_thread::sleep_for(1ms);
        }
        REQUIRE(manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::stopped);
    }
};

ark::task<> query_and_execute(ark::mysql_module& database, std::vector<ark::mysql_reply>& replies) {
    replies.push_back(co_await database.query("primary", std::string("SELECT 42")));
    std::vector<arknet::mysql::field_value> parameters{std::int64_t{42}};
    replies.push_back(co_await database.execute("primary", "SELECT ?", std::move(parameters)));
}

ark::task<> transact(ark::mysql_module& database, bool commit, ark::mysql_transaction_id& id) {
    const auto begun = co_await database.begin("primary", {2s, 1s});
    REQUIRE(begun);
    id = begun.transaction;
    REQUIRE(id != 0);
    REQUIRE(begun.transaction_status.outcome == ark::mysql_transaction_outcome::active);
    const auto query = co_await database.query(id, "UPDATE records SET value=42");
    REQUIRE(query);
    auto result = commit ? co_await database.commit(id) : co_await database.rollback(id);
    REQUIRE(result);
    CHECK(result.transaction_status.outcome ==
          (commit ? ark::mysql_transaction_outcome::committed : ark::mysql_transaction_outcome::rolled_back));
}

ark::task<> abandon_transaction(ark::mysql_module& database, std::optional<ark::mysql_transaction_reply>& reply) {
    reply.emplace(co_await database.begin("primary", {40ms, 1s}));
}

struct queued_query_report {
    std::optional<ark::mysql_reply> reply;
    std::exception_ptr failure;
    unsigned completions{};
    bool resumed_on_owner{};
};

ark::task<> collect_queued_query(
    module_owner& owner, std::string sql, queued_query_report& report, std::thread::id thread) {
    report.reply.emplace(co_await owner.database.query("primary", std::move(sql)));
    report.resumed_on_owner = std::this_thread::get_id() == thread &&
                              owner.manager.get_task_runtime()->get_executor().running_in_this_thread();
}

ark::task<> hold_transaction(ark::mysql_module& database, ark::mysql_transaction_id& id) {
    auto begun = co_await database.begin("primary", {4s, 500ms});
    REQUIRE(begun);
    REQUIRE(begun.transaction_status.outcome == ark::mysql_transaction_outcome::active);
    id = begun.transaction;
}

ark::task<> reuse_after_queued_cancellation(
    module_owner& owner, ark::mysql_transaction_id id, std::vector<ark::mysql_reply>& replies, std::thread::id thread) {
    const auto changed = co_await owner.database.query(id, "UPDATE records SET value=42");
    REQUIRE(changed);
    const auto rolled_back = co_await owner.database.rollback(id);
    REQUIRE(rolled_back);
    REQUIRE(rolled_back.transaction_status.outcome == ark::mysql_transaction_outcome::rolled_back);
    co_await query_and_execute(owner.database, replies);
    CHECK(std::this_thread::get_id() == thread);
    CHECK(owner.manager.get_task_runtime()->get_executor().running_in_this_thread());
}
} // namespace

TEST_CASE("Public MySQL coroutines own native query results and prepared cleanup diagnostics") {
    for (const bool tls : {false, true}) {
        observed_commands seen;
        mysql_peer peer([&seen, tls](mysql_peer& server) { return serve(server, seen, tls); });
        module_owner owner;
        owner.configure(peer, tls);
        std::vector<ark::mysql_reply> replies;
        std::exception_ptr failure;
        auto& tasks = owner.manager.create_task_group(owner.database);
        REQUIRE(tasks.start(query_and_execute(owner.database, replies), [&](auto error) { failure = error; }));
        owner.drain();
        CHECK_FALSE(failure);
        REQUIRE(replies.size() == 2);
        REQUIRE(replies[0]);
        REQUIRE(replies[1]);
        REQUIRE(replies[0].result.rows.size() == 1);
        CHECK(std::get<std::int64_t>(replies[0].result.rows[0][0]) == 42);
        REQUIRE(replies[1].statement_close);
        CHECK(bool(*replies[1].statement_close));
        owner.stop();
        peer.finish();
        CHECK(seen.queries == 1);
        CHECK(seen.prepares == 1);
        CHECK(seen.executes == 1);
        CHECK(seen.closes == 1);
        // The response remains valid after native retirement and module shut.
        CHECK(std::get<std::int64_t>(replies[0].result.rows[0][0]) == 42);
    }
}

TEST_CASE("Public MySQL transactions retain a lease across query and terminal steps") {
    for (const bool commit : {false, true}) {
        observed_commands seen;
        mysql_peer peer([&seen](mysql_peer& server) { return serve(server, seen); });
        module_owner owner;
        owner.configure(peer);
        ark::mysql_transaction_id id{};
        std::exception_ptr failure;
        auto& tasks = owner.manager.create_task_group(owner.database);
        REQUIRE(tasks.start(transact(owner.database, commit, id), [&](auto error) { failure = error; }));
        owner.drain();
        CHECK_FALSE(failure);
        owner.stop();
        peer.finish();
        CHECK(seen.commits == (commit ? 1u : 0u));
        CHECK(seen.rollbacks == (commit ? 0u : 1u));
    }
}

TEST_CASE("Public MySQL abandoned transactions drain deadline rollback before shutdown") {
    for (const bool hold_rollback : {false, true}) {
        CAPTURE(hold_rollback);
        observed_commands seen;
        seen.allow_rollback = !hold_rollback;
        mysql_peer peer([&seen](mysql_peer& server) { return serve(server, seen); });
        module_owner owner;
        owner.configure(peer);
        std::optional<ark::mysql_transaction_reply> begun;
        std::exception_ptr failure;

        struct drain_guard {
            observed_commands& seen;
            module_owner& owner;

            ~drain_guard() {
                seen.allow_rollback = true;
                owner.stop();
            }
        } guard{seen, owner};

        auto& tasks = owner.manager.create_task_group(owner.database);
        REQUIRE(tasks.start(abandon_transaction(owner.database, begun), [&](auto error) { failure = error; }));
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (!seen.rollbacks && std::chrono::steady_clock::now() < deadline) {
            REQUIRE(owner.manager.update(0));
            std::this_thread::sleep_for(1ms);
        }
        REQUIRE(seen.rollbacks == 1);
        if (hold_rollback) {
            // The peer has received ROLLBACK but cannot complete it yet.
            CHECK_FALSE(owner.manager.stop(0));
            CHECK(owner.manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::stopping);
            seen.allow_rollback = true;
        }
        owner.drain();
        REQUIRE_FALSE(failure);
        REQUIRE(begun);
        REQUIRE(*begun);
        if (begun->transaction) {
            CHECK(begun->transaction_status.outcome == ark::mysql_transaction_outcome::active);
        } else {
            CHECK(begun->transaction_status.outcome == ark::mysql_transaction_outcome::rolled_back);
            REQUIRE(begun->rollback);
            CHECK(*begun->rollback);
        }
        // Cleanup may already be complete, so the first stop can return true.
        owner.stop();
        peer.finish();
        CHECK(seen.commits == 0);
        CHECK(seen.rollbacks == 1);
    }
}

TEST_CASE("MySQL owner delivery preserves successful BEGIN results across transaction expiry") {
    using namespace ark::detail;
    for (const bool expire_before_delivery : {false, true}) {
        CAPTURE(expire_before_delivery);
        observed_commands seen;
        seen.allow_rollback = false;
        mysql_peer peer([&seen](mysql_peer& server) { return serve(server, seen); });
        rollback_guard guard{seen};
        arknet::mysql::pool_options options;
        options.connection = options_for(peer);
        options.max_connections = 1;
        auto pool = std::make_shared<arknet::mysql::pool>(std::move(options));
        auto owner = std::make_shared<mysql_transaction_owner>(
            pool, mysql_transaction_limits{1, 1024}, arknet::mysql::query_options{});
        std::optional<mysql_transaction_reply> reply;
        unsigned calls{};
        REQUIRE(owner->begin({2s, 1s}, [&](auto value) {
            ++calls;
            reply.emplace(std::move(value));
        }) != 0);
        const auto state = owner->states.front();
        REQUIRE(state);
        const auto wait_until = [](auto ready) {
            const auto deadline = std::chrono::steady_clock::now() + 5s;
            while (!ready()) {
                REQUIRE(std::chrono::steady_clock::now() < deadline);
                std::this_thread::sleep_for(1ms);
            }
        };
        wait_until([&] {
            owner->update(1);
            return state->phase == mysql_transaction_phase::beginning;
        });
        // Wait for the native success without advancing its owner. This makes
        // expiry-before-delivery independent of OS scheduling and handshake time.
        wait_until([&] {
            std::lock_guard lock(state->event_mutex);
            return state->completed.has_value();
        });
        {
            std::lock_guard lock(state->event_mutex);
            const auto* begun = std::get_if<arknet::mysql::query_result>(&*state->completed);
            REQUIRE(begun);
            REQUIRE(*begun);
            REQUIRE((begun->status_flags & 1u) != 0);
        }
        if (!expire_before_delivery) {
            state->deadline = std::chrono::steady_clock::time_point::max();
            REQUIRE(owner->update(1) == 1);
            REQUIRE(reply);
            REQUIRE(*reply);
            REQUIRE(reply->transaction);
            REQUIRE(reply->transaction_status.outcome == mysql_transaction_outcome::active);
        }
        owner->stop_accepting();
        CHECK_FALSE(owner->logically_drained());
        state->deadline = std::chrono::steady_clock::now() - 1ms;
        CHECK(owner->update(1) == 0);
        wait_until([&] { return seen.rollbacks == 1; });
        CHECK(state->phase == mysql_transaction_phase::cleanup);
        CHECK_FALSE(owner->logically_drained());
        CHECK(calls == (expire_before_delivery ? 0u : 1u));
        seen.allow_rollback = true;
        wait_until([&] {
            owner->update(1);
            return owner->logically_drained();
        });
        REQUIRE(reply);
        REQUIRE(*reply);
        CHECK(calls == 1);
        if (expire_before_delivery) {
            // bool reports the business result, not a live transaction token.
            CHECK_FALSE(reply->transaction);
            CHECK(reply->transaction_status.outcome == mysql_transaction_outcome::rolled_back);
            REQUIRE(reply->rollback);
            CHECK(*reply->rollback);
        } else {
            CHECK(reply->transaction->snapshot().outcome == mysql_transaction_outcome::rolled_back);
        }
        CHECK_FALSE(state->status.discard_requested);
        CHECK(state->status.rollback_code == arknet::mysql::connection_errc::none);
        CHECK(seen.commits == 0);
        CHECK(seen.rollbacks == 1);
        owner->close();
        REQUIRE(pool->close_and_wait());
        peer.finish();
    }
}

TEST_CASE("Public MySQL rejects foreign executors and invalid configuration before starting IO") {
    module_owner owner;
    ark::mysql_options options;
    CHECK_THROWS_AS(owner.database.configure("", options), std::invalid_argument);
    options.max_operations = 0;
    CHECK_THROWS_AS(owner.database.configure("invalid", options), std::invalid_argument);
    options.max_operations = 1;
    REQUIRE(owner.database.configure("primary", options));
    CHECK_FALSE(owner.database.configure("primary", options));
    CHECK_THROWS_AS((void)owner.database.query("missing", "SELECT 1"), std::invalid_argument);
    CHECK_THROWS_AS((void)owner.database.commit(999), std::invalid_argument);
    auto foreign = std::make_shared<arknet::manual_runtime>();
    std::exception_ptr failure;
    asio::co_spawn(foreign->get_executor(), owner.database.query("primary", "SELECT 1"),
        [&](std::exception_ptr error, ark::mysql_reply) { failure = error; });
    foreign->poll(64);
    REQUIRE(failure);
    CHECK_THROWS_AS(std::rethrow_exception(failure), std::logic_error);
    owner.stop();
}

TEST_CASE("Manager stop cancels sent MySQL work and retires unknown outcomes without replay") {
    observed_commands seen;
    mysql_peer peer([&seen](mysql_peer& server) { return serve(server, seen); });
    module_owner owner;
    owner.configure(peer);
    std::vector<ark::mysql_reply> replies;
    auto slow = [](ark::mysql_module& database, std::vector<ark::mysql_reply>& values) -> ark::task<> {
        values.push_back(co_await database.query("primary", "slow"));
    };
    std::exception_ptr failure;
    auto& tasks = owner.manager.create_task_group(owner.database);
    const auto id = tasks.start(slow(owner.database, replies), [&](auto error) { failure = error; });
    REQUIRE(id);
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!seen.queries && std::chrono::steady_clock::now() < deadline) {
        REQUIRE(owner.manager.update(0));
        std::this_thread::sleep_for(1ms);
    }
    REQUIRE(seen.queries == 1);
    CHECK_FALSE(owner.manager.stop(0));
    CHECK_FALSE(owner.database.configure("late", {}));
    owner.drain();
    CHECK_FALSE(failure);
    REQUIRE(replies.size() == 1);
    CHECK_FALSE(replies.front());
    CHECK(replies.front().result.status.outcome_unknown);
    owner.stop();
    peer.finish();
    CHECK(seen.queries == 1);
}

TEST_CASE("Public MySQL cancels a queued acquire without losing its transaction lease or pool capacity") {
    observed_commands seen;
    mysql_peer peer([&seen](mysql_peer& server) { return serve(server, seen, true); });
    queued_query_report waiting, overflow;
    std::vector<ark::mysql_reply> reused;
    std::exception_ptr failure;
    ark::mysql_transaction_id transaction{};
    module_owner owner;
    owner.configure(peer, true, 1);
    auto& tasks = owner.manager.create_task_group(owner.database);
    const auto thread = std::this_thread::get_id();

    // Drain before destroying reports captured by pending completions, including
    // when an assertion below unwinds the host stack.
    struct drain_guard {
        module_owner& owner;
        ark::task_group& tasks;

        ~drain_guard() {
            tasks.cancel_all();
            owner.stop();
        }
    } guard{owner, tasks};

    REQUIRE(tasks.start(hold_transaction(owner.database, transaction), [&](auto error) { failure = error; }));
    owner.drain();
    REQUIRE_FALSE(failure);
    REQUIRE(transaction != 0);

    const auto queued =
        tasks.start(collect_queued_query(owner, "must remain unsent", waiting, thread), [&](auto error) {
            waiting.failure = error;
            ++waiting.completions;
            CHECK(std::this_thread::get_id() == thread);
        });
    REQUIRE(queued);
    REQUIRE(owner.manager.update(0));
    REQUIRE(tasks.start(collect_queued_query(owner, "must not fit", overflow, thread), [&](auto error) {
        overflow.failure = error;
        ++overflow.completions;
        CHECK(std::this_thread::get_id() == thread);
    }));
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!overflow.completions && std::chrono::steady_clock::now() < deadline) {
        REQUIRE(owner.manager.update(0));
        std::this_thread::sleep_for(1ms);
    }
    REQUIRE(overflow.completions == 1);
    REQUIRE_FALSE(overflow.failure);
    REQUIRE(overflow.reply);
    REQUIRE(overflow.reply->result.status.code == arknet::mysql::connection_errc::queue_full);
    CHECK_FALSE(overflow.reply->result.status.outcome_unknown);
    CHECK(overflow.resumed_on_owner);
    // A held sole lease plus a full one-waiter pool establishes waiting state;
    // elapsed time is not used to infer whether a query has been sent.
    REQUIRE_FALSE(waiting.reply);
    REQUIRE(waiting.completions == 0);
    CHECK(seen.queries == 0);
    REQUIRE(tasks.cancel(queued));
    owner.drain();
    REQUIRE(waiting.completions == 1);
    REQUIRE_FALSE(waiting.failure);
    REQUIRE(waiting.reply);
    CHECK(waiting.reply->result.status.code == arknet::mysql::connection_errc::cancelled);
    CHECK_FALSE(waiting.reply->result.status.outcome_unknown);
    CHECK(waiting.resumed_on_owner);
    CHECK(seen.queries == 0);

    REQUIRE(tasks.start(
        reuse_after_queued_cancellation(owner, transaction, reused, thread), [&](auto error) { failure = error; }));
    owner.drain();
    owner.stop();
    REQUIRE_FALSE(failure);
    REQUIRE(reused.size() == 2);
    CHECK(bool(reused[0]));
    CHECK(bool(reused[1]));
    CHECK(waiting.completions == 1);
    CHECK(overflow.completions == 1);
    peer.finish();
    // This peer accepts one socket only: successful later work demonstrates
    // reuse of that connection, not replacement after cancelling a waiter.
    CHECK(seen.queries == 2);
    CHECK(seen.rollbacks == 1);
    CHECK(seen.prepares == 1);
    CHECK(seen.executes == 1);
    CHECK(seen.closes == 1);
}
