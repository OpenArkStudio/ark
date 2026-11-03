// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#define DOCTEST_CONFIG_IMPLEMENT
#include "../network/doctest.h"

// Compile the production implementation once, exposing only its independent
// capsule to this test rather than adding a public ownership inspection API.
#include "../../src/plugins/utility/schedule_module_impl.cpp"

namespace {
thread_local bool reject_allocations{};
} // namespace

void* operator new(std::size_t size) {
    if (!reject_allocations)
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
    std::free(pointer);
}

void operator delete[](void* pointer, const std::nothrow_t&) noexcept {
    std::free(pointer);
}

namespace ark {

struct schedule_startup_test_access
{
    static auto state(schedule_module& module) {
        return static_cast<schedule_module_impl&>(module).startup_;
    }

    static void at(schedule_module& module, const std::string& time, scheduler_functor callback) {
        static_cast<schedule_module_impl&>(module).add_at_schedule(time, std::move(callback));
    }
};

} // namespace ark

namespace {

using namespace std::chrono_literals;
using access = ark::schedule_startup_test_access;

[[noreturn]] void exit_probe(int status) noexcept {
    std::fflush(stderr);
#if defined(_WIN32)
    ::TerminateProcess(::GetCurrentProcess(), static_cast<UINT>(status));
    std::_Exit(3);
#else
    std::_Exit(status);
#endif
}

struct failing_module final : ark::module
{
    unsigned phase{};
    bool throws{};
    unsigned ordinary_cleanup{};

    bool step(unsigned current) const {
        if (phase != current)
        {
            return true;
        }
        if (throws)
        {
            throw std::runtime_error("controlled later Schedule startup failure");
        }
        return false;
    }

    bool init() override {
        return step(0);
    }

    bool post_init() override {
        return step(1);
    }

    bool check_config() override {
        return step(2);
    }

    bool pre_update() override {
        return step(3);
    }

    bool pre_shut() override {
        ++ordinary_cleanup;
        return true;
    }

    bool shut() override {
        ++ordinary_cleanup;
        return true;
    }
};

ark::startup_cleanup_result release_older(void* pointer) {
    ++*static_cast<unsigned*>(pointer);
    return ark::startup_cleanup_result::done;
}

auto make_schedule(ark::plugin_manager& manager) {
    auto module = ark::detail::make_schedule_module();
    module->set_plugin_manager(&manager);
    module->set_name("Schedule");
    return module;
}

template<typename Predicate>
bool eventually(Predicate&& ready) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!ready())
    {
        if (std::chrono::steady_clock::now() >= deadline)
        {
            return false;
        }
        std::this_thread::yield();
    }
    return true;
}

template<typename State>
bool dispatcher_retired(const State& state) {
    return state->dispatch.valid() && state->dispatch.wait_for(0s) == std::future_status::ready;
}

template<typename State>
void retire_test_owner(const State& state) {
    // Explicit fixture teardown after retention assertions. The failed manager
    // must never perform this ordinary cancellation or retry its cleanup action.
    if (state->worker)
    {
        state->worker->close();
        REQUIRE(eventually([&] { return state->worker->ready_to_shut(); }));
        REQUIRE(state->worker->pre_shut());
        state->worker.reset();
    }
}

} // namespace

TEST_CASE("Scheduler rejects overflowing worker counts before creating threads") {
    CHECK_THROWS_AS((ark::scheduler{std::numeric_limits<unsigned int>::max()}), std::length_error);
    CHECK_THROWS_AS((ark::scheduler{static_cast<unsigned int>(std::numeric_limits<int>::max())}), std::length_error);
}

TEST_CASE("Schedule failed startup retires its unexposed workers after every failed phase") {
    for (unsigned phase{}; phase < 4; ++phase)
    {
        for (bool throws : {false, true})
        {
            CAPTURE(phase);
            CAPTURE(throws);
            ark::plugin_manager manager;
            auto older = std::make_shared<unsigned>(0);
            REQUIRE(manager.prepare_startup_cleanup("older", "test", older, release_older));
            auto schedule = make_schedule(manager);
            failing_module failure;
            failure.phase = phase;
            failure.throws = throws;
            REQUIRE(manager.add_module("Schedule", schedule.get()));
            REQUIRE(manager.add_module("failure", &failure));
            if (throws)
            {
                CHECK_THROWS_AS(manager.start(0, {}, {}, {}), std::runtime_error);
            }
            else
            {
                CHECK_FALSE(manager.start(0, {}, {}, {}));
            }
            auto state = access::state(*schedule);
            REQUIRE(state);
            REQUIRE(state->worker != nullptr);
            CHECK_FALSE(dispatcher_retired(state));
            CHECK(manager.cleanup_failed_startup() == ark::startup_cleanup_result::done);
            CHECK(state->sealed);
            CHECK(state->worker == nullptr);
            CHECK(dispatcher_retired(state));
            CHECK(*older == 1);
            auto capture = std::make_shared<int>(42);
            std::weak_ptr<int> lifetime = capture;
            access::at(*schedule, "2100-01-01 00:00:00", [capture = std::move(capture)] {});
            CHECK(lifetime.expired());
            CHECK(failure.ordinary_cleanup == 0);
            CHECK(manager.cleanup_failed_startup() == ark::startup_cleanup_result::done);
            CHECK(*older == 1);
            CHECK_FALSE(manager.update(1));
            CHECK_THROWS_AS(manager.start(0, {}, {}, {}), std::logic_error);
        }
    }
}

