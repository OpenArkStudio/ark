// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <array>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

#include "../network/doctest.h"

#include <ark/plugin_manager.hpp>
#include <ark/time/timer.hpp>

namespace {

struct owned_wheel
{
    std::unique_ptr<ark::time_wheel_manager> wheel;
    unsigned releases{};
    ark::startup_cleanup_result result{ark::startup_cleanup_result::done};
    bool throw_release{};
    ark::plugin_manager* manager{};
};

ark::startup_cleanup_result release_wheel(void* pointer) {
    auto& context = *static_cast<owned_wheel*>(pointer);
    ++context.releases;
    if (context.manager)
    {
        CHECK(context.manager->cleanup_failed_startup() == ark::startup_cleanup_result::unsafe);
        CHECK_FALSE(context.manager->prepare_startup_cleanup("reentrant", "test", {}, release_wheel));
        CHECK_THROWS_AS(context.manager->update(1), std::logic_error);
        CHECK_THROWS_AS(context.manager->add_module("forbidden", nullptr), std::logic_error);
    }
    if (context.throw_release)
    {
        throw 42;
    }
    if (context.result != ark::startup_cleanup_result::done)
    {
        return context.result;
    }
    if (context.wheel)
    {
        context.wheel->shut();
        context.wheel.reset();
    }
    return context.result;
}

class wheel_module : public ark::module
{
public:
    wheel_module(std::shared_ptr<owned_wheel> context, unsigned phase, bool throws)
        : context_(std::move(context))
        , failed_phase_(phase)
        , throws_(throws) {
        context_->wheel = std::make_unique<ark::time_wheel_manager>();
        context_->wheel->init(1);
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
        ++normal_stop;
        return true;
    }

    bool shut() override {
        ++normal_stop;
        return true;
    }

    unsigned normal_stop{};
    unsigned calls{};

private:
    bool step(unsigned phase) {
        ++calls;
        if (phase == failed_phase_)
        {
            if (throws_)
            {
                throw std::runtime_error("private wheel startup failure");
            }
            return false;
        }
        return true;
    }

    std::shared_ptr<owned_wheel> context_;
    unsigned failed_phase_;
    bool throws_;
};

} // namespace

TEST_CASE("Startup cleanup owns capsules before acquisition and runs once after each failed phase") {
    for (unsigned phase{}; phase < 4; ++phase)
    {
        for (bool throws : {false, true})
        {
            ark::plugin_manager manager;
            auto context = std::make_shared<owned_wheel>();
            context->manager = &manager;
            REQUIRE(manager.prepare_startup_cleanup("private-wheel", "test-executable", context, release_wheel));
            CHECK_FALSE(manager.prepare_startup_cleanup("private-wheel", "other-code-owner", context, release_wheel));
            wheel_module module(context, phase, throws);
            REQUIRE(context->wheel->initialized());
            REQUIRE(manager.add_module("private-wheel", &module));
            auto& tasks = manager.create_task_group(module);
            if (throws)
            {
                CHECK_THROWS_AS(manager.start(1, {}, {}, {}), std::runtime_error);
            }
            else
            {
                CHECK_FALSE(manager.start(1, {}, {}, {}));
            }
            CHECK(context->releases == 0);
            CHECK(manager.get_startup_call().owner.data() == std::string("private-wheel"));
            CHECK(manager.get_startup_call().result ==
                  (throws ? ark::startup_call_result::exception : ark::startup_call_result::returned_false));
            CHECK(manager.cleanup_failed_startup() == ark::startup_cleanup_result::done);
            CHECK(context->wheel == nullptr);
            CHECK(context->releases == 1);
            CHECK(manager.cleanup_failed_startup() == ark::startup_cleanup_result::done);
            CHECK(context->releases == 1);
            CHECK_FALSE(manager.update(1));
            CHECK(tasks.closed());
            CHECK_FALSE(manager.stop(1));
            CHECK_THROWS_AS(manager.remove_module("private-wheel"), std::logic_error);
            CHECK(module.calls == phase + 1);
            CHECK(module.normal_stop == 0);
            CHECK_THROWS_AS(manager.start(1, {}, {}, {}), std::logic_error);
            CHECK_FALSE(manager.prepare_startup_cleanup("later", "test-executable", context, release_wheel));
        }
    }
}

