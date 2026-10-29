// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <array>
#include <cstdlib>
#include <memory>
#include <new>
#include <string_view>

#include <ark/plugin_manager.hpp>
#include <ark/time/timer.hpp>
#include "../../src/application/startup_failure.hpp"

namespace {
bool fail_allocations{};

struct context
{
    std::unique_ptr<ark::time_wheel_manager> wheel;
};

ark::startup_cleanup_result release(void* pointer) {
    auto& capsule = *static_cast<context*>(pointer);
    if (capsule.wheel)
    {
        capsule.wheel->shut();
        capsule.wheel.reset();
    }
    ark::app_detail::write_startup_failure("cleanup-probe", "released exactly once");
    return ark::startup_cleanup_result::done;
}

class allocation_module final : public ark::module
{
public:
    std::shared_ptr<context> capsule;
    bool partial_wheel{};

    bool init() override {
        fail_allocations = true;
        if (partial_wheel)
        {
            capsule->wheel->init(1);
        }
        throw std::bad_alloc();
    }
};
} // namespace

void* operator new(std::size_t size) {
    if (!fail_allocations)
    {
        if (auto* pointer = std::malloc(size == 0 ? 1 : size))
        {
            return pointer;
        }
    }
    throw std::bad_alloc();
}

void* operator new[](std::size_t size) {
    return ::operator new(size);
}

void operator delete(void* pointer) noexcept {
    std::free(pointer);
}

void operator delete[](void* pointer) noexcept {
    std::free(pointer);
}

void operator delete(void* pointer, std::size_t) noexcept {
    std::free(pointer);
}

void operator delete[](void* pointer, std::size_t) noexcept {
    std::free(pointer);
}

int main(int argc, char** argv) {
    if (argc != 2)
    {
        return 2;
    }
    const std::string_view mode(argv[1]);
    std::array<char, 4096> long_detail{};
    long_detail.fill(char(120));
    long_detail.back() = char{};
    ark::plugin_manager manager;
    allocation_module module;
    module.capsule = std::make_shared<context>();
    module.capsule->wheel = std::make_unique<ark::time_wheel_manager>();
    module.partial_wheel = mode == "partial-wheel";
    if (mode != "partial-wheel")
    {
        module.capsule->wheel->init(1);
    }
    if (!manager.add_module("allocation-wheel", &module))
    {
        return 3;
    }
    try
    {
        if (mode == "capsule")
        {
            fail_allocations = true;
            (void)std::make_shared<context>();
            return 4;
        }
        // Preparation and fixed phase recording themselves must not allocate.
        fail_allocations = true;
        if (!manager.prepare_startup_cleanup("allocation-wheel", "allocation-test", module.capsule, release))
        {
            return 5;
        }
        ark::startup_call_record record;
        record.enter("allocation-wheel", "constructor");
        if (record.result != ark::startup_call_result::entered)
        {
            return 6;
        }
        fail_allocations = mode == "phase-record";
        (void)manager.start(1, {}, {}, {});
    }
    catch (const std::exception& error)
    {
        // Failure remains armed through the real production diagnostic, cleanup
        // action and process termination. Never disarm merely to make logging work.
        ark::app_detail::exit_startup_failure(manager, mode == "long-detail" ? long_detail.data() : error.what());
    }
    return 7;
}
