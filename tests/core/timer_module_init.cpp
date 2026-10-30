// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <array>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <string>

#include "../network/doctest.h"

#include <ark/plugin_manager.hpp>
#include <ark/time/timer.hpp>
#include "utility/utility_plugin.hpp"

#ifndef NDEBUG
#error This regression must exercise the production module with assertions disabled.
#endif

namespace {

std::size_t nothrow_calls{};
std::size_t fail_remaining{};
std::size_t fail_after_timer{};
std::size_t fail_standard_remaining{};
std::size_t allocation_attempts{};
bool fail_allocations{};
std::array<void*, ark::free_timer_size + ark::tv_num> live_wheel_allocations{};

std::size_t live_count() {
    std::size_t count{};
    for (auto* pointer : live_wheel_allocations)
    {
        count += pointer != nullptr;
    }
    return count;
}

class receiver
{
public:
    void on_timer(std::uint64_t, const ark::guid_t&) {
        ++calls;
    }

    unsigned calls{};
};

class later_failure final : public ark::module
{
public:
    bool post_init() override {
        if (arm)
        {
            id = timer->add_forever_timer(ark::null_guid, std::chrono::milliseconds(10), &target, &receiver::on_timer);
            if (remove)
            {
                removed = timer->remove_timer(id);
            }
        }
        if (persistent_failure)
        {
            fail_allocations = true;
            throw std::bad_alloc{};
        }
        return false;
    }

    bool pre_shut() override {
        ++normal_stops;
        return true;
    }

    bool shut() override {
        ++normal_stops;
        return true;
    }

    ark::timer_module* timer{};
    receiver target;
    std::uint64_t id{};
    bool arm{};
    bool remove{};
    bool removed{};
    bool persistent_failure{};
    unsigned normal_stops{};
};

struct independent_resource
{
    unsigned releases{};
    std::size_t wheel_storage_at_release{};
};

ark::startup_cleanup_result release_independent_resource(void* pointer) {
    auto& resource = *static_cast<independent_resource*>(pointer);
    ++resource.releases;
    resource.wheel_storage_at_release = live_count();
    return ark::startup_cleanup_result::done;
}

class enrolling_failure final : public ark::module
{
public:
    bool post_init() override {
        enrolled = manager->prepare_startup_cleanup("after_timer", "test", resource, release_independent_resource);
        if (arm)
        {
            timer_id =
                timer->add_forever_timer(ark::null_guid, std::chrono::milliseconds(10), &target, &receiver::on_timer);
        }
        return false;
    }

    ark::plugin_manager* manager{};
    ark::timer_module* timer{};
    std::shared_ptr<independent_resource> resource;
    receiver target;
    std::uint64_t timer_id{};
    bool arm{};
    bool enrolled{};
};

} // namespace

// Keep all allocation forms in the same malloc/free family under sanitizers.
// The fixed array tracks real production ARK_NEW storage without allocating.
void* operator new(std::size_t size) {
    ++allocation_attempts;
    if (fail_standard_remaining && --fail_standard_remaining == 0)
    {
        throw std::bad_alloc{};
    }
    if (!fail_allocations)
    {
        if (auto* pointer = std::malloc(size ? size : 1))
        {
            return pointer;
        }
    }
    throw std::bad_alloc{};
}

void* operator new[](std::size_t size) {
    return ::operator new(size);
}

void operator delete(void* pointer) noexcept {
    for (auto& live : live_wheel_allocations)
    {
        if (live == pointer)
        {
            live = nullptr;
            break;
        }
    }
    std::free(pointer);
}

void operator delete[](void* pointer) noexcept {
    ::operator delete(pointer);
}

void operator delete(void* pointer, std::size_t) noexcept {
    ::operator delete(pointer);
}

void operator delete[](void* pointer, std::size_t) noexcept {
    ::operator delete(pointer);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    ++nothrow_calls;
    if (fail_remaining && --fail_remaining == 0)
    {
        return nullptr;
    }
    try
    {
        auto* pointer = ::operator new(size);
        for (auto& live : live_wheel_allocations)
        {
            if (!live)
            {
                live = pointer;
                if (fail_after_timer && --fail_after_timer == 0)
                {
                    // Only inject at time_wheel_data (no allocating constructor),
                    // before the following list insertion, never a Debug STL proxy.
                    fail_allocations = true;
                }
                return pointer;
            }
        }
        std::abort();
    }
    catch (...)
    { return nullptr; }
}

