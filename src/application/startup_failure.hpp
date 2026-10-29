// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <string_view>

#include <ark/platform.hpp>
#include <ark/plugin_manager.hpp>

#ifdef ARK_PLATFORM_WIN
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace ark::app_detail {

inline void write_native_stderr(std::string_view line) noexcept {
    std::size_t written{};
    while (written < line.size())
    {
#ifdef ARK_PLATFORM_WIN
        DWORD count{};
        if (!WriteFile(GetStdHandle(STD_ERROR_HANDLE), line.data() + written, static_cast<DWORD>(line.size() - written),
                &count, nullptr) ||
            count == 0)
        {
            break;
        }
#else
        const auto count = ::write(STDERR_FILENO, line.data() + written, line.size() - written);
        if (count < 0 && errno == EINTR)
        {
            continue;
        }
        if (count <= 0)
        {
            break;
        }
#endif
        written += static_cast<std::size_t>(count);
    }
}

// Bounded stack storage and native writes remain usable under persistent C++
// allocation failure. Truncation keeps the phase/owner before exception detail.
inline void write_startup_failure(
    std::string_view phase, std::string_view detail, std::string_view owner = {}) noexcept {
    char line[1024];
    std::size_t size{};
    const auto append = [&](std::string_view value) {
        for (const char character : value)
        {
            if (size == sizeof(line) - 1)
            {
                break;
            }
            line[size++] = character;
        }
    };
    append("Application startup failed");
    if (!phase.empty())
    {
        append(" at ");
        append(phase);
    }
    if (!owner.empty())
    {
        append(": ");
        append(owner);
    }
    append(": ");
    append(detail);
    line[size++] = '\n';
    write_native_stderr(std::string_view(line, size));
}

[[noreturn]] inline void terminate_startup_failure() noexcept {
#ifdef ARK_PLATFORM_WIN
    // No DLL_PROCESS_DETACH/static destruction on partial startup.
    TerminateProcess(GetCurrentProcess(), 1);
#endif
    std::_Exit(1);
}

[[noreturn]] inline void exit_startup_failure(plugin_manager& manager, const char* detail) noexcept {
    // The manager already exists before start; no allocation, logger callback or
    // virtual name lookup is needed to preserve the primary failure diagnostic.
    const auto& call = manager.get_startup_call();
    if (call.result == startup_call_result::exception && !call.diagnostic_ready)
    {
        write_startup_failure(call.phase.data(), detail, call.owner.data());
    }
    else
    {
        write_startup_failure(manager.get_startup_failure(), detail);
    }
    switch (manager.cleanup_failed_startup())
    {
    case startup_cleanup_result::done:
        write_native_stderr("Startup failure cleanup: done\n");
        break;
    case startup_cleanup_result::unsafe:
        write_native_stderr("Startup failure cleanup: unsafe; owners retained\n");
        break;
    case startup_cleanup_result::failed:
        write_native_stderr("Startup failure cleanup: failed; owners retained\n");
        break;
    case startup_cleanup_result::not_run:
        break;
    }
    terminate_startup_failure();
}

} // namespace ark::app_detail