TEST_CASE("Schedule successful startup disarms failure cleanup and keeps normal shutdown") {
    ark::plugin_manager manager;
    auto schedule = make_schedule(manager);
    REQUIRE(manager.add_module("Schedule", schedule.get()));
    REQUIRE(manager.start(0, {}, {}, {}));
    auto state = access::state(*schedule);
    REQUIRE(state);
    REQUIRE(state->worker);
    CHECK(manager.cleanup_failed_startup() == ark::startup_cleanup_result::unsafe);
    CHECK_FALSE(dispatcher_retired(state));
    CHECK(eventually([&] { return manager.stop(1); }));
    CHECK(dispatcher_retired(state));
    CHECK(manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::stopped);
}

TEST_CASE("Schedule full cleanup registry rejects init before acquiring a scheduler") {
    ark::plugin_manager manager;
    auto older = std::make_shared<unsigned>(0);
    for (std::size_t index{}; index < ark::startup_cleanup_capacity; ++index)
    {
        REQUIRE(manager.prepare_startup_cleanup("owner-" + std::to_string(index), "test", older, release_older));
    }
    auto schedule = make_schedule(manager);
    REQUIRE(manager.add_module("Schedule", schedule.get()));
    CHECK_FALSE(manager.start(0, {}, {}, {}));
    auto state = access::state(*schedule);
    CHECK((!state || (!state->worker && !state->dispatch.valid())));
    CHECK(manager.cleanup_failed_startup() == ark::startup_cleanup_result::done);
    CHECK(*older == ark::startup_cleanup_capacity);
}

TEST_CASE("Schedule retained future callback forbids cleanup and preserves older owners") {
    ark::plugin_manager manager;
    auto older = std::make_shared<unsigned>(0);
    REQUIRE(manager.prepare_startup_cleanup("older", "test", older, release_older));
    auto schedule = make_schedule(manager);
    REQUIRE(schedule->init());
    auto state = access::state(*schedule);
    REQUIRE(state);
    REQUIRE(state->worker != nullptr);
    auto captured = std::make_shared<int>(42);
    std::weak_ptr<int> retained = captured;
    std::atomic<unsigned> calls{};
    access::at(*schedule, "2099-12-31 23:59:59", [&, captured] { calls += *captured == 42; });
    captured.reset();
    failing_module failure;
    REQUIRE(manager.add_module("failure", &failure));
    REQUIRE_FALSE(manager.start(0, {}, {}, {}));
    std::weak_ptr<typename decltype(state)::element_type> capsule = state;
    state.reset();
    schedule.reset();
    REQUIRE_FALSE(capsule.expired());
    state = capsule.lock();
    CHECK(manager.cleanup_failed_startup() == ark::startup_cleanup_result::unsafe);
    REQUIRE(state->worker);
    CHECK_FALSE(dispatcher_retired(state));
    CHECK_FALSE(retained.expired());
    CHECK(calls == 0);
    CHECK(*older == 0);
    CHECK_FALSE(manager.update(1));
    CHECK(calls == 0);
    retire_test_owner(state);
    CHECK(retained.expired());
    CHECK(calls == 0);
    CHECK(manager.cleanup_failed_startup() == ark::startup_cleanup_result::unsafe);
    CHECK(*older == 0);
    CHECK(failure.ordinary_cleanup == 0);
}

TEST_CASE("Schedule attempted invalid registration remains unsafe even without a queued timer") {
    ark::plugin_manager manager;
    auto older = std::make_shared<unsigned>(0);
    REQUIRE(manager.prepare_startup_cleanup("older", "test", older, release_older));
    auto schedule = make_schedule(manager);
    REQUIRE(schedule->init());
    auto state = access::state(*schedule);
    CHECK_THROWS_AS(access::at(*schedule, "invalid-time", [] {}), std::runtime_error);
    failing_module failure;
    REQUIRE(manager.add_module("failure", &failure));
    REQUIRE_FALSE(manager.start(0, {}, {}, {}));
    CHECK(manager.cleanup_failed_startup() == ark::startup_cleanup_result::unsafe);
    CHECK_FALSE(dispatcher_retired(state));
    CHECK(*older == 0);
    retire_test_owner(state);
    CHECK(manager.cleanup_failed_startup() == ark::startup_cleanup_result::unsafe);
    CHECK(*older == 0);
}

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--allocation-failure")
    {
        ark::plugin_manager manager;
        auto older = std::make_shared<unsigned>(0);
        if (!manager.prepare_startup_cleanup("older", "test", older, release_older))
        {
            exit_probe(2);
        }
        auto schedule = make_schedule(manager);
        schedule->init();
        auto state = access::state(*schedule);
        std::weak_ptr<typename decltype(state)::element_type> retained = state;
        failing_module failure;
        manager.add_module("failure", &failure);
        // Prepare all owning call arguments before arming persistent rejection.
        if (manager.start(0, {}, {}, {}))
        {
            exit_probe(2);
        }
        reject_allocations = true;
        state.reset();
        schedule.reset();
        state = retained.lock();
        if (!state || !state->worker)
        {
            exit_probe(2);
        }
        const auto result = manager.cleanup_failed_startup();
        if (result != ark::startup_cleanup_result::done || manager.cleanup_failed_startup() != result ||
            state->worker || !dispatcher_retired(state) || *older != 1 || retained.expired())
        {
            exit_probe(2);
        }
        std::fputs("Schedule startup allocation failure: dispatcher retired; workers joined; candidate gone; exit 1\n",
            stderr);
        exit_probe(1);
    }
    doctest::Context context(argc, argv);
    return context.run();
}