void operator delete(void* value, const std::nothrow_t&) noexcept {
    ::operator delete(value);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    try
    { return ::operator new[](size); }
    catch (...)
    { return nullptr; }
}

void operator delete[](void* value, const std::nothrow_t&) noexcept {
    ::operator delete[](value);
}

TEST_CASE("Production timer module rejects null allocations without continuing initialization") {
    for (const std::size_t ordinal : {1u, 18u, ark::free_timer_size + 1, ark::free_timer_size + ark::tv_num})
    {
        ark::plugin_manager manager;
        auto timer = ark::detail::make_timer_module();
        timer->set_plugin_manager(&manager);
        const auto initial_calls = nothrow_calls;
        fail_remaining = ordinal;
        const auto initialized = timer->init();
        CHECK_FALSE(initialized);
        CHECK(fail_remaining == 0);
        CHECK(nothrow_calls - initial_calls == ordinal);
        CHECK(live_count() == 0);
        // Failed startup proceeds only to destruction, without later lifecycle calls.
    }
}

TEST_CASE("Production timer startup releases partial null allocations exactly once") {
    for (const std::size_t ordinal : {1u, 18u, ark::free_timer_size + 1, ark::free_timer_size + ark::tv_num})
    {
        ark::plugin_manager manager;
        auto timer = ark::detail::make_timer_module();
        timer->set_plugin_manager(&manager);
        REQUIRE(manager.add_module("Timer", timer.get()));
        fail_remaining = ordinal;
        REQUIRE_FALSE(manager.start(1, {}, {}, {}));
        CHECK(live_count() == 0);
        timer.reset(); // The prepared action must not borrow the incomplete module.
        CHECK(manager.cleanup_failed_startup() == ark::startup_cleanup_result::done);
        CHECK(manager.cleanup_failed_startup() == ark::startup_cleanup_result::done);
        CHECK(live_count() == 0);
    }
}

TEST_CASE("Production timer startup cleans up under persistent container allocation failure") {
    for (const std::size_t ordinal : {1u, 18u})
    {
        ark::plugin_manager manager;
        auto timer = ark::detail::make_timer_module();
        timer->set_plugin_manager(&manager);
        REQUIRE(manager.add_module("Timer", timer.get()));
        fail_after_timer = ordinal;
        bool threw{};
        try
        { (void)manager.start(1, {}, {}, {}); }
        catch (const std::bad_alloc&)
        { threw = true; }
        const auto attempts = allocation_attempts;
        const auto first = manager.cleanup_failed_startup();
        const auto second = manager.cleanup_failed_startup();
        const auto after_cleanup = allocation_attempts;
        fail_allocations = false;
        REQUIRE(threw);
        CHECK(first == ark::startup_cleanup_result::done);
        CHECK(second == first);
        CHECK(after_cleanup == attempts);
        CHECK(live_count() == 0);
    }
}

TEST_CASE("Production timer capsule allocation failure precedes acquisition") {
    ark::plugin_manager manager;
    auto timer = ark::detail::make_timer_module();
    timer->set_plugin_manager(&manager);
    const auto calls = nothrow_calls;
    fail_allocations = true;
    bool threw{};
    try
    { (void)timer->init(); }
    catch (const std::bad_alloc&)
    { threw = true; }
    fail_allocations = false;
    CHECK(threw);
    CHECK(nothrow_calls == calls);
    CHECK(live_count() == 0);
    REQUIRE(timer->init()); // No failed enrollment or resource was left behind.
    CHECK(timer->pre_shut());
}

TEST_CASE("Production timer rejected enrollment acquires no wheel storage") {
    ark::plugin_manager manager;
    auto context = std::make_shared<int>(1);
    REQUIRE(manager.prepare_startup_cleanup(
        "Timer", "test", context, [](void*) { return ark::startup_cleanup_result::done; }));
    auto timer = ark::detail::make_timer_module();
    timer->set_plugin_manager(&manager);
    const auto calls = nothrow_calls;
    CHECK_FALSE(timer->init());
    CHECK(nothrow_calls == calls);
    CHECK(live_count() == 0);
}

