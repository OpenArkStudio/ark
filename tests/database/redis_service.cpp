// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

#include "redis/detail/redis_service.hpp"

using namespace std::chrono_literals;

namespace {

using redis_service = ark::detail::redis_service;
using service_result = ark::detail::redis_service_result;

void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

arknet::redis::result await_result(redis_service& service, std::optional<service_result>& completion) {
    const auto expires = std::chrono::steady_clock::now() + 5s;
    while (!completion && std::chrono::steady_clock::now() < expires) {
        service.update();
        std::this_thread::sleep_for(1ms);
    }
    check(bool(completion), "Cancelled Redis operation did not deliver a terminal continuation");
    if (auto* failure = std::get_if<std::exception_ptr>(&*completion)) {
        std::rethrow_exception(*failure);
    }
    auto result = std::get<arknet::redis::result>(std::move(*completion));
    completion.reset();
    return result;
}

arknet::redis::result command(redis_service& service, arknet::redis::command arguments) {
    std::optional<service_result> completion;
    check(service.start(
              std::move(arguments), [&](auto result) { completion = std::move(result); }, 4s) != 0,
        "Observer command was not admitted");
    auto result = await_result(service, completion);
    check(bool(result), "Observer Redis command failed");
    return result;
}

void wait_for_block(redis_service& observer, std::int64_t client_id) {
    const auto expires = std::chrono::steady_clock::now() + 4s;
    const auto marker = "id=" + std::to_string(client_id) + " ";
    while (std::chrono::steady_clock::now() < expires) {
        const auto clients = command(observer, {"CLIENT", "LIST"});
        check(clients.replies.size() == 1, "Redis CLIENT LIST response shape changed");
        const auto& listing = clients.replies.front().text;
        const auto start = listing.find(marker);
        if (start != std::string::npos) {
            const auto end = listing.find('\n', start);
            const auto blocked = listing.find("cmd=blpop", start);
            if (blocked != std::string::npos && (end == std::string::npos || blocked < end)) {
                return;
            }
        }
        std::this_thread::sleep_for(1ms);
    }
    throw std::runtime_error("Redis did not observe the identified sent BLPOP operation");
}

void check_cancellation(const arknet::redis::pool_options& options, const std::string& prefix) {
    using arknet::redis::client_errc;
    ark::detail::database_request_control control;
    const auto submission = control.begin(ark::detail::database_request_control::stage::command);
    arknet::redis::pool::lease empty;
    bool inline_completion{};
    const auto native_id = empty.async_execute({"PING"}, [&](auto result) {
        check(result.error == client_errc::closed, "Empty Redis lease did not reject inline");
        check(control.complete(submission) && control.finish(), "Inline rejection lost its terminal phase");
        inline_completion = true;
    });
    check(inline_completion && native_id == 0 && !control.publish(submission, native_id),
        "Native inline ID0 rejection deadlocked or published a stale ID");

    redis_service service(options, 4, 8192);
    redis_service observer(options, 4, 8192);
    const auto blocking_key = prefix + "-blocking";
    const auto cancelled_write = prefix + "-unsent";
    command(observer, {"DEL", blocking_key, cancelled_write});
    const auto client_id = command(service, {"CLIENT", "ID"}).replies.front().integer;
    std::optional<service_result> active, waiting;
    const auto active_id =
        service.start({"BLPOP", blocking_key, "2"}, [&](auto result) { active = std::move(result); }, 4s);
    check(active_id != 0, "Active Redis cancellation request was not admitted");
    wait_for_block(observer, client_id);
    const auto waiting_id =
        service.start({"SET", cancelled_write, "must-not-run"}, [&](auto result) { waiting = std::move(result); }, 4s);
    check(waiting_id && waiting_id != active_id && service.cancel(waiting_id),
        "Redis waiting acquire did not retain a separate logical cancellation ID");
    auto unsent = await_result(service, waiting);
    check(unsent.error == client_errc::cancelled && !unsent.command_may_have_run,
        "Cancelled waiting lease lost known-unsent status");
    check(service.cancel(active_id), "Sent Redis operation was not cancellable");
    auto sent = await_result(service, active);
    check(sent.error == client_errc::cancelled && sent.command_may_have_run,
        "Sent Redis cancellation lost execution uncertainty");
    // Cancellation stops this observer, not the server command. Wake the same
    // blocked BLPOP and confirm it can still consume after cancellation.
    command(observer, {"LPUSH", blocking_key, "after-cancel"});
    const auto consumed_deadline = std::chrono::steady_clock::now() + 4s;
    bool consumed{};
    while (std::chrono::steady_clock::now() < consumed_deadline) {
        consumed = command(observer, {"LLEN", blocking_key}).replies.front().integer == 0;
        if (consumed) {
            break;
        }
    }
    check(consumed, "Cancelled sent BLPOP was incorrectly treated as guaranteed not executed");
    check(command(observer, {"EXISTS", cancelled_write}).replies.front().integer == 0,
        "Cancelled waiting Redis write reached the server");

    redis_service ready(options, 1, 8192);
    std::optional<service_result> ready_result;
    const auto ready_id =
        ready.start({"ECHO", "native-success"}, [&](auto result) { ready_result = std::move(result); }, 4s);
    check(ready_id != 0, "Ready-result request was not admitted");
    const auto ready_deadline = std::chrono::steady_clock::now() + 5s;
    while (ready.is_pending(ready_id) && std::chrono::steady_clock::now() < ready_deadline) {
        std::this_thread::sleep_for(1ms);
    }
    check(!ready.is_pending(ready_id) && !ready_result && !ready.cancel(ready_id),
        "Cancellation rewrote an already queued Redis terminal result");
    unsigned rejected{};
    check(ready.start(
              {"PING"},
              [&](auto result) {
                  ++rejected;
                  check(std::get<arknet::redis::result>(result).error == client_errc::queue_full,
                      "Undispatched Redis result did not retain quota");
              },
              4s) == 0 &&
              rejected == 1,
        "New-style quota rejection did not complete inline with ID0");
    auto unchanged = await_result(ready, ready_result);
    check(bool(unchanged) && unchanged.replies.front().text == "native-success",
        "Queued native Redis success changed after cancellation");

    const auto stopping_key = prefix + "-stop";
    command(observer, {"DEL", stopping_key});
    const auto stopping_client = command(service, {"CLIENT", "ID"}).replies.front().integer;
    std::optional<service_result> stopping_active, stopping_waiter;
    const auto stopping_id =
        service.start({"BLPOP", stopping_key, "2"}, [&](auto result) { stopping_active = std::move(result); }, 4s);
    check(stopping_id != 0, "Quiesce Redis command was not admitted");
    wait_for_block(observer, stopping_client);
    const auto stopping_waiter_id = service.start(
        {"SET", cancelled_write, "must-not-run"}, [&](auto result) { stopping_waiter = std::move(result); }, 4s);
    check(stopping_waiter_id != 0, "Quiesce Redis waiter was not admitted");
    service.stop_accepting();
    check(service.start(
              {"PING"},
              [&](auto result) {
                  ++rejected;
                  check(std::get<arknet::redis::result>(result).error == client_errc::closed,
                      "Stopped Redis admission lost native rejection");
              },
              4s) == 0 &&
              rejected == 2,
        "Stopped Redis start did not complete inline with ID0");
    service.cancel_all();
    auto stopped_active = await_result(service, stopping_active);
    auto stopped_waiter = await_result(service, stopping_waiter);
    check(stopped_active.error == client_errc::cancelled && stopped_active.command_may_have_run &&
              stopped_waiter.error == client_errc::cancelled && !stopped_waiter.command_may_have_run,
        "Redis quiesce suppressed continuation or confused sent and waiting cancellation");
    check(!service.is_pending(stopping_id) && !service.is_pending(stopping_waiter_id) && service.update() == 0,
        "Terminal Redis request remained registered or completed twice");
    command(observer, {"LPUSH", stopping_key, "unblock"});
    command(observer, {"DEL", blocking_key, stopping_key, cancelled_write});
    const auto diagnostics = service.diagnostics();
    check(!diagnostics.cancellation_submission && !diagnostics.lease_return,
        "Redis cancellation silently retained a native submission or lease-return exception");
}

} // namespace

