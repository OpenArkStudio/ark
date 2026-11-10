// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "detail/database_awaitable.hpp"
#include "mysql/detail/mysql_service.hpp"
#include "mysql/detail/mysql_transaction_awaitable.hpp"
#include "mysql_transaction_relay.hpp"

namespace {

using namespace std::chrono_literals;
using namespace ark::detail;
using namespace arknet::mysql;

unsigned checks{};

void require(bool value, const std::string& message) {
    ++checks;
    if (!value) {
        throw std::runtime_error(message);
    }
}

std::string environment(const char* name, bool empty_allowed = false) {
    const auto* value = std::getenv(name);
    if (!value || (!empty_allowed && !*value)) {
        throw std::invalid_argument(std::string("Required test environment variable: ") + name);
    }
    return value;
}

pool_options test_options() {
    pool_options options;
    options.max_connections = 1;
    options.max_waiters = 4;
    options.acquire_timeout = 2s;
    options.reset_timeout = 2s;
    options.connection.host = environment("ARK_DATABASE_MYSQL_HOST");
    if (options.connection.host != "127.0.0.1") {
        throw std::invalid_argument("Transaction fault-relay tests require ARK_DATABASE_MYSQL_HOST=127.0.0.1");
    }
    const auto port = environment("ARK_DATABASE_MYSQL_PORT");
    unsigned number{};
    const auto parsed = std::from_chars(port.data(), port.data() + port.size(), number);
    if (parsed.ec != std::errc{} || parsed.ptr != port.data() + port.size() || !number || number > 65535) {
        throw std::invalid_argument("ARK_DATABASE_MYSQL_PORT must be an integer between 1 and 65535");
    }
    options.connection.port = static_cast<std::uint16_t>(number);
    options.connection.username = environment("ARK_DATABASE_MYSQL_USER");
    options.connection.password = environment("ARK_DATABASE_MYSQL_PASSWORD", true);
    options.connection.database = environment("ARK_DATABASE_MYSQL_DATABASE");
    options.connection.ca_file = environment("ARK_DATABASE_MYSQL_CA");
    return options;
}

template<class predicate_type>
void wait_until(predicate_type predicate) {
    const auto end = std::chrono::steady_clock::now() + 6s;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= end) {
            throw std::runtime_error("Timed out waiting for a transaction observation");
        }
        std::this_thread::sleep_for(1ms);
    }
}

mysql_service_reply observe(mysql_service& observer, std::string sql) {
    std::optional<mysql_service_result> result;
    require(observer.start_query(
                std::move(sql), [&](auto value) { result = std::move(value); }, 3s) != 0,
        "Observer query was not admitted");
    wait_until([&] {
        observer.update();
        return bool(result);
    });
    if (auto* failure = std::get_if<std::exception_ptr>(&*result)) {
        std::rethrow_exception(*failure);
    }
    auto reply = std::get<mysql_service_reply>(std::move(*result));
    require(bool(reply.result), "Observer query failed");
    return reply;
}

std::int64_t integer(const query_result& result) {
    require(result.rows.size() == 1 && result.rows[0].size() == 1, "Expected one scalar result");
    if (const auto* number = std::get_if<std::uint64_t>(&result.rows[0][0])) {
        return static_cast<std::int64_t>(*number);
    }
    return std::get<std::int64_t>(result.rows[0][0]);
}

template<class submitter>
mysql_transaction_reply run(mysql_transaction_service& service, submitter submit) {
    std::optional<mysql_transaction_reply> response;
    const auto owner = std::this_thread::get_id();
    unsigned calls{};
    require(submit([&](auto reply) {
        require(std::this_thread::get_id() == owner, "Transaction callback escaped the module thread");
        require(++calls == 1, "Transaction callback ran twice");
        response = std::move(reply);
    }) != 0,
        "Transaction operation was not admitted");
    wait_until([&] {
        service.update();
        return bool(response);
    });
    require(calls == 1, "Missing transaction callback");
    if (response->initiation_exception) {
        std::rethrow_exception(response->initiation_exception);
    }
    return std::move(*response);
}

mysql_transaction begin(mysql_transaction_service& service, std::chrono::milliseconds timeout = 5s,
    std::chrono::milliseconds cleanup_timeout = 2s) {
    auto reply =
        run(service, [&](auto complete) { return service.start({timeout, cleanup_timeout}, std::move(complete)); });
    require(bool(reply), "START TRANSACTION failed");
    require(reply.transaction.has_value(), "BEGIN did not return its move-only scope");
    require(reply.transaction_status.outcome == mysql_transaction_outcome::active, "BEGIN did not activate scope");
    return std::move(*reply.transaction);
}

mysql_transaction_reply query(mysql_transaction_service& service, mysql_transaction& transaction, std::string sql) {
    return run(service, [&](auto complete) { return transaction.start_query(std::move(sql), std::move(complete)); });
}

