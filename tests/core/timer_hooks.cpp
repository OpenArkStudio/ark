// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <cstdlib>
#include <limits>
#include <new>
#include <unordered_set>

#include "../network/doctest.h"

namespace {

struct allocation_tag
{
    ~allocation_tag();
    mutable bool allocated{};
};

std::unordered_set<void*> live;
std::size_t allocations{};
std::size_t releases{};
std::size_t fail_after{std::numeric_limits<std::size_t>::max()};
bool fail_standard_allocation{};
std::size_t fail_container_after{std::numeric_limits<std::size_t>::max()};

allocation_tag::~allocation_tag() {
    if (!allocated)
    {
        return;
    }
    // A placement argument outlives construction through the full new-expression.
    // Arm after construction so MSVC's noexcept Debug iterator proxies cannot fail.
    if (fail_container_after == 0)
    {
        fail_container_after = std::numeric_limits<std::size_t>::max();
        fail_standard_allocation = true;
    }
    else if (fail_container_after != std::numeric_limits<std::size_t>::max())
    {
        --fail_container_after;
    }
}

bool should_fail() {
    if (fail_after == std::numeric_limits<std::size_t>::max())
    {
        return false;
    }
    if (fail_after > 0)
    {
        --fail_after;
        return false;
    }
    fail_after = std::numeric_limits<std::size_t>::max();
    return true;
}

template<typename T>
void release(T*& value) noexcept {
    if (!value)
    {
        return;
    }
    if (live.erase(value) != 1)
    {
        std::abort();
    }
    ++releases;
    delete value;
    value = nullptr;
}

} // namespace

void* operator new(std::size_t size) {
    if (fail_standard_allocation)
    {
        fail_standard_allocation = false;
        throw std::bad_alloc{};
    }
    if (auto* value = std::malloc(size ? size : 1))
    {
        return value;
    }
    throw std::bad_alloc{};
}

void operator delete(void* value) noexcept {
    std::free(value);
}

void operator delete(void* value, std::size_t) noexcept {
    ::operator delete(value);
}

void* operator new(std::size_t size, const allocation_tag& tag) noexcept {
    if (should_fail())
    {
        return nullptr;
    }
    auto* value = ::operator new(size);
    live.insert(value);
    ++allocations;
    tag.allocated = true;
    return value;
}

void operator delete(void* value, const allocation_tag& tag) noexcept {
    tag.allocated = false;
    live.erase(value);
    ++releases;
    ::operator delete(value);
}

// Keep these hook-expanded inline definitions in a separate executable.
#define ARK_NEW new (allocation_tag{})
#define ARK_DELETE(pointer) release(pointer)
#include <ark/time/timer.hpp>

using namespace std::chrono_literals;

TEST_CASE("Time wheel owns cancelled timers until shutdown") {
    REQUIRE(live.empty());
    ark::time_wheel_manager manager;
    manager.init(0);
    const auto id = manager.add_timer(ark::null_guid, 1, 1h, [](auto, const auto&) {});
    CHECK(manager.remove_timer(id));
    manager.shut();
    CHECK(live.empty());
    CHECK(allocations == releases);
}

TEST_CASE("Time wheel partial initialization null failures release every allocation") {
    for (const std::size_t position : {0u, 17u, ark::free_timer_size, ark::free_timer_size + ark::tv_num - 1})
    {
        REQUIRE(live.empty());
        ark::time_wheel_manager manager;
        fail_after = position;
        manager.init(0);
        manager.shut();
        CHECK(live.empty());
        CHECK(allocations == releases);
        manager.init(100);
        manager.shut();
        CHECK(live.empty());
    }
}

TEST_CASE("Time wheel container allocation exceptions unwind initialization without leaking") {
    for (const std::size_t position : {0u, 17u, ark::free_timer_size, ark::free_timer_size + ark::tv_num - 1})
    {
        REQUIRE(live.empty());
        ark::time_wheel_manager manager;
        fail_container_after = position;
        CHECK_THROWS_AS(manager.init(0), std::bad_alloc);
        CHECK(live.empty());
        CHECK(allocations == releases);
    }
}

TEST_CASE("Time wheel pool exhaustion returns zero for a null allocation and recovers") {
    REQUIRE(live.empty());
    ark::time_wheel_manager manager;
    manager.init(0);
    for (std::size_t i = 0; i < ark::free_timer_size; ++i)
    {
        CHECK(manager.add_timer(ark::null_guid, 1, 1h, [](auto, const auto&) {}) != 0);
    }
    fail_after = 0;
    CHECK(manager.add_timer(ark::null_guid, 1, 1h, [](auto, const auto&) {}) == 0);
    const auto id = manager.add_timer(ark::null_guid, 1, 1h, [](auto, const auto&) {});
    CHECK(id == ark::free_timer_size + 1);
    CHECK(manager.remove_timer(id));
    manager.shut();
    CHECK(live.empty());
    CHECK(allocations == releases);
}

