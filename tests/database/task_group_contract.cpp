// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <cstdlib>
#include <exception>
#include <memory>
#include <string_view>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <ark/task.hpp>

namespace {

[[noreturn]] void exit_on_contract_violation() noexcept {
#if defined(_WIN32)
    // This child intentionally terminates. Avoid CRT error dialogs and DLL
    // teardown while its other thread may be waiting for this one to finish.
    ::TerminateProcess(::GetCurrentProcess(), 77);
    std::_Exit(78);
#else
    std::_Exit(77);
#endif
}

void install_contract_exit() noexcept {
    std::set_terminate(exit_on_contract_violation);
}

ark::task<> pending_task() {
    co_return;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        return 2;
    }
    install_contract_exit();
    const std::string_view mode(argv[1]);
    if (mode == "--pending-destruction") {
        auto runtime = std::make_shared<arknet::manual_runtime>();
        ark::task_group group(runtime);
        group.start(pending_task());
        // Even a task which could finish immediately is pending before poll.
        // Destroying this owner violates the documented drain precondition.
        return 0;
    }
    if (mode == "--wrong-thread-destruction") {
        auto runtime = std::make_shared<arknet::manual_runtime>();
        auto group = std::make_unique<ark::task_group>(runtime);
        std::thread worker([owned = std::move(group)]() mutable {
            // MSVC terminate handlers are per-thread.
            install_contract_exit();
            owned.reset();
        });
        worker.join();
        return 0;
    }
    return 2;
}