mysql_transaction_reply execute(mysql_transaction_service& service, mysql_transaction& transaction, std::string sql,
    std::vector<field_value> parameters) {
    return run(service, [&](auto complete) {
        return transaction.start_execute(std::move(sql), std::move(parameters), std::move(complete));
    });
}

std::uint64_t prepared_count(mysql_service& observer) {
    auto result = observe(observer, "SHOW GLOBAL STATUS LIKE 'Prepared_stmt_count'");
    require(result.result.rows.size() == 1 && result.result.rows[0].size() == 2,
        "Prepared statement count did not return a status row");
    return std::stoull(std::get<std::string>(result.result.rows[0][1]));
}

void success(const mysql_transaction_reply& result) {
    require(bool(result), "Transaction SQL failed");
    require(!result.initiation_exception, "Transaction initiation threw");
}

void check_commit(mysql_transaction_service& service, mysql_service& observer, const std::string& table) {
    auto transaction = begin(service);
    const auto first = query(service, transaction, "SELECT CONNECTION_ID()");
    success(first);
    success(query(service, transaction, "INSERT INTO " + table + " VALUES(1, 'retained')"));
    const auto second = query(service, transaction, "SELECT CONNECTION_ID()");
    success(second);
    const auto binary = query(service, transaction, "SELECT X'00746c73ff00'");
    success(binary);
    require(integer(first.result) == integer(second.result), "Transaction switched its native lease");
    require(
        integer(observe(observer, "SELECT COUNT(*) FROM " + table).result) == 0, "Observer saw an uncommitted write");
    success(query(service, transaction, "SET SESSION completion_type=1"));
    auto result = run(service, [&](auto complete) { return transaction.start_commit(std::move(complete)); });
    success(result);
    require(result.transaction_status.outcome == mysql_transaction_outcome::committed,
        "COMMIT did not preserve its acknowledged outcome");
    require(!result.transaction_status.discard_requested, "A known successful commit was discarded");
    require(integer(observe(observer, "SELECT COUNT(*) FROM " + table).result) == 1,
        "Observer did not see the committed write");
    auto retained = query_result(first.result);
    transaction = {};
    service.update();
    require(service.active() == 0 && service.retained_input_bytes() == 0, "Commit retained its reservation");
    auto next = begin(service);
    require(integer(query(service, next, "SELECT CONNECTION_ID()").result) == integer(retained),
        "NO CHAIN did not permit same-connection reset and reuse");
    success(query(service, next, "SET SESSION completion_type=2"));
    success(query(service, next, "INSERT INTO " + table + " VALUES(4, 'explicit rollback')"));
    auto rolled = run(service, [&](auto complete) { return next.start_rollback(std::move(complete)); });
    require(rolled.transaction_status.outcome == mysql_transaction_outcome::rolled_back,
        "Explicit rollback did not end the scope");
    require(integer(observe(observer, "SELECT COUNT(*) FROM " + table + " WHERE id=4").result) == 0,
        "Explicit rollback retained its write");
    require(std::get<std::string>(binary.result.rows[0][0]) == std::string("\0tls\xff\0", 6),
        "Binary transaction result was not owned after later steps and commit");
    auto reused = begin(service);
    require(integer(query(service, reused, "SELECT CONNECTION_ID()").result) == integer(retained),
        "NO RELEASE did not preserve a reusable connection");
    run(service, [&](auto complete) { return reused.start_rollback(std::move(complete)); });
}

void check_abort(mysql_transaction_service& service, mysql_service& observer, const std::string& table) {
    auto transaction = begin(service);
    success(query(service, transaction, "INSERT INTO " + table + " VALUES(2, 'rollback')"));
    auto failed = query(service, transaction, "INSERT INTO " + table + " VALUES(1, 'duplicate')");
    require(failed.result.status.code == connection_errc::server && failed.result.status.server_code == 1062 &&
                failed.result.status.sql_state == "23000",
        "Rollback overwrote the original server error");
    require(failed.rollback && bool(*failed.rollback), "Known SQL error did not get an explicit rollback result");
    require(failed.transaction_status.outcome == mysql_transaction_outcome::rolled_back,
        "Known SQL error left the scope active");
    require(integer(observe(observer, "SELECT COUNT(*) FROM " + table + " WHERE id=2").result) == 0,
        "Automatic rollback left a prior successful write committed");
    unsigned rejected{};
    require(transaction.start_commit([&](auto result) {
        ++rejected;
        require(result.result.status.code == connection_errc::not_connected, "Terminal scope accepted another commit");
    }) == 0 && rejected == 1,
        "Terminal scope did not reject synchronously");
}

