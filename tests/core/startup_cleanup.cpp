// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
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

TEST_CASE("Startup cleanup owns one capsule before acquisition and runs once after each failed phase") {
    for (unsigned phase{}; phase < 4; ++phase)
    {
        for (bool throws : {false, true})
        {
            ark::plugin_manager manager;
            auto context = std::make_shared<owned_wheel>();
            context->manager = &manager;
            REQUIRE(manager.prepare_startup_cleanup("private-wheel", "test-executable", context, release_wheel));
            CHECK_FALSE(manager.prepare_startup_cleanup("other", "test-executable", context, release_wheel));
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
    CHECK_FALSE(manager.prepare_startup_cleanup("wheel", "test", {}, release_wheel));
    CHECK_FALSE(manager.prepare_startup_cleanup("wheel", "test", context, nullptr));
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