TEST_CASE("Time wheel failed replacement keeps the initialized wheel usable") {
    REQUIRE(live.empty());
    ark::time_wheel_manager manager;
    manager.init(0);
    unsigned calls{};
    manager.add_timer(ark::null_guid, 1, 10ms, [&](auto, const auto&) { ++calls; });
    const auto initial_live = live.size();
    fail_after = ark::free_timer_size;
    manager.init(100);
    CHECK(live.size() == initial_live);
    manager.update(20);
    CHECK(calls == 1);
    manager.shut();
    CHECK(live.empty());
    CHECK(allocations == releases);
}

TEST_CASE("Time wheel growth container exception releases the uncommitted timer") {
    REQUIRE(live.empty());
    ark::time_wheel_manager manager;
    manager.init(0);
    for (std::size_t i = 0; i < ark::free_timer_size; ++i)
    {
        manager.add_timer(ark::null_guid, 1, 1h, [](auto, const auto&) {});
    }
    const auto initial_live = live.size();
    fail_container_after = 0;
    CHECK_THROWS_AS(manager.add_timer(ark::null_guid, 1, 1h, [](auto, const auto&) {}), std::bad_alloc);
    CHECK(live.size() == initial_live);
    CHECK(manager.add_timer(ark::null_guid, 1, 1h, [](auto, const auto&) {}) == ark::free_timer_size + 1);
    manager.shut();
    manager.shut();
    CHECK(live.empty());
    CHECK(allocations == releases);
}

TEST_CASE("Time wheel destructor releases active completed and cancelled entries once") {
    REQUIRE(live.empty());
    {
        ark::time_wheel_manager manager;
        manager.init(0);
        manager.add_timer(ark::null_guid, 1, 1h, [](auto, const auto&) {});
        manager.add_timer(ark::null_guid, 1, 10ms, [](auto, const auto&) {});
        const auto cancelled = manager.add_timer(ark::null_guid, 1, 1h, [](auto, const auto&) {});
        CHECK(manager.remove_timer(cancelled));
        manager.update(20);
    }
    CHECK(live.empty());
    CHECK(allocations == releases);
}

TEST_CASE("Timer container failure injection starts after noexcept object construction") {
    class proxy_probe
    {
    public:
        proxy_probe() noexcept
            : proxy_(::operator new(1)) {
        }

        ~proxy_probe() {
            ::operator delete(proxy_);
        }

    private:
        void* proxy_;
    };

    // MSVC Debug vector constructors allocate an iterator proxy inside noexcept.
    // Model that allocation even when the host STL has no Debug iterator proxy.
    REQUIRE(live.empty());
    fail_container_after = 0;
    auto* value = ARK_NEW proxy_probe;
    bool threw{};
    try
    {
        auto* storage = ::operator new(1);
        ::operator delete(storage);
    }
    catch (const std::bad_alloc&)
    { threw = true; }
    CHECK(threw);
    CHECK_FALSE(fail_standard_allocation);
    release(value);
    CHECK(live.empty());
    CHECK(allocations == releases);
}

TEST_CASE("Timer hook constructor failure releases storage without arming a later allocation") {
    class throwing_probe
    {
    public:
        throwing_probe() {
            throw std::bad_alloc{};
        }
    };

    REQUIRE(live.empty());
    fail_container_after = 0;
    CHECK_THROWS_AS((ARK_NEW throwing_probe), std::bad_alloc);
    CHECK(fail_container_after == 0);
    CHECK_FALSE(fail_standard_allocation);
    CHECK(live.empty());
    CHECK(allocations == releases);
    fail_container_after = std::numeric_limits<std::size_t>::max();
}

TEST_CASE("Time wheel callback predicate includes an addition interrupted before ID registration") {
    REQUIRE(live.empty());
    ark::time_wheel_manager manager;
    manager.init(0);
    unsigned calls{};
    ark::timer_functor callback = [&](auto, const auto&) { ++calls; };
    fail_standard_allocation = true;
    CHECK_THROWS_AS(manager.add_timer(ark::null_guid, 1, 1h, std::move(callback)), std::bad_alloc);
    CHECK_FALSE(manager.remove_timer(1));
    CHECK(manager.has_retained_callbacks());
    CHECK(calls == 0);
    manager.shut();
    CHECK_FALSE(manager.has_retained_callbacks());
    CHECK(live.empty());
    CHECK(allocations == releases);
}
