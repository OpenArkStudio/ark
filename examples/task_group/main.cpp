// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <utility>

#include <ark/task.hpp>

namespace {

ark::task<int> next_value(int value) {
    co_await asio::post(asio::use_awaitable);
    co_return value + 1;
}

ark::task<> compute(int& result) {
    result = co_await next_value(41);
}

} // namespace

int main() {
    auto runtime = std::make_shared<arknet::manual_runtime>();
    ark::task_group tasks(runtime, 1);
    int result{};
    bool completed{};
    std::exception_ptr failure;
    const auto id = tasks.start(compute(result), [&](std::exception_ptr error) {
        failure = std::move(error);
        completed = true;
    });
    if (id == 0) {
        return 1;
    }
    tasks.stop_accepting();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!tasks.closed() && std::chrono::steady_clock::now() < deadline) {
        tasks.poll(8);
    }
    if (!tasks.closed()) {
        // A sample failure guard, not an application drain/abandon policy.
        // Do not destroy an owner which still has suspended task frames.
        std::_Exit(2);
    }
    if (failure || !completed || result != 42) {
        return 1;
    }
    std::cout << "result=" << result << " completed=" << completed << '\n';
    return 0;
}