TEST_CASE("Startup cleanup rejects unsafe enrollment and only disarms on success") {
    ark::plugin_manager manager;
    auto context = std::make_shared<owned_wheel>();
    CHECK_FALSE(manager.prepare_startup_cleanup("", "test", context, release_wheel));
    CHECK_FALSE(manager.prepare_startup_cleanup(std::string(96, 'x'), "test", context, release_wheel));
    CHECK_FALSE(manager.prepare_startup_cleanup("wheel", "", context, release_wheel));
    CHECK_FALSE(manager.prepare_startup_cleanup("wheel", std::string(96, 'x'), context, release_wheel));
    CHECK_FALSE(manager.prepare_startup_cleanup(std::string_view("a\0b", 3), "test", context, release_wheel));
    CHECK_FALSE(manager.prepare_startup_cleanup("wheel", std::string_view("a\0b", 3), context, release_wheel));
    CHECK_FALSE(manager.prepare_startup_cleanup("wheel", "test", {}, release_wheel));
    CHECK_FALSE(manager.prepare_startup_cleanup("wheel", "test", context, nullptr));
    CHECK(context.use_count() == 1);
    bool foreign{};
    std::thread thread([&] { foreign = manager.prepare_startup_cleanup("wheel", "test", context, release_wheel); });
    thread.join();
    CHECK_FALSE(foreign);
    REQUIRE(manager.prepare_startup_cleanup("wheel", "test", context, release_wheel));
    CHECK(manager.cleanup_failed_startup() == ark::startup_cleanup_result::unsafe);
    wheel_module module(context, 4, false);
    REQUIRE(manager.add_module("wheel", &module));
    REQUIRE(manager.start(1, {}, {}, {}));
    CHECK(manager.cleanup_failed_startup() == ark::startup_cleanup_result::unsafe);
    CHECK(context->releases == 0);
    CHECK(context->wheel->initialized());
    context->wheel->shut();
}

TEST_CASE("Unsafe and throwing startup cleanup retain their capsule and never retry") {
    for (const auto result : {ark::startup_cleanup_result::unsafe, ark::startup_cleanup_result::failed,
             ark::startup_cleanup_result::not_run})
    {
        for (bool throws : {false, true})
        {
            ark::plugin_manager manager;
            auto context = std::make_shared<owned_wheel>();
            context->result = result;
            context->throw_release = throws;
            REQUIRE(manager.prepare_startup_cleanup("wheel", "test", context, release_wheel));
            wheel_module module(context, 0, false);
            REQUIRE(manager.add_module("wheel", &module));
            REQUIRE_FALSE(manager.start(1, {}, {}, {}));
            const auto expected =
                throws || result == ark::startup_cleanup_result::not_run ? ark::startup_cleanup_result::failed : result;
            CHECK(manager.cleanup_failed_startup() == expected);
            CHECK(manager.cleanup_failed_startup() == expected);
            CHECK(context->releases == 1);
            CHECK(context->wheel->initialized());
            context->wheel->shut();
        }
    }
}

namespace {

struct cleanup_trace
{
    std::array<unsigned, ark::startup_cleanup_capacity> calls{};
    std::size_t count{};
};

struct traced_context
{
    cleanup_trace* trace{};
    unsigned id{};
    ark::startup_cleanup_result result{ark::startup_cleanup_result::done};
    bool throws{};
};

ark::startup_cleanup_result trace_cleanup(void* pointer) {
    auto& context = *static_cast<traced_context*>(pointer);
    auto& trace = *context.trace;
    if (trace.count < trace.calls.size())
    {
        trace.calls[trace.count] = context.id;
    }
    ++trace.count;
    if (context.throws)
    {
        throw 17;
    }
    return context.result;
}

class startup_failure_module final : public ark::module
{
public:
    bool init() override {
        return false;
    }
};

constexpr std::array<const char*, ark::startup_cleanup_capacity> resource_keys{"resource-0", "resource-1", "resource-2",
    "resource-3", "resource-4", "resource-5", "resource-6", "resource-7", "resource-8", "resource-9", "resource-10",
    "resource-11", "resource-12", "resource-13", "resource-14", "resource-15"};

} // namespace