void check_prepared(mysql_transaction_service& service, mysql_service& observer, const std::string& table) {
    const auto count = prepared_count(observer);
    auto transaction = begin(service);
    const auto connection = integer(query(service, transaction, "SELECT CONNECTION_ID()").result);
    const std::string binary("own\0ed\xff", 7);
    auto selected =
        execute(service, transaction, "SELECT CONNECTION_ID(), ?, ?, ?", {binary, std::int64_t{-7}, std::monostate{}});
    success(selected);
    require(selected.statement_close && bool(*selected.statement_close) && !selected.statement_close_exception,
        "Prepared statement was not explicitly closed on its held lease");
    require(selected.result.rows.size() == 1 && selected.result.rows[0].size() == 4, "Prepared result shape was lost");
    const auto& row = selected.result.rows[0];
    require(std::get<std::uint64_t>(row[0]) == static_cast<std::uint64_t>(connection),
        "Prepared execution acquired a different connection");
    require(std::get<std::string>(row[1]) == binary && std::get<std::int64_t>(row[2]) == -7 &&
                std::holds_alternative<std::monostate>(row[3]),
        "Prepared parameters or owning binary results were changed");
    auto inserted = execute(service, transaction, "INSERT INTO " + table + " VALUES(?, ?)", {std::int64_t{10}, binary});
    success(inserted);
    require(inserted.result.affected_rows == 1 && inserted.statement_close && bool(*inserted.statement_close),
        "Parameterized write or statement cleanup failed");
    require(integer(observe(observer, "SELECT COUNT(*) FROM " + table + " WHERE id=10").result) == 0,
        "Parameterized write escaped its transaction before commit");
    wait_until([&] { return prepared_count(observer) == count; });
    success(run(service, [&](auto complete) { return transaction.start_commit(std::move(complete)); }));
    require(integer(observe(observer, "SELECT COUNT(*) FROM " + table + " WHERE id=10").result) == 1,
        "Parameterized commit did not persist its write");
    require(std::get<std::string>(selected.result.rows[0][1]) == binary,
        "Prepared result stopped owning its bytes after commit");

    transaction = begin(service);
    success(execute(
        service, transaction, "INSERT INTO " + table + " VALUES(?, ?)", {std::int64_t{11}, std::string("rollback")}));
    success(run(service, [&](auto complete) { return transaction.start_rollback(std::move(complete)); }));
    require(integer(observe(observer, "SELECT COUNT(*) FROM " + table + " WHERE id=11").result) == 0,
        "Parameterized rollback retained a write");

    transaction = begin(service);
    success(execute(service, transaction, "INSERT INTO " + table + " VALUES(?, ?)",
        {std::int64_t{12}, std::string("must roll back")}));
    auto duplicate = execute(
        service, transaction, "INSERT INTO " + table + " VALUES(?, ?)", {std::int64_t{10}, std::string("duplicate")});
    require(duplicate.result.status.code == connection_errc::server && duplicate.result.status.server_code == 1062 &&
                duplicate.result.status.sql_state == "23000",
        "Prepared execution lost its original server error");
    require(duplicate.statement_close && bool(*duplicate.statement_close) && duplicate.rollback &&
                bool(*duplicate.rollback) &&
                duplicate.transaction_status.outcome == mysql_transaction_outcome::rolled_back,
        "Prepared SQL error did not close its statement and roll back the same scope");
    require(integer(observe(observer, "SELECT COUNT(*) FROM " + table + " WHERE id=12").result) == 0,
        "Prepared SQL error committed an earlier step");
    transaction = begin(service);
    auto invalid = execute(service, transaction, "SELECT ?", {});
    require(invalid.result.status.code == connection_errc::invalid_options && invalid.statement_close &&
                bool(*invalid.statement_close) && invalid.rollback && bool(*invalid.rollback),
        "Parameter-count rejection leaked a statement or changed the native error");
    transaction = begin(service);
    auto prepare_error =
        execute(service, transaction, "SELECT * FROM " + table + "_missing WHERE id=?", {std::int64_t{1}});
    require(prepare_error.result.status.code == connection_errc::server &&
                prepare_error.result.status.server_code == 1146 && !prepare_error.statement_close &&
                prepare_error.rollback && bool(*prepare_error.rollback),
        "Prepare failure lost its server error or attempted to execute a nonexistent statement");
    transaction = begin(service);
    require(integer(query(service, transaction, "SELECT CONNECTION_ID()").result) == connection,
        "Known prepared failures discarded a usable lease or returned it before cleanup");
    success(run(service, [&](auto complete) { return transaction.start_rollback(std::move(complete)); }));
    require(prepared_count(observer) == count && service.retained_input_bytes() == 0,
        "Prepared failures leaked server statements or retained input");
}

