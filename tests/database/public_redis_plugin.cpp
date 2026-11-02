// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <chrono>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <thread>

#include "ark/plugin_manager.hpp"
#include "ark/redis/redis_module.hpp"

namespace {
ark::task<> request(ark::redis_module& database) {
    auto missing = co_await database.command("missing", {"PING"});
    if (missing.error != arknet::redis::client_errc::not_connected)
    {
        throw std::runtime_error("Expected missing connection result across the plugin boundary");
    }
    auto result = co_await database.command("test", {"PING"}, std::chrono::milliseconds(10));
    if (result)
    {
        throw std::runtime_error("Expected unavailable test endpoint");
    }
}
} // namespace

int main() {
    ark::logger::init(nullptr);
    ark::logger::instance()->set_log_func([](spdlog::level::level_enum, const std::string&) {});
    ark::plugin_manager manager;
    if (!manager.start(0, ARK_DATABASE_PLUGIN_DIR, "", {{"ark_database_redis_plugin", ""}}))
    {
        return 1;
    }
    auto* database = manager.find_module<ark::redis_module>();
    if (!database)
    {
        return 2;
    }
    ark::redis_options options;
    options.pool.connection.host = "127.0.0.1";
    options.pool.connection.port = "1";
    if (!database->configure("test", std::move(options)))
    {
        return 3;
    }
    std::exception_ptr failure;
    bool completed{};
    if (!manager.create_task_group(*database).start(request(*database), [&](auto error) {
            failure = error;
            completed = true;
        }))
    {
        return 4;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    // Complete the public coroutine while running. Process shutdown cancels
    // residual tasks; it no longer completes business requests on their behalf.
    while (!completed && std::chrono::steady_clock::now() < deadline)
    {
        manager.update(0);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!completed)
    {
        return 8;
    }
    while (!manager.stop(0) && std::chrono::steady_clock::now() < deadline)
    {
        if (manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::failed)
        {
            return 5;
        }
        manager.update(0);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (manager.get_shutdown_state() != ark::plugin_manager::shutdown_state::stopped || failure)
    {
        std::cerr << "Public Redis DSO shutdown state=" << static_cast<int>(manager.get_shutdown_state())
                  << ", pending tasks=" << manager.pending_tasks() << '\n';
        if (failure)
        {
            try
            { std::rethrow_exception(failure); }
            catch (const std::exception& error)
            { std::cerr << "Public Redis DSO task failed: " << error.what() << '\n'; }
            catch (...)
            { std::cerr << "Public Redis DSO task failed with an unknown exception\n"; }
        }
        return 6;
    }
    if (manager.pending_tasks())
    {
        return 7;
    }
    std::cout << "Public Redis DSO registration, coroutine delivery and shutdown passed\n";
}
