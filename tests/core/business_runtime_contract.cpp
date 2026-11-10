// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <cstdlib>
#include <exception>
#include <functional>
#include <memory>
#include <string>
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

#include <ark/plugin_manager.hpp>

namespace {

[[noreturn]] void contract_exit() noexcept {
#if defined(_WIN32)
    ::TerminateProcess(::GetCurrentProcess(), 77);
    std::_Exit(78);
#else
    std::_Exit(77);
#endif
}

class owner_module final : public ark::module {
public:
    const std::string& get_name() const override {
        return name_;
    }

    bool update() override {
        on_update();
        return true;
    }

    std::function<void()> on_update;

private:
    const std::string name_{"contract-owner"};
};

ark::task<> ready_task() {
    co_return;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        return 2;
    }
    std::set_terminate(contract_exit);
    const std::string_view mode(argv[1]);
    auto manager = std::make_unique<ark::plugin_manager>();
    owner_module module;
    if (mode == "--pending-destruction") {
        manager->add_module(module.get_name(), &module);
        manager->create_task_group(module).start(ready_task());
        manager.reset();
        return 3;
    }
    if (mode == "--wrong-thread-destruction") {
        std::thread worker([owned = std::move(manager)]() mutable {
            std::set_terminate(contract_exit);
            owned.reset();
        });
        worker.join();
        return 3;
    }
    if (mode == "--update-destruction") {
        manager->add_module(module.get_name(), &module);
        manager->add_update_module(&module);
        module.on_update = [&] { manager.reset(); };
        manager->update(1);
        return 3;
    }
    if (mode == "--final-completion-destruction") {
        manager->add_module(module.get_name(), &module);
        manager->create_task_group(module).start(ready_task(), [&](std::exception_ptr error) {
            if (error || manager->pending_tasks()) {
                std::_Exit(4);
            }
            manager.reset();
        });
        auto runtime = manager->get_task_runtime();
        runtime->poll();
        return 3;
    }
    return 2;
}