TEST_CASE("Production Timer composes with independent cleanup owners and retains unsafe predecessors") {
    for (const bool arm : {false, true})
    {
        ark::plugin_manager manager;
        auto before = std::make_shared<independent_resource>();
        auto after = std::make_shared<independent_resource>();
        REQUIRE(manager.prepare_startup_cleanup("before_timer", "test", before, release_independent_resource));
        auto timer = ark::detail::make_timer_module();
        timer->set_plugin_manager(&manager);
        enrolling_failure later;
        later.manager = &manager;
        later.timer = timer.get();
        later.resource = after;
        later.arm = arm;
        REQUIRE(manager.add_module("Timer", timer.get()));
        REQUIRE(manager.add_module("later", &later));
        REQUIRE_FALSE(manager.start(1, {}, {}, {}));
        REQUIRE(later.enrolled);
        REQUIRE(live_count() == ark::free_timer_size + ark::tv_num);

        const auto attempts = allocation_attempts;
        fail_allocations = true;
        const auto result = manager.cleanup_failed_startup();
        const auto repeated = manager.cleanup_failed_startup();
        const auto after_cleanup = allocation_attempts;
        fail_allocations = false;
        CHECK(after_cleanup == attempts);
        CHECK(repeated == result);
        CHECK(after->releases == 1);
        CHECK(after->wheel_storage_at_release == ark::free_timer_size + ark::tv_num);
        CHECK(later.target.calls == 0);
        if (arm)
        {
            REQUIRE(later.timer_id != 0);
            CHECK(result == ark::startup_cleanup_result::unsafe);
            CHECK(before->releases == 0);
            CHECK(live_count() == ark::free_timer_size + ark::tv_num);
            // Test-only retirement with all receivers and code still alive.
            CHECK(timer->remove_timer(later.timer_id));
            CHECK(timer->pre_shut());
            CHECK(manager.cleanup_failed_startup() == ark::startup_cleanup_result::unsafe);
            CHECK(before->releases == 0);
        }
        else
        {
            CHECK(result == ark::startup_cleanup_result::done);
            CHECK(before->releases == 1);
            CHECK(before->wheel_storage_at_release == 0);
        }
        CHECK(live_count() == 0);
    }
}

TEST_CASE("Production Timer rejects a full cleanup registry before wheel acquisition") {
    ark::plugin_manager manager;
    std::array<std::shared_ptr<independent_resource>, ark::startup_cleanup_capacity> resources;
    for (std::size_t index{}; index < resources.size(); ++index)
    {
        resources[index] = std::make_shared<independent_resource>();
        REQUIRE(manager.prepare_startup_cleanup(
            "external_" + std::to_string(index), "test", resources[index], release_independent_resource));
    }
    auto timer = ark::detail::make_timer_module();
    timer->set_plugin_manager(&manager);
    REQUIRE(manager.add_module("Timer", timer.get()));
    const auto calls = nothrow_calls;
    CHECK_FALSE(manager.start(1, {}, {}, {}));
    CHECK(nothrow_calls == calls);
    CHECK(live_count() == 0);
    CHECK(manager.cleanup_failed_startup() == ark::startup_cleanup_result::done);
    for (const auto& resource : resources)
    {
        CHECK(resource->releases == 1);
        CHECK(resource->wheel_storage_at_release == 0);
    }
}

TEST_CASE("Production timer wheel object allocation failure leaves an independent prepared capsule") {
    ark::plugin_manager manager;
    auto timer = ark::detail::make_timer_module();
    timer->set_plugin_manager(&manager);
    // The first allocation constructs the empty capsule. Enrollment does not
    // allocate; the second allocation attempts to construct the wheel itself.
    fail_standard_remaining = 2;
    bool threw{};
    try
    { (void)timer->init(); }
    catch (const std::bad_alloc&)
    { threw = true; }
    REQUIRE(threw);
    CHECK(fail_standard_remaining == 0);
    CHECK(live_count() == 0);
    timer.reset();
    later_failure later;
    REQUIRE(manager.add_module("later", &later));
    REQUIRE_FALSE(manager.start(1, {}, {}, {}));
    CHECK(manager.cleanup_failed_startup() == ark::startup_cleanup_result::done);
    CHECK(manager.cleanup_failed_startup() == ark::startup_cleanup_result::done);
    CHECK(live_count() == 0);
}