int main() {
    try {
        const auto* host = std::getenv("ARK_DATABASE_REDIS_HOST");
        const auto* port = std::getenv("ARK_DATABASE_REDIS_PORT");
        if (!host || !*host || !port || !*port) {
            throw std::invalid_argument(
                "Set ARK_DATABASE_REDIS_HOST and ARK_DATABASE_REDIS_PORT for an isolated test server");
        }
        arknet::redis::pool_options options;
        options.connection.host = host;
        options.connection.port = port;
        if (const auto* username = std::getenv("ARK_DATABASE_REDIS_USER")) {
            options.connection.username = username;
        }
        if (const auto* password = std::getenv("ARK_DATABASE_REDIS_PASSWORD")) {
            options.connection.password = password;
        }
        options.connection.client_name = "ark-database-service-test";
        options.max_connections = 1;
        options.acquire_timeout = 2s;
        options.client_configuration.command_timeout = 2s;
        options.client_configuration.response_limits.max_bulk_bytes = 1024;
        ark::detail::redis_service service(options, 1, 8192);
        std::optional<ark::detail::redis_service_result> completed;
        unsigned callbacks{};
        const auto owner = std::this_thread::get_id();
        auto handler = [&](ark::detail::redis_service_result result) {
            if (std::this_thread::get_id() != owner) {
                throw std::runtime_error("Business callback escaped the module thread");
            }
            ++callbacks;
            completed = std::move(result);
        };
        const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto key = "ark-private-service-" + std::to_string(unique);
        const std::string expected("binary\0value", 12);
        std::string input = expected;
        if (!service.submit_pipeline({{"SET", key, input}, {"GET", key}}, handler, 2s)) {
            throw std::runtime_error("First operation was not admitted");
        }
        input.assign("modified caller storage");
        if (service.submit({"PING"}, handler, 2s)) {
            throw std::runtime_error("Completed/active operations bypassed module admission limit");
        }
        auto await = [&] {
            const auto expires = std::chrono::steady_clock::now() + 4s;
            while (!completed && std::chrono::steady_clock::now() < expires) {
                service.update();
                std::this_thread::sleep_for(1ms);
            }
            if (!completed) {
                throw std::runtime_error("No module completion before timeout");
            }
            if (auto failure = std::get_if<std::exception_ptr>(&*completed)) {
                std::rethrow_exception(*failure);
            }
            auto result = std::get<arknet::redis::result>(std::move(*completed));
            completed.reset();
            if (!result) {
                throw std::runtime_error("Native Redis failed: " + std::to_string(static_cast<int>(result.error)));
            }
            return result;
        };
        auto owned = await();
        if (owned.replies.size() != 2 || owned.replies[1].type != arknet::redis::value_type::bulk_string ||
            owned.replies[1].text != expected) {
            throw std::runtime_error("Pipeline/binary result did not preserve ownership");
        }
        if (!service.submit({"DEL", key}, handler, 2s)) {
            throw std::runtime_error("Module admission did not recover after dispatch");
        }
        auto removed = await();
        if (removed.replies.size() != 1 || removed.replies[0].integer != 1 || owned.replies[1].text != expected) {
            throw std::runtime_error("A later command invalidated an owning result");
        }
        if (!service.submit({"PING"}, handler, 2s)) {
            throw std::runtime_error("Stop test was not admitted");
        }
        service.stop();
        if (service.update() != 0 || callbacks != 2 || service.submit({"PING"}, handler, 2s)) {
            throw std::runtime_error("Stop delivered business code or accepted new work");
        }
        auto ephemeral = std::make_unique<ark::detail::redis_service>(options);
        bool destroyed_from_callback{};
        if (!ephemeral->submit(
                {"PING"},
                [&](ark::detail::redis_service_result result) {
                    if (auto failure = std::get_if<std::exception_ptr>(&result)) {
                        std::rethrow_exception(*failure);
                    }
                    if (!std::get<arknet::redis::result>(result)) {
                        throw std::runtime_error("Callback-destruction PING failed");
                    }
                    ephemeral.reset();
                    destroyed_from_callback = true;
                },
                2s)) {
            throw std::runtime_error("Callback-destruction operation was not admitted");
        }
        const auto destruction_deadline = std::chrono::steady_clock::now() + 4s;
        while (!destroyed_from_callback && std::chrono::steady_clock::now() < destruction_deadline) {
            ephemeral->update();
            std::this_thread::sleep_for(1ms);
        }
        if (!destroyed_from_callback) {
            throw std::runtime_error("Callback-destruction completion timed out");
        }
        check_cancellation(options, key);
        std::cout
            << "private Redis service: owning binary pipeline, admission, module-thread dispatch and stop passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
