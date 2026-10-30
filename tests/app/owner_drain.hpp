// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string_view>
#include <thread>

#include <ark/plugin_manager.hpp>

namespace ark::test {

inline const char* shutdown_state_name(plugin_manager::shutdown_state state) {
    switch (state)
    {
    case plugin_manager::shutdown_state::running:
        return "running";
    case plugin_manager::shutdown_state::stopping_acceptance:
        return "stopping_acceptance";
    case plugin_manager::shutdown_state::draining:
        return "draining";
    case plugin_manager::shutdown_state::failed:
        return "failed";
    case plugin_manager::shutdown_state::stopped:
        return "stopped";
    case plugin_manager::shutdown_state::timed_out:
        return "timed_out";
    case plugin_manager::shutdown_state::cleanup_timed_out:
        return "cleanup_timed_out";
    }
    return "unknown";
}

[[noreturn]] inline void fail_owner_drain(plugin_manager& manager, std::string_view context, const char* reason) {
    std::cerr << context << ": " << reason << "; state=" << shutdown_state_name(manager.get_shutdown_state())
              << "; phase=" << (manager.get_shutdown_failure().empty() ? "<none>" : manager.get_shutdown_failure())
              << std::endl;
    // Failure retains the fixture's modules/DSOs. Do not unwind through owners
    // still in use or turn a test observation timeout into successful cleanup.
    std::_Exit(1);
}

template<typename next_timestamp>
std::size_t drain_owner(plugin_manager& manager, next_timestamp&& now, std::chrono::steady_clock::time_point deadline,
    std::string_view context) {
    std::size_t deferred_frames{};
    try
    {
        while (!manager.stop_until(now(), deadline))
        {
            const auto state = manager.get_shutdown_state();
            if ((state != plugin_manager::shutdown_state::draining &&
                    state != plugin_manager::shutdown_state::stopping_acceptance) ||
                !manager.get_shutdown_failure().empty() || manager.get_shutdown_exception())
            {
                fail_owner_drain(manager, context, "shutdown failed");
            }
            if (std::chrono::steady_clock::now() >= deadline)
            {
                fail_owner_drain(manager, context, "owner drain deadline reached");
            }
            if (deferred_frames++ == 0)
            {
                std::cout << context << ": deferred shutdown; state=" << shutdown_state_name(state)
                          << "; phase=<none>; owner updates continue" << std::endl;
            }
            if (!manager.update(now()))
            {
                fail_owner_drain(manager, context, "owner update failed during drain");
            }
            std::this_thread::yield();
        }
    }
    catch (const std::exception& error)
    {
        std::cerr << context << ": " << error.what() << std::endl;
        fail_owner_drain(manager, context, "owner drain threw");
    }
    catch (...)
    { fail_owner_drain(manager, context, "owner drain threw a nonstandard exception"); }
    if (manager.get_shutdown_state() != plugin_manager::shutdown_state::stopped ||
        !manager.get_shutdown_failure().empty() || manager.get_shutdown_exception())
    {
        fail_owner_drain(manager, context, "stop returned without normal completion");
    }
    std::cout << context << ": state=stopped; deferred owner frames=" << deferred_frames << std::endl;
    return deferred_frames;
}

} // namespace ark::test