TEST_CASE("Production timer later startup failure releases only callback-free storage") {
    for (bool arm : {false, true})
    {
        for (bool remove : {false, true})
        {
            for (bool persistent_failure : {false, true})
            {
                ark::plugin_manager manager;
                auto timer = ark::detail::make_timer_module();
                timer->set_plugin_manager(&manager);
                later_failure later;
                later.timer = timer.get();
                later.arm = arm;
                later.remove = remove;
                later.persistent_failure = persistent_failure;
                REQUIRE(manager.add_module("Timer", timer.get()));
                REQUIRE(manager.add_module("later", &later));
                bool returned{}, threw{};
                try
                { returned = manager.start(1, {}, {}, {}); }
                catch (const std::bad_alloc&)
                { threw = true; }
                const auto live_before = live_count();
                const auto attempts = allocation_attempts;
                const auto result = manager.cleanup_failed_startup();
                const auto repeated = manager.cleanup_failed_startup();
                const auto after_cleanup = allocation_attempts;
                fail_allocations = false;
                CHECK_FALSE(returned);
                CHECK(threw == persistent_failure);
                CHECK(live_before == ark::free_timer_size + ark::tv_num);
                CHECK(after_cleanup == attempts);
                CHECK(repeated == result);
                CHECK(later.normal_stops == 0);
                CHECK(later.target.calls == 0);
                if (arm && !remove)
                {
                    CHECK(result == ark::startup_cleanup_result::unsafe);
                    CHECK(live_count() == live_before);
                    CHECK(timer->remove_timer(later.id));
                    // A later change cannot retry an already-entered action.
                    CHECK(manager.cleanup_failed_startup() == ark::startup_cleanup_result::unsafe);
                    CHECK(live_count() == live_before);
                }
                else
                {
                    CHECK(result == ark::startup_cleanup_result::done);
                    CHECK(live_count() == 0);
                    CHECK(later.removed == (arm && remove));
                }
                // Explicit test teardown only. Emergency startup never calls this.
                CHECK(timer->pre_shut());
                CHECK(live_count() == 0);
            }
        }
    }
}

TEST_CASE("Production timer successful startup disarms cleanup and keeps its wheel alive") {
    ark::plugin_manager manager;
    auto timer = ark::detail::make_timer_module();
    timer->set_plugin_manager(&manager);
    REQUIRE(manager.add_module("Timer", timer.get()));
    REQUIRE(manager.start(1, {}, {}, {}));
    REQUIRE(live_count() == ark::free_timer_size + ark::tv_num);
    CHECK(manager.cleanup_failed_startup() == ark::startup_cleanup_result::unsafe);
    receiver target;
    const auto id =
        timer->add_forever_timer(ark::null_guid, std::chrono::milliseconds(10), &target, &receiver::on_timer);
    REQUIRE(id != 0);
    CHECK(manager.update(21));
    CHECK(timer->update());
    CHECK(target.calls == 1);
    CHECK(timer->remove_timer(id));
    CHECK(manager.stop(21));
    CHECK(live_count() == 0);
    CHECK(timer->pre_shut());
    CHECK(manager.stop(21));
}

TEST_CASE("Production timer prepared capsule outlives its module without a dangling action") {
    ark::plugin_manager manager;
    auto timer = ark::detail::make_timer_module();
    timer->set_plugin_manager(&manager);
    later_failure later;
    REQUIRE(manager.add_module("Timer", timer.get()));
    REQUIRE(manager.add_module("later", &later));
    REQUIRE_FALSE(manager.start(1, {}, {}, {}));
    REQUIRE(live_count() == ark::free_timer_size + ark::tv_num);
    timer.reset();
    CHECK(live_count() == ark::free_timer_size + ark::tv_num);
    CHECK(manager.cleanup_failed_startup() == ark::startup_cleanup_result::done);
    CHECK(live_count() == 0);
    CHECK(manager.cleanup_failed_startup() == ark::startup_cleanup_result::done);
    CHECK(later.normal_stops == 0);
}

TEST_CASE("Production timer module initializes normally and closes through its public interface") {
    ark::plugin_manager manager;
    auto timer = ark::detail::make_timer_module();
    timer->set_plugin_manager(&manager);
    REQUIRE(timer->init());
    receiver target;
    const auto id =
        timer->add_forever_timer(ark::null_guid, std::chrono::milliseconds(10), &target, &receiver::on_timer);
    REQUIRE(id != 0);
    CHECK(timer->remove_timer(id));
    CHECK_FALSE(timer->remove_timer(id));
    CHECK(timer->update());
    CHECK(timer->pre_shut());
    CHECK(timer->pre_shut());
}
