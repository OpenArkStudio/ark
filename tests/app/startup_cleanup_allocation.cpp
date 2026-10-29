// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <array>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <unordered_map>

#include <ark/plugin_manager.hpp>
#include <ark/time/timer.hpp>
#include "../../src/application/startup_failure.hpp"

namespace {
// The exercised ARK_NEW allocations use ordinary new, not aligned new.
static_assert(alignof(ark::time_wheel_data) <= __STDCPP_DEFAULT_NEW_ALIGNMENT__);
static_assert(alignof(ark::time_wheel_vec) <= __STDCPP_DEFAULT_NEW_ALIGNMENT__);

bool fail_allocations{};
const char* active_mode = "setup";
ark::plugin_manager* active_manager{};

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

// Keep nothrow allocations in the same malloc/free family as throwing new.
// Otherwise a sanitizer's nothrow interceptor can bypass this replacement and
// hand memory from its operator-new family to our replacement delete/free.
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    try
    { return ::operator new(size); }
    catch (...)
    { return nullptr; }
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    try
    { return ::operator new[](size); }
    catch (...)
    { return nullptr; }
}

void operator delete(void* pointer, const std::nothrow_t&) noexcept {
    ::operator delete(pointer);
}

void operator delete[](void* pointer, const std::nothrow_t&) noexcept {
    ::operator delete[](pointer);
}

namespace {

int allocation_family_probe() {
    void* single = ::operator new(sizeof(int), std::nothrow);
    void* array = ::operator new[](3 * sizeof(int), std::nothrow);
    const bool allocated = single && array;
    ::operator delete(single);
    ::operator delete[](array);
    if (!allocated)
    {
        return 10;
    }

    // A constructor exception uses the matching nothrow placement-delete form.
    struct construction_failure
    {
        construction_failure() {
            throw 17;
        }
    };

    bool single_threw = false, array_threw = false;
    try
    { (void)new (std::nothrow) construction_failure; }
    catch (int error)
    { single_threw = error == 17; }
    try
    { (void)new (std::nothrow) construction_failure[2]; }
    catch (int error)
    { array_threw = error == 17; }
    if (!single_threw || !array_threw)
    {
        return 11;
    }

    fail_allocations = true;
    single = ::operator new(sizeof(int), std::nothrow);
    array = ::operator new[](3 * sizeof(int), std::nothrow);
    const bool failed = !single && !array;
    ::operator delete(single);
    ::operator delete[](array);
    bool throwing_failed = false;
    try
    { (void)::operator new(1); }
    catch (const std::bad_alloc&)
    { throwing_failed = true; }
    if (!failed || !throwing_failed || !fail_allocations)
    {
        return 12;
    }
    ark::app_detail::write_native_stderr(
        "Startup allocation family: success, constructor failure and persistent nullptr passed\n");
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2)
    {
        return 2;
    }
    active_mode = argv[1];
    // A terminate inside noexcept must fail with context, not wait for Windows
    // crash reporting until the parent's process timeout hides the cause.
    std::set_terminate([]() noexcept {
        ark::app_detail::write_native_stderr("Startup allocation probe: unexpected std::terminate\n");
        if (active_manager)
        {
            const auto& call = active_manager->get_startup_call();
            ark::app_detail::write_startup_failure(call.phase.data(), active_mode, call.owner.data());
        }
        else
        {
            ark::app_detail::write_startup_failure("unexpected terminate", active_mode);
        }
#ifdef ARK_PLATFORM_WIN
        TerminateProcess(GetCurrentProcess(), 86);
#endif
        std::_Exit(86);
    });
    const std::string_view mode(argv[1]);
    if (mode == "allocation-family")
    {
        // The failed allocation flag stays armed until this isolated exit.
        std::_Exit(allocation_family_probe());
    }
    // Call arguments belong to the setup, not the armed startup operation.
    // MSVC's empty containers may allocate before start() can enter its guard.
    const std::string library_directory;
    const std::string configuration_directory;
    const std::unordered_map<std::string, std::string> plugins;
    std::array<char, 4096> long_detail{};
    long_detail.fill(char(120));
    long_detail.back() = char{};
    ark::plugin_manager manager;
    active_manager = &manager;
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
        if (mode == "terminate-probe")
        {
            std::terminate();
        }
        if (mode == "call-argument")
        {
            const auto failed_argument = []() -> const std::string& { throw std::bad_alloc{}; };
            (void)manager.start(1, failed_argument(), configuration_directory, plugins);
        }
        fail_allocations = mode == "phase-record";
        (void)manager.start(1, library_directory, configuration_directory, plugins);
    }
    catch (const std::exception& error)
    {
        const bool entered = manager.get_startup_call().result != ark::startup_call_result::not_entered;
        if ((mode == "phase-record" && !entered) || (mode == "call-argument" && entered))
        {
            return 8;
        }
        // Failure remains armed through the real production diagnostic, cleanup
        // action and process termination. Never disarm merely to make logging work.
        ark::app_detail::exit_startup_failure(manager, mode == "long-detail" ? long_detail.data() : error.what());
    }
    return 7;
}