TEST_CASE("Startup cleanup accepts sixteen independent owners and rejects without retaining ownership") {
    static_assert(ark::startup_cleanup_capacity == 16);
    cleanup_trace trace;
    std::array<std::weak_ptr<traced_context>, ark::startup_cleanup_capacity> retained;
    ark::plugin_manager manager;
    startup_failure_module module;
    REQUIRE(manager.add_module("failure", &module));
    for (std::size_t index{}; index < resource_keys.size(); ++index)
    {
        auto context = std::make_shared<traced_context>();
        context->trace = &trace;
        context->id = static_cast<unsigned>(index);
        retained[index] = context;
        REQUIRE(manager.prepare_startup_cleanup(resource_keys[index], "same-plugin", context, trace_cleanup));
        CHECK(context.use_count() == 2);
        auto rejected = std::make_shared<traced_context>();
        CHECK_FALSE(manager.prepare_startup_cleanup(resource_keys[index], "different-plugin", rejected, trace_cleanup));
        CHECK(rejected.use_count() == 1);
    }
    auto overflow = std::make_shared<traced_context>();
    CHECK_FALSE(manager.prepare_startup_cleanup("overflow", "same-plugin", overflow, trace_cleanup));
    CHECK(overflow.use_count() == 1);
    REQUIRE_FALSE(manager.start(1, {}, {}, {}));
    CHECK_FALSE(manager.prepare_startup_cleanup("late", "same-plugin", overflow, trace_cleanup));
    CHECK(overflow.use_count() == 1);
    CHECK(manager.cleanup_failed_startup() == ark::startup_cleanup_result::done);
    REQUIRE(trace.count == ark::startup_cleanup_capacity);
    for (std::size_t index{}; index < trace.count; ++index)
    {
        CHECK(trace.calls[index] == ark::startup_cleanup_capacity - index - 1);
        CHECK_FALSE(retained[index].expired());
    }
    CHECK(manager.cleanup_failed_startup() == ark::startup_cleanup_result::done);
    CHECK(trace.count == ark::startup_cleanup_capacity);
}

TEST_CASE("Startup cleanup stops at the first incomplete newer owner and retains all contexts") {
    for (const auto result : {ark::startup_cleanup_result::unsafe, ark::startup_cleanup_result::failed,
             ark::startup_cleanup_result::not_run})
    {
        for (bool throws : {false, true})
        {
            cleanup_trace trace;
            ark::plugin_manager manager;
            startup_failure_module module;
            REQUIRE(manager.add_module("failure", &module));
            std::array<std::weak_ptr<traced_context>, 3> retained;
            for (unsigned index{}; index < retained.size(); ++index)
            {
                auto context = std::make_shared<traced_context>();
                context->trace = &trace;
                context->id = index;
                if (index == 1)
                {
                    context->result = result;
                    context->throws = throws;
                }
                retained[index] = context;
                REQUIRE(manager.prepare_startup_cleanup(resource_keys[index], "plugin", context, trace_cleanup));
            }
            REQUIRE_FALSE(manager.start(1, {}, {}, {}));
            const auto expected =
                throws || result == ark::startup_cleanup_result::not_run ? ark::startup_cleanup_result::failed : result;
            CHECK(manager.cleanup_failed_startup() == expected);
            CHECK(manager.cleanup_failed_startup() == expected);
            REQUIRE(trace.count == 2);
            CHECK(trace.calls[0] == 2);
            CHECK(trace.calls[1] == 1);
            for (const auto& context : retained)
            {
                CHECK_FALSE(context.expired());
            }
        }
    }
}

TEST_CASE("Successful startup disarms all owners and permits no late enrollment") {
    cleanup_trace trace;
    ark::plugin_manager manager;
    std::array<std::shared_ptr<traced_context>, 3> contexts;
    for (std::size_t index{}; index < contexts.size(); ++index)
    {
        contexts[index] = std::make_shared<traced_context>();
        contexts[index]->trace = &trace;
        REQUIRE(manager.prepare_startup_cleanup(resource_keys[index], "plugin", contexts[index], trace_cleanup));
        CHECK(contexts[index].use_count() == 2);
    }
    REQUIRE(manager.start(1, {}, {}, {}));
    for (const auto& context : contexts)
    {
        CHECK(context.use_count() == 1);
    }
    CHECK(trace.count == 0);
    CHECK(manager.cleanup_failed_startup() == ark::startup_cleanup_result::unsafe);
    CHECK_FALSE(manager.prepare_startup_cleanup("late", "plugin", contexts[0], trace_cleanup));
}

TEST_CASE("Maximum startup cleanup keys are accepted and empty registries remain one shot") {
    auto context = std::make_shared<owned_wheel>();
    ark::plugin_manager manager;
    const std::string maximum_key(95, 'x');
    REQUIRE(manager.prepare_startup_cleanup(maximum_key, maximum_key, context, release_wheel));
    startup_failure_module module;
    REQUIRE(manager.add_module("failure", &module));
    REQUIRE_FALSE(manager.start(1, {}, {}, {}));
    CHECK(manager.cleanup_failed_startup() == ark::startup_cleanup_result::done);
    CHECK(context->releases == 1);

    ark::plugin_manager empty;
    REQUIRE(empty.add_module("failure", &module));
    REQUIRE_FALSE(empty.start(1, {}, {}, {}));
    CHECK(empty.cleanup_failed_startup() == ark::startup_cleanup_result::not_run);
    CHECK(empty.cleanup_failed_startup() == ark::startup_cleanup_result::not_run);
    CHECK_FALSE(empty.prepare_startup_cleanup("late", "plugin", context, release_wheel));
}
