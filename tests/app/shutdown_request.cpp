// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <atomic>
#include <chrono>
#include <csignal>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

#include "shutdown_request.hpp"

namespace {

int checks{};

void check(bool condition, const char* message) {
    ++checks;
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void install_signal_handler(int signal_number) {
    check(std::signal(signal_number, ark::app_detail::ark_app_stop_signal_handler) != SIG_ERR,
        "signal handler installation failed");
}

void signal_before_logger(int signal_number) {
    // This process has no logger. The actual application handler must only publish the reason.
    check(ark::app_detail::requested_stop_reason() == 0, "fresh process already has a stop request");
    install_signal_handler(signal_number);
    check(std::raise(signal_number) == 0, "raising the stop signal failed");
    check(ark::app_detail::requested_stop_reason() == signal_number, "signal reason was not published");
#ifndef _WIN32
    // POSIX keeps the installed handler; the Windows CRT may reset it after one signal.
    check(std::raise(signal_number) == 0, "raising the repeated stop signal failed");
    check(ark::app_detail::requested_stop_reason() == signal_number, "repeated signal changed the first reason");
    const auto other_signal = signal_number == SIGINT ? SIGTERM : SIGINT;
    install_signal_handler(other_signal);
    check(std::raise(other_signal) == 0, "raising the other stop signal failed");
    check(ark::app_detail::requested_stop_reason() == signal_number, "other signal replaced the first reason");
#endif
    ark::app_detail::request_stop(ark::app_detail::command_stop_reason);
    check(ark::app_detail::requested_stop_reason() == signal_number, "later command replaced the signal reason");
}

void concurrent_requests() {
    check(ark::app_detail::requested_stop_reason() == 0, "fresh process already has a stop request");
    install_signal_handler(SIGTERM);
    check(std::raise(SIGTERM) == 0, "raising the first stop signal failed");
    const auto owner = std::this_thread::get_id();
    std::atomic<bool> start{false};
    std::vector<std::thread> producers;
    for (int i = 0; i < 4; ++i) {
        producers.emplace_back([&start, i] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (int request = 0; request < 256; ++request) {
                ark::app_detail::request_stop(i % 2 == 0 ? SIGINT : ark::app_detail::command_stop_reason);
            }
        });
    }
    start.store(true, std::memory_order_release);
    int owner_stop_calls = 0;
    int observed_reason = 0;
    std::thread::id observer;
    // Match main's single observation followed by leaving the update loop.
    for (;;) {
        const auto reason = ark::app_detail::requested_stop_reason();
        if (reason != 0) {
            observer = std::this_thread::get_id();
            observed_reason = reason;
            ++owner_stop_calls;
            break;
        }
    }
    for (auto& producer : producers) {
        producer.join();
    }
    check(observer == owner, "stop work left the owner thread");
    check(observed_reason == SIGTERM, "concurrent request replaced the first reason");
    check(owner_stop_calls == 1, "owner dispatched stop more than once");
    check(ark::app_detail::requested_stop_reason() == SIGTERM, "final stop reason changed");
}

void worker_signal() {
    check(ark::app_detail::requested_stop_reason() == 0, "fresh process already has a stop request");
    install_signal_handler(SIGINT);
    std::atomic<int> raise_result{-1};
    std::thread producer([&raise_result] { raise_result.store(std::raise(SIGINT), std::memory_order_relaxed); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (ark::app_detail::requested_stop_reason() == 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    producer.join();
    check(raise_result.load(std::memory_order_relaxed) == 0, "worker could not raise its signal");
    check(ark::app_detail::requested_stop_reason() == SIGINT, "worker signal was not visible to the owner");
}

void command_request() {
    check(ark::app_detail::requested_stop_reason() == 0, "fresh process already has a stop request");
    std::thread producer([] { ark::app_detail::request_stop(ark::app_detail::command_stop_reason); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (ark::app_detail::requested_stop_reason() == 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    producer.join();
    check(ark::app_detail::requested_stop_reason() == ark::app_detail::command_stop_reason,
        "command request was not visible to the owner");
    ark::app_detail::request_stop(SIGTERM);
    check(ark::app_detail::requested_stop_reason() == ark::app_detail::command_stop_reason,
        "later signal replaced the command reason");
}

} // namespace

int main(int argc, char** argv) {
    try {
        check(argc == 2, "exactly one process mode is required");
        const std::string_view mode = argv[1];
        if (mode == "signal-int") {
            signal_before_logger(SIGINT);
        } else if (mode == "signal-term") {
            signal_before_logger(SIGTERM);
        } else if (mode == "concurrent") {
            concurrent_requests();
        } else if (mode == "worker-signal") {
            worker_signal();
        } else if (mode == "command") {
            command_request();
        } else {
            throw std::runtime_error("unknown process mode");
        }
        std::cout << mode << ": " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& failure) {
        std::cerr << "Application stop-request check failed: " << failure.what() << '\n';
        return 1;
    }
}