void check_prepared_deadline(mysql_transaction_service& service, mysql_service& observer, const std::string& table) {
    const auto count = prepared_count(observer);
    const auto started = std::chrono::steady_clock::now();
    auto transaction = begin(service, 500ms);
    std::optional<mysql_transaction_reply> response;
    require(
        transaction.start_execute("INSERT INTO " + table + " VALUES(?, ?)",
            {std::int64_t{13}, std::string("must not execute")}, [&](auto reply) { response = std::move(reply); }) != 0,
        "Deadline test did not admit prepare");
    // Pause the module owner after the server creates the statement. Preparation
    // can finish on IO, but execution cannot start until the owner is polled.
    wait_until([&] { return prepared_count(observer) == count + 1; });
    std::this_thread::sleep_until(started + 550ms);
    wait_until([&] {
        service.update();
        return bool(response);
    });
    require(response->result.status.code == connection_errc::timeout && !response->result.status.outcome_unknown,
        "Prepare-to-execute transition refreshed the absolute deadline");
    require(response->statement_close && response->statement_close->code == connection_errc::timeout &&
                response->transaction_status.outcome == mysql_transaction_outcome::unconfirmed &&
                response->transaction_status.discard_requested && !response->rollback,
        "Expired statement cleanup refreshed the business budget or invented a confirmed rollback");
    wait_until([&] { return prepared_count(observer) == count; });
    require(integer(observe(observer, "SELECT COUNT(*) FROM " + table + " WHERE id=13").result) == 0,
        "Expired prepared step executed its write");
    require(service.retained_input_bytes() == 0, "Expired prepared step retained input quota");
}

void check_prepared_cancellation(mysql_transaction_service& service, mysql_service& observer) {
    const auto count = prepared_count(observer);
    auto transaction = begin(service);
    const auto tag = "ark_prepared_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::optional<mysql_transaction_reply> response;
    const auto id = transaction.start_execute(
        "SELECT SLEEP(?) /*" + tag + "*/", {std::int64_t{2}}, [&](auto reply) { response = std::move(reply); });
    require(id != 0, "Prepared cancellation was not admitted");
    wait_until([&] {
        service.update();
        auto active =
            observe(observer, "SELECT COUNT(*) FROM information_schema.processlist WHERE ID <> CONNECTION_ID() "
                              "AND STATE='User sleep' AND INFO LIKE '%" +
                                  tag + "%'");
        return integer(active.result) == 1;
    });
    require(transaction.cancel(id), "Observed prepared execution could not be cancelled");
    wait_until([&] {
        service.update();
        return bool(response);
    });
    require(response->result.status.code == connection_errc::cancelled && response->result.status.outcome_unknown &&
                response->transaction_status.outcome == mysql_transaction_outcome::unconfirmed &&
                response->transaction_status.discard_requested && !response->statement_close && !response->rollback,
        "Cancelled prepared execution lost its unknown outcome or continued with cleanup commands");
    wait_until([&] { return prepared_count(observer) == count; });
    auto next = begin(service);
    require(!next.cancel(id), "Stale prepared request ID cancelled a later scope");
    success(run(service, [&](auto complete) { return next.start_rollback(std::move(complete)); }));
    require(service.retained_input_bytes() == 0, "Cancelled prepared step kept admission quota");
}

void check_prepare_cancellation(const pool_options& options, mysql_service& observer, const std::string& table) {
    const auto count = prepared_count(observer);
    mysql_transaction_relay relay(options.connection.port);
    auto relayed = options;
    relayed.connection.port = relay.port();
    auto native = std::make_shared<pool>(relayed);
    {
        mysql_transaction_service service(native, {1, 8192});
        auto transaction = begin(service);
        relay.drop_responses();
        std::optional<mysql_transaction_reply> response;
        const auto id = transaction.start_execute("INSERT INTO " + table + " VALUES(?, ?)",
            {std::int64_t{14}, std::string("cancel before execute")}, [&](auto reply) { response = std::move(reply); });
        require(id != 0, "Relayed prepare cancellation was not admitted");
        wait_until([&] { return prepared_count(observer) == count + 1; });
        require(transaction.cancel(id), "Sent prepare could not be cancelled while its response was withheld");
        wait_until([&] {
            service.update();
            return bool(response);
        });
        require(response->result.status.code == connection_errc::cancelled && response->result.status.outcome_unknown &&
                    response->transaction_status.discard_requested && !response->statement_close && !response->rollback,
            "Cancelled prepare continued into execution or lost the native uncertain result");
        require(relay.discarded_bytes() != 0 && !relay.failed(), "Prepare response withholding was not observed");
        wait_until([&] { return prepared_count(observer) == count; });
        require(integer(observe(observer, "SELECT COUNT(*) FROM " + table + " WHERE id=14").result) == 0,
            "Cancelled prepare executed its parameterized write");
        require(service.retained_input_bytes() == 0 && service.active() == 0,
            "Cancelled prepare retained its transaction or parameter quota");
    }
    require(native->close_and_wait(), "Prepare-cancellation native pool did not retire explicitly");
}

