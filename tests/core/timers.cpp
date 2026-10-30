// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <chrono>
#include <memory>
#include <type_traits>
#include <vector>

#include "../network/doctest.h"

#include <ark/time/timer.hpp>

static_assert(!std::is_copy_constructible_v<ark::time_wheel_manager>);
static_assert(!std::is_copy_assignable_v<ark::time_wheel_manager>);
static_assert(!std::is_move_constructible_v<ark::time_wheel_manager>);
static_assert(!std::is_move_assignable_v<ark::time_wheel_manager>);

using namespace std::chrono_literals;

TEST_CASE("Time wheel preserves slot precision order and repeat counts") {
    ark::time_wheel_manager manager;
    manager.init(100);
    std::vector<std::uint64_t> calls;
    const auto first = manager.add_timer(ark::null_guid, 2, 10ms, [&](auto id, const auto&) { calls.push_back(id); });
    const auto second = manager.add_timer(ark::null_guid, 1, 10ms, [&](auto id, const auto&) { calls.push_back(id); });
    manager.update(109);
    CHECK(calls.empty());
    manager.update(110);
    CHECK(calls.empty());
    manager.update(120);
    CHECK(calls == std::vector<std::uint64_t>{first, second});
    manager.update(130);
    CHECK(calls == std::vector<std::uint64_t>{first, second, first});
    CHECK_FALSE(manager.remove_timer(first));
    CHECK_FALSE(manager.remove_timer(second));
    manager.shut();
}

TEST_CASE("Time wheel cancels pending and repeating callbacks across wheel levels") {
    ark::time_wheel_manager manager;
    manager.init(0);
    unsigned calls{};
    auto callback = [&](auto, const auto&) { ++calls; };
    const auto cancelled = manager.add_timer(ark::null_guid, 1, 2600ms, callback);
    const auto repeated = manager.add_timer(ark::null_guid, 0, 10ms, callback);
    CHECK(manager.remove_timer(cancelled));
    CHECK_FALSE(manager.remove_timer(cancelled));
    manager.update(30);
    CHECK(calls == 2);
    CHECK(manager.remove_timer(repeated));
    manager.update(2700);
    CHECK(calls == 2);
    manager.shut();
}

TEST_CASE("Time wheel destruction releases active and completed callback state") {
    std::weak_ptr<int> active;
    std::weak_ptr<int> completed;
    {
        ark::time_wheel_manager manager;
        manager.init(0);
        auto state = std::make_shared<int>(1);
        active = state;
        manager.add_timer(ark::null_guid, 1, 2600ms, [state](auto, const auto&) {});
        state = std::make_shared<int>(2);
        completed = state;
        manager.add_timer(ark::null_guid, 1, 10ms, [state](auto, const auto&) {});
        state.reset();
        manager.update(20);
        CHECK_FALSE(active.expired());
        CHECK_FALSE(completed.expired());
    }
    CHECK(active.expired());
    CHECK(completed.expired());
}

TEST_CASE("Time wheel shutdown is idempotent and can be initialized again") {
    ark::time_wheel_manager manager;
    manager.shut();
    manager.init(0);
    unsigned calls{};
    manager.add_timer(ark::null_guid, 1, 10ms, [&](auto, const auto&) { ++calls; });
    manager.shut();
    manager.shut();
    manager.init(100);
    manager.add_timer(ark::null_guid, 1, 10ms, [&](auto, const auto&) { ++calls; });
    manager.update(120);
    CHECK(calls == 1);
}

TEST_CASE("Time wheel callback predicate covers active cancelled and completed storage") {
    ark::time_wheel_manager manager;
    CHECK_FALSE(manager.has_retained_callbacks());
    manager.init(0);
    CHECK_FALSE(manager.has_retained_callbacks());
    auto state = std::make_shared<int>(1);
    std::weak_ptr<int> retained = state;
    const auto cancelled = manager.add_timer(ark::null_guid, 1, 1h, [state](auto, const auto&) {});
    CHECK(manager.has_retained_callbacks());
    state.reset();
    CHECK_FALSE(retained.expired());
    CHECK(manager.remove_timer(cancelled));
    CHECK(retained.expired());
    CHECK_FALSE(manager.has_retained_callbacks());

    state = std::make_shared<int>(2);
    retained = state;
    const auto completed = manager.add_timer(ark::null_guid, 1, 10ms, [state](auto, const auto&) {});
    state.reset();
    manager.update(20);
    CHECK_FALSE(manager.remove_timer(completed));
    CHECK_FALSE(retained.expired());
    CHECK(manager.has_retained_callbacks());
    manager.shut();
    CHECK(retained.expired());
    CHECK_FALSE(manager.has_retained_callbacks());
}
