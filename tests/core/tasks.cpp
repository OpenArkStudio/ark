// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <chrono>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>

#include "../network/doctest.h"

#include <ark/task.hpp>
#include "detail/module_task_group.hpp"

static_assert(std::is_same_v<ark::task<int>, ark::detail::module_task<int>>);
static_assert(std::is_same_v<ark::task_group, ark::detail::module_task_group>);
static_assert(std::is_same_v<ark::task<int>, asio::awaitable<int>>);
static_assert(!std::is_copy_constructible_v<ark::task_group>);
static_assert(!std::is_move_constructible_v<ark::task_group>);

namespace {

ark::task<std::string> owning_value() {
    co_await asio::post(asio::use_awaitable);
    co_return std::string("task\0value", 10);
}

ark::task<> receive_value(std::string& result, unsigned& resumed) {
    result = co_await owning_value();
    co_await asio::post(asio::use_awaitable);
    ++resumed;
}

ark::task<> wait_for_cancel(asio::steady_timer& timer, unsigned& cancelled) {
    try {
        co_await timer.async_wait(asio::use_awaitable);
    } catch (const asio::system_error& error) {
        if (error.code() != asio::error::operation_aborted) {
            throw;
        }
        ++cancelled;
    }
}

} // namespace

TEST_CASE("Public task SDK returns owning values after admission stops") {
    auto runtime = std::make_shared<arknet::manual_runtime>();
    ark::task_group tasks(runtime, 1);
    std::string result;
    unsigned resumed{};
    unsigned completed{};
    const auto owner = std::this_thread::get_id();
    REQUIRE(tasks.start(receive_value(result, resumed), [&](std::exception_ptr error) {
        CHECK(std::this_thread::get_id() == owner);
        CHECK_FALSE(error);
        CHECK(tasks.pending() == 0);
        ++completed;
    }) != 0);
    tasks.stop_accepting();
    CHECK_FALSE(tasks.closed());
    CHECK(tasks.poll(0) == 0);
    CHECK(result.empty());
    for (unsigned step = 0; step < 32 && !tasks.closed(); ++step) {
        tasks.poll(1);
    }
    CHECK(tasks.closed());
    CHECK(completed == 1);
    CHECK(resumed == 1);
    CHECK(result == std::string("task\0value", 10));
    tasks.close();
}

TEST_CASE("Public task SDK cancellation remains explicit and owner polled") {
    auto runtime = std::make_shared<arknet::manual_runtime>();
    ark::task_group tasks(runtime, 2);
    asio::steady_timer first(runtime->get_executor(), std::chrono::hours(1));
    asio::steady_timer second(runtime->get_executor(), std::chrono::hours(1));
    unsigned cancelled{};
    unsigned completed{};
    auto completion = [&](std::exception_ptr error) {
        CHECK_FALSE(error);
        ++completed;
    };
    const auto id = tasks.start(wait_for_cancel(first, cancelled), completion);
    REQUIRE(id != 0);
    REQUIRE(tasks.start(wait_for_cancel(second, cancelled), completion) != 0);
    tasks.poll();
    CHECK(tasks.pending() == 2);
    CHECK_FALSE(tasks.cancel(0));
    CHECK(tasks.cancel(id));
    CHECK(tasks.cancel(id));
    tasks.cancel_all();
    tasks.stop_accepting();
    CHECK(completed == 0);
    CHECK_FALSE(tasks.closed());
    for (unsigned step = 0; step < 32 && !tasks.closed(); ++step) {
        tasks.poll(1);
    }
    CHECK(tasks.closed());
    CHECK(completed == 2);
    CHECK(cancelled == 2);
    CHECK_FALSE(tasks.cancel(id));
}

TEST_CASE("Public task SDK rejects invalid owners and construction limits") {
    CHECK_THROWS_AS(ark::task_group(nullptr), std::invalid_argument);
    auto runtime = std::make_shared<arknet::manual_runtime>();
    CHECK_THROWS_AS(ark::task_group(runtime, 0), std::invalid_argument);
    ark::task_group tasks(runtime);
    unsigned rejected{};
    std::thread worker([&] {
        try {
            tasks.poll();
        } catch (const std::logic_error&) {
            ++rejected;
        }
        try {
            tasks.cancel_all();
        } catch (const std::logic_error&) {
            ++rejected;
        }
    });
    worker.join();
    CHECK(rejected == 2);
    tasks.stop_accepting();
    CHECK(tasks.closed());
}