void check_prepare_delivery_cancellation(
    const std::shared_ptr<pool>& native, mysql_service& observer, const std::string& table) {
    const auto count = prepared_count(observer);
    // Exercise the production owner directly only to observe its private mailbox
    // barrier. No synthetic response or protocol endpoint is involved.
    auto owner = std::make_shared<mysql_transaction_owner>(native, mysql_transaction_limits{1, 8192}, query_options{});
    try {
        std::optional<mysql_transaction_reply> begun;
        require(owner->begin({3s, 1s}, [&](auto reply) { begun = std::move(reply); }) != 0,
            "Prepare-delivery barrier did not admit BEGIN");
        wait_until([&] {
            owner->update(64);
            return bool(begun);
        });
        require(begun->transaction.has_value(), "Prepare-delivery barrier had no active scope");
        auto transaction = std::move(*begun->transaction);
        auto state = owner->states[0];
        std::optional<mysql_transaction_reply> response;
        const auto id = transaction.start_execute("INSERT INTO " + table + " VALUES(?, ?)",
            {std::int64_t{15}, std::string("must never execute")}, [&](auto reply) { response = std::move(reply); });
        require(id != 0, "Prepare-delivery barrier did not admit its step");
        wait_until([&] {
            std::lock_guard lock(state->event_mutex);
            return state->completed && std::holds_alternative<prepare_result>(*state->completed);
        });
        {
            std::lock_guard lock(state->event_mutex);
            require(
                bool(std::get<prepare_result>(*state->completed)), "Barrier did not observe successful preparation");
        }
        require(prepared_count(observer) == count + 1, "Barrier did not retain the actual server statement");
        require(transaction.cancel(id), "Logical cancellation was rejected after prepare publication");
        require(!transaction.cancel(id), "Barrier cancellation was accepted twice");
        wait_until([&] {
            owner->update(64);
            return bool(response);
        });
        require(response->result.status.code == connection_errc::cancelled && !response->result.status.outcome_unknown,
            "Cancelled pending transition executed or fabricated an uncertain business write");
        require(response->statement_close && bool(*response->statement_close) && response->rollback &&
                    bool(*response->rollback) &&
                    response->transaction_status.outcome == mysql_transaction_outcome::rolled_back &&
                    !response->transaction_status.discard_requested,
            "Cancelled pending transition did not close its statement and complete rollback");
        require(integer(observe(observer, "SELECT COUNT(*) FROM " + table + " WHERE id=15").result) == 0,
            "A prepared write ran after cancellation at the native-publication barrier");
        require(prepared_count(observer) == count && owner->retained_input->load() == 0,
            "Barrier cancellation leaked a statement or parameter reservation");
    } catch (...) {
        owner->close();
        throw;
    }
    owner->close();
}

void check_bounds(mysql_transaction_service& service, mysql_service& observer, const std::string& table) {
    auto transaction = begin(service, 500ms);
    success(query(service, transaction, "INSERT INTO " + table + " VALUES(3, 'expired')"));
    std::this_thread::sleep_for(550ms);
    wait_until([&] {
        service.update();
        return service.active() == 0;
    });
    require(transaction.snapshot().outcome == mysql_transaction_outcome::rolled_back,
        "Idle deadline did not complete its independent cleanup grace");
    require(integer(observe(observer, "SELECT COUNT(*) FROM " + table + " WHERE id=3").result) == 0,
        "Expired idle transaction left its write visible");

    transaction = begin(service);
    std::string oversized("SELECT 1");
    oversized.reserve(16384);
    unsigned rejected{};
    require(transaction.start_query(std::move(oversized),
                [&](auto result) {
                    ++rejected;
                    require(result.result.status.code == connection_errc::queue_full,
                        "Retained-input limit was not enforced");
                }) == 0 &&
                rejected == 1,
        "Oversized transaction input was admitted");
    require(service.retained_input_bytes() == 0, "Rejected input kept its byte reservation");
    success(query(service, transaction, "SELECT 1"));
    run(service, [&](auto complete) { return transaction.start_rollback(std::move(complete)); });
}

void check_waiter(const std::shared_ptr<pool>& native, mysql_service& observer) {
    mysql_transaction_service service(native, {2, 8192});
    auto holder = begin(service);
    auto waiting = run(service, [&](auto complete) { return service.start({100ms, 1s}, std::move(complete)); });
    require(waiting.result.status.code == connection_errc::timeout && !waiting.result.status.outcome_unknown &&
                !waiting.transaction,
        "Waiting transaction refreshed its deadline or began without its lease: code=" +
            std::to_string(static_cast<int>(waiting.result.status.code)) +
            ", unknown=" + std::to_string(waiting.result.status.outcome_unknown) +
            ", handle=" + std::to_string(waiting.transaction.has_value()));
    auto first = query(service, holder, "SELECT CONNECTION_ID()");
    require(integer(first.result) != integer(observe(observer, "SELECT CONNECTION_ID()").result),
        "Observer unexpectedly shares the transaction connection");
    holder.abort();
    wait_until([&] {
        service.update();
        return service.active() == 0;
    });
    require(holder.snapshot().outcome == mysql_transaction_outcome::rolled_back,
        "Abort did not explicitly rollback on the held lease");

    holder = begin(service);
    const auto started = std::chrono::steady_clock::now();
    std::optional<mysql_transaction_reply> delayed;
    require(service.start({400ms, 1s}, [&](auto reply) { delayed = std::move(reply); }) != 0,
        "Partially delayed transaction was not admitted");
    std::this_thread::sleep_for(150ms);
    success(run(service, [&](auto complete) { return holder.start_rollback(std::move(complete)); }));
    wait_until([&] {
        service.update();
        return bool(delayed);
    });
    require(delayed->transaction.has_value(), "Transaction did not acquire the released lease within its deadline");
    auto acquired = std::move(*delayed->transaction);
    std::this_thread::sleep_until(started + 450ms);
    unsigned expired{};
    require(acquired.start_query("SELECT 1",
                [&](auto reply) {
                    ++expired;
                    require(
                        reply.result.status.code == connection_errc::timeout && !reply.result.status.outcome_unknown,
                        "Expired delayed transaction lost its rejection reason");
                }) == 0 &&
                expired == 1,
        "Lease wait refreshed the transaction's absolute business deadline");
    wait_until([&] {
        service.update();
        return service.active() == 0;
    });
    require(acquired.snapshot().outcome == mysql_transaction_outcome::rolled_back,
        "Expired delayed transaction did not finish its independent cleanup");
}

void check_unknown_commit(const pool_options& options, mysql_service& observer, const std::string& table) {
    mysql_transaction_relay relay(options.connection.port);
    auto relayed = options;
    relayed.connection.port = relay.port();
    auto native = std::make_shared<pool>(relayed);
    {
        mysql_transaction_service service(native, {1, 8192});
        auto transaction = begin(service, 1s);
        success(query(service, transaction, "INSERT INTO " + table + " VALUES(5, 'unknown commit')"));
        require(integer(observe(observer, "SELECT COUNT(*) FROM " + table + " WHERE id=5").result) == 0,
            "Relay test write was already committed before fault injection");
        relay.drop_responses();
        std::optional<mysql_transaction_reply> response;
        const auto id = transaction.start_commit([&](auto result) { response = std::move(result); });
        require(id != 0, "Relayed COMMIT was not admitted");
        wait_until([&] {
            service.update();
            return integer(observe(observer, "SELECT COUNT(*) FROM " + table + " WHERE id=5").result) == 1;
        });
        wait_until([&] {
            service.update();
            return bool(response);
        });
        require(relay.discarded_bytes() != 0 && !relay.failed(), "COMMIT response fault injection was not observed");
        require(response->result.status.code == connection_errc::timeout && response->result.status.outcome_unknown,
            "Lost COMMIT reply did not retain the native unknown outcome: code=" +
                std::to_string(static_cast<int>(response->result.status.code)) +
                ", unknown=" + std::to_string(response->result.status.outcome_unknown));
        require(response->transaction_status.outcome == mysql_transaction_outcome::unconfirmed &&
                    response->transaction_status.discard_requested && !response->rollback,
            "A committed but unacknowledged transaction was retried or reported rolled back");
        require(service.active() == 0, "Unknown COMMIT retained its transaction reservation");
    }
    require(native->close_and_wait(), "Relayed native pool did not reach its explicit test-owner barrier");
}

void check_cleanup_failure(const pool_options& options, mysql_service& observer, const std::string& table) {
    mysql_transaction_relay relay(options.connection.port);
    auto relayed = options;
    relayed.connection.port = relay.port();
    auto native = std::make_shared<pool>(relayed);
    {
        mysql_transaction_service service(native, {1, 8192});
        auto transaction = begin(service, 3s, 200ms);
        const auto inserted = query(service, transaction, "INSERT INTO " + table + " VALUES(6, 'lost cleanup reply')");
        success(inserted);
        relay.drop_responses();
        transaction.abort();
        wait_until([&] {
            service.update();
            return service.active() == 0;
        });
        const auto snapshot = transaction.snapshot();
        require(relay.discarded_bytes() != 0 && !relay.failed(), "Cleanup response loss was not injected");
        require(snapshot.outcome == mysql_transaction_outcome::unconfirmed && snapshot.discard_requested &&
                    snapshot.rollback_code == connection_errc::timeout && snapshot.rollback_outcome_unknown,
            "Lost cleanup reply was reported as confirmed rollback or its native outcome was lost");
        require(inserted.result.affected_rows == 1 && bool(inserted.result),
            "Cleanup failure changed the already-delivered write result");
        require(integer(observe(observer, "SELECT COUNT(*) FROM " + table + " WHERE id=6").result) == 0,
            "Abandoned transaction cleanup left a committed write");
    }
    require(native->close_and_wait(), "Cleanup-failure pool did not complete its test-owner native barrier");
}

void check_cancellation(mysql_transaction_service& service, mysql_service& observer) {
    auto transaction = begin(service);
    const auto tag = "ark_transaction_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::optional<mysql_transaction_reply> response;
    const auto id =
        transaction.start_query("SELECT SLEEP(5) /*" + tag + "*/", [&](auto reply) { response = std::move(reply); });
    require(id != 0, "Cancellation query was not admitted");
    unsigned parallel_rejections{};
    require(transaction.start_query("SELECT 2",
                [&](auto reply) {
                    ++parallel_rejections;
                    require(reply.result.status.code == connection_errc::already_started,
                        "Parallel step lost its rejection reason");
                }) == 0 &&
                parallel_rejections == 1,
        "Parallel transaction commands were admitted");
    wait_until([&] {
        auto active =
            observe(observer, "SELECT COUNT(*) FROM information_schema.processlist WHERE ID <> CONNECTION_ID() "
                              "AND STATE='User sleep' AND INFO LIKE '%" +
                                  tag + "%'");
        return integer(active.result) == 1;
    });
    require(service.retained_input_bytes() > 0, "In-flight SQL was not charged");
    require(transaction.cancel(id), "Observed sent query could not be cancelled");
    require(!transaction.cancel(id), "Repeated cancellation was resubmitted");
    wait_until([&] {
        service.update();
        return bool(response);
    });
    require(response->result.status.code == connection_errc::cancelled && response->result.status.outcome_unknown,
        "Sent cancellation discarded the native uncertain result");
    require(response->transaction_status.outcome == mysql_transaction_outcome::unconfirmed &&
                response->transaction_status.discard_requested,
        "Uncertain cancellation was reported as a known rollback");
    require(!response->rollback, "Cancelled closed connection was used for a fictitious rollback");
    require(service.active() == 0 && service.retained_input_bytes() == 0, "Cancelled operation kept admission quota");
    auto next = begin(service);
    require(!next.cancel(id), "Old logical ID cancelled a later transaction step");
    run(service, [&](auto complete) { return next.start_rollback(std::move(complete)); });
}

module_task<> coroutine_transaction(mysql_transaction_service& service,
    database_awaitable<mysql_transaction_reply>& bridge, std::thread::id owner, bool& completed) {
    auto begun = co_await mysql_transaction_begin(service, bridge, {4s, 1s});
    require(begun.transaction.has_value(), "Coroutine begin did not own a transaction");
    auto transaction = std::move(*begun.transaction);
    auto result = co_await mysql_transaction_query(transaction, bridge, "SELECT 'owning coroutine result'");
    success(result);
    require(std::this_thread::get_id() == owner, "Transaction coroutine resumed on a native IO thread");
    const std::string binary("coroutine\0parameter", 19);
    std::vector<field_value> parameters{binary};
    auto executed = co_await mysql_transaction_execute(transaction, bridge, "SELECT ?", std::move(parameters));
    success(executed);
    require(executed.statement_close && bool(*executed.statement_close), "Coroutine did not close its statement");
    require(std::this_thread::get_id() == owner && std::get<std::string>(executed.result.rows[0][0]) == binary,
        "Prepared coroutine resumed on IO or lost its owning binary result");
    auto committed = co_await mysql_transaction_commit(transaction, bridge);
    require(committed.transaction_status.outcome == mysql_transaction_outcome::committed, "Coroutine commit failed");
    require(std::get<std::string>(result.result.rows[0][0]) == "owning coroutine result",
        "Coroutine result lost ownership");
    auto next = co_await mysql_transaction_begin(service, bridge, {4s, 1s});
    require(next.transaction.has_value(), "Coroutine rollback did not own a transaction");
    auto rollback_transaction = std::move(*next.transaction);
    auto rolled_back = co_await mysql_transaction_rollback(rollback_transaction, bridge);
    require(rolled_back.transaction_status.outcome == mysql_transaction_outcome::rolled_back,
        "Coroutine explicit rollback failed");
    completed = true;
}

void check_coroutine(mysql_transaction_service& service) {
    auto runtime = std::make_shared<arknet::manual_runtime>();
    database_awaitable<mysql_transaction_reply> bridge(runtime);
    module_task_group tasks(runtime);
    bool completed{};
    std::exception_ptr error;
    tasks.start(coroutine_transaction(service, bridge, std::this_thread::get_id(), completed),
        [&](std::exception_ptr failure) { error = std::move(failure); });
    wait_until([&] {
        service.update();
        bridge.dispatch();
        tasks.poll();
        return tasks.pending() == 0;
    });
    tasks.close();
    if (error) {
        std::rethrow_exception(error);
    }
    require(completed && tasks.closed(), "Coroutine group did not finish explicitly");
}

void check_throwing_delivery(mysql_transaction_service& service) {
    unsigned calls{};
    bool observed{};
    require(service.start({3s, 1s},
                [&](mysql_transaction_reply reply) {
                    ++calls;
                    require(reply.transaction.has_value(), "Throwing delivery had no active scope to abandon");
                    throw std::runtime_error("Expected transaction callback failure");
                }) != 0,
        "Throwing BEGIN delivery was not accepted");
    wait_until([&] {
        try {
            service.update(1);
        } catch (const std::runtime_error& error) {
            require(
                std::string(error.what()) == "Expected transaction callback failure", "Unexpected delivery failure");
            observed = true;
        }
        return observed;
    });
    wait_until([&] {
        service.update(1);
        return service.active() == 0;
    });
    require(calls == 1 && service.retained_input_bytes() == 0,
        "Throwing callback was repeated or prevented abandoned-scope cleanup");
}

void check_thread_abandon(mysql_transaction_service& service, mysql_service& observer, const std::string& table) {
    auto transaction = begin(service);
    const auto connection = integer(query(service, transaction, "SELECT CONNECTION_ID()").result);
    success(query(service, transaction, "INSERT INTO " + table + " VALUES(7, 'worker abandonment')"));
    auto moved = std::move(transaction);
    require(transaction.snapshot().outcome == mysql_transaction_outcome::not_started,
        "Move left ownership in the original transaction handle");
    std::thread worker([owned = std::move(moved)]() mutable { owned.abort(); });
    worker.join();
    wait_until([&] {
        service.update();
        return service.active() == 0;
    });
    require(service.retained_input_bytes() == 0, "Worker abandonment retained input quota");
    require(integer(observe(observer, "SELECT COUNT(*) FROM " + table + " WHERE id=7").result) == 0,
        "Worker abandonment committed a write");
    auto next = begin(service);
    require(integer(query(service, next, "SELECT CONNECTION_ID()").result) == connection,
        "Worker abandonment discarded a usable connection instead of completing rollback");
    success(run(service, [&](auto complete) { return next.start_rollback(std::move(complete)); }));
}

void check_natural_drain(const std::shared_ptr<pool>& native) {
    {
        mysql_transaction_service service(native, {1, 8192});
        require(service.quiescent() && !service.logically_drained(), "Open transaction admission reported drain");
        auto transaction = begin(service);
        service.stop_accepting();
        require(!service.quiescent() && !service.logically_drained(), "Idle transaction was omitted from drain");
        unsigned refused{};
        require(service.start({1s, 1s},
                    [&](auto reply) {
                        ++refused;
                        require(reply.result.status.code == connection_errc::not_connected,
                            "Sealed BEGIN used wrong refusal");
                        require(!service.logically_drained(), "Inline refusal escaped transaction activity");
                    }) == 0,
            "Sealed transaction service admitted BEGIN");
        require(refused == 1, "Sealed transaction BEGIN did not complete inline");
        success(query(service, transaction, "SELECT 42"));
        auto committed = run(service, [&](auto complete) {
            return transaction.start_commit([&, complete = std::move(complete)](auto reply) mutable {
                require(!service.logically_drained(), "Commit callback reported premature transaction drain");
                complete(std::move(reply));
            });
        });
        success(committed);
        require(committed.transaction_status.outcome == mysql_transaction_outcome::committed,
            "Accepted transaction could not commit after BEGIN admission stopped");
        require(service.logically_drained(), "Completed transaction did not naturally drain");
    }
    {
        // A new borrower proves the first borrower's natural drain/destruction
        // left the same real pool and its native connection usable.
        mysql_transaction_service service(native, {1, 8192});
        auto transaction = begin(service, 1s);
        success(query(service, transaction, "SELECT 43"));
        service.stop_accepting();
        require(!service.logically_drained(), "Idle deadline cleanup was skipped");
        wait_until([&] {
            service.update();
            return service.logically_drained();
        });
        require(transaction.snapshot().outcome == mysql_transaction_outcome::rolled_back,
            "Idle transaction did not complete rollback before natural drain");
    }
    {
        mysql_transaction_service service(native, {1, 8192});
        auto transaction = begin(service);
        success(query(service, transaction, "SELECT 44"));
        success(run(service, [&](auto complete) { return transaction.start_rollback(std::move(complete)); }));
        service.stop_accepting();
        require(service.logically_drained(), "Reused borrowed pool did not naturally drain");
    }
}

} // namespace

int main() {
    try {
        const auto options = test_options();
        auto native = std::make_shared<pool>(options);
        mysql_service observer(options);
        const auto table =
            "ark_transaction_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        observe(observer, "CREATE TABLE " + table + " (id INT PRIMARY KEY, value VARBINARY(64)) ENGINE=InnoDB");
        try {
            {
                mysql_transaction_service service(native, {1, 8192});
                check_commit(service, observer, table);
                check_abort(service, observer, table);
                check_prepared(service, observer, table);
                check_prepared_deadline(service, observer, table);
                check_prepared_cancellation(service, observer);
                check_bounds(service, observer, table);
                check_cancellation(service, observer);
                check_coroutine(service);
                check_throwing_delivery(service);
                check_thread_abandon(service, observer, table);
                service.start({2s, 1s}, {});
                wait_until([&] {
                    service.update();
                    return service.active() == 0;
                });
                require(service.retained_input_bytes() == 0, "Abandoned BEGIN delivery leaked input quota");
            }
            check_natural_drain(native);
            check_waiter(native, observer);
            check_prepare_delivery_cancellation(native, observer, table);
            check_unknown_commit(options, observer, table);
            check_cleanup_failure(options, observer, table);
            check_prepare_cancellation(options, observer, table);
        } catch (...) {
            observe(observer, "DROP TABLE IF EXISTS " + table);
            native->close_and_wait();
            throw;
        }
        observe(observer, "DROP TABLE " + table);
        require(native->close_and_wait(), "Native pool did not complete its explicit test-owner shutdown barrier");
        std::cout << "Private MySQL transaction checks passed: " << checks << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
