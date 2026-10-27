// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <thread>

#include "../network/doctest.h"

#include <ark/plugin_manager.hpp>

namespace {

enum class startup_phase : std::size_t
{
    init,
    post_init,
    check_config,
    pre_update,
    none
};

constexpr std::array phase_names{"init", "post_init", "check_config", "pre_update"};

enum class failure_mode
{
    false_result,
    standard_exception,
    unknown_exception
};

class counted_module final : public ark::module
{
public:
    explicit counted_module(
        startup_phase failed_phase = startup_phase::none, failure_mode mode = failure_mode::false_result)
        : failed_phase_(failed_phase)
        , mode_(mode) {
    }

    bool init() override {
        return record(startup_phase::init);
    }

    bool post_init() override {
        return record(startup_phase::post_init);
    }

    bool check_config() override {
        return record(startup_phase::check_config);
    }

    bool pre_update() override {
        return record(startup_phase::pre_update);
    }

    bool update() override {
        ++late_calls;
        return true;
    }

    bool pre_shut() override {
        ++late_calls;
        return true;
    }

    bool shut() override {
        ++late_calls;
        return true;
    }

    std::array<unsigned, phase_names.size()> calls{};
    unsigned late_calls{};

private:
    bool record(startup_phase phase) {
        ++calls[static_cast<std::size_t>(phase)];
        if (phase == failed_phase_)
        {
            if (mode_ == failure_mode::standard_exception)
            {
                throw std::runtime_error("startup module exception");
            }
            if (mode_ == failure_mode::unknown_exception)
            {
                throw 91;
            }
            return false;
        }
        return true;
    }

    startup_phase failed_phase_;
    failure_mode mode_;
};

class unconfigured_logger_scope
{
public:
    unconfigured_logger_scope() {
        ark::logger::set_instance(&logger_);
    }

    ~unconfigured_logger_scope() {
        ark::logger::set_instance(previous_);
    }

private:
    ark::logger* previous_{ark::logger::instance()};
    ark::logger logger_;
};

bool start(ark::plugin_manager& manager) {
    return manager.start(42, {}, {}, {});
}

} // namespace

TEST_CASE("Plugin startup calls each successful module phase once") {
    ark::plugin_manager manager;
    std::array<counted_module, 3> modules;
    for (std::size_t index = 0; index < modules.size(); ++index)
    {
        REQUIRE(manager.add_module("counted-module-" + std::to_string(index), &modules[index]));
    }

    CHECK(manager.get_startup_failure().empty());
    CHECK(start(manager));
    CHECK(manager.get_startup_failure().empty());
    for (const auto& module : modules)
    {
        CHECK(module.calls == std::array<unsigned, 4>{1, 1, 1, 1});
    }
}

TEST_CASE("Plugin startup returns false without repeating a failed phase or running later phases") {
    for (std::size_t failed = 0; failed < phase_names.size(); ++failed)
    {
        CAPTURE(std::string(phase_names[failed]));
        ark::plugin_manager manager;
        counted_module module(static_cast<startup_phase>(failed));
        REQUIRE(manager.add_module("counted-module-single", &module));

        CHECK_FALSE(start(manager));
        CHECK(manager.get_startup_failure() == std::string(phase_names[failed]) + ": counted-module-single");
        CHECK(module.late_calls == 0);
        for (std::size_t phase = 0; phase < phase_names.size(); ++phase)
        {
            CHECK(module.calls[phase] == (phase <= failed ? 1u : 0u));
        }
    }
}

TEST_CASE("Plugin startup stops a phase at its first failure independently of module iteration order") {
    for (std::size_t failed = 0; failed < phase_names.size(); ++failed)
    {
        for (const auto mode : {failure_mode::false_result, failure_mode::standard_exception})
        {
            CAPTURE(std::string(phase_names[failed]));
            ark::plugin_manager manager;
            std::array modules{counted_module(static_cast<startup_phase>(failed), mode),
                counted_module(static_cast<startup_phase>(failed), mode),
                counted_module(static_cast<startup_phase>(failed), mode)};
            for (std::size_t index = 0; index < modules.size(); ++index)
            {
                REQUIRE(manager.add_module("counted-module-" + std::to_string(index), &modules[index]));
            }

            if (mode == failure_mode::false_result)
            {
                CHECK_FALSE(start(manager));
            }
            else
            {
                CHECK_THROWS_WITH_AS(start(manager), "startup module exception", std::runtime_error);
            }
            for (std::size_t phase = 0; phase < phase_names.size(); ++phase)
            {
                unsigned phase_calls = 0;
                for (const auto& module : modules)
                {
                    CHECK(module.calls[phase] <= 1);
                    phase_calls += module.calls[phase];
                    if (phase == failed && module.calls[phase] == 1)
                    {
                        const auto index = static_cast<std::size_t>(&module - modules.data());
                        CHECK(manager.get_startup_failure() ==
                              std::string(phase_names[failed]) + ": counted-module-" + std::to_string(index));
                    }
                }
                const auto expected = phase < failed ? modules.size() : phase == failed ? 1u : 0u;
                CHECK(phase_calls == expected);
            }
        }
    }
}

TEST_CASE("Plugin startup returns false without a configured logger") {
    unconfigured_logger_scope logger;
    ark::plugin_manager manager;
    counted_module module(startup_phase::init);
    REQUIRE(manager.add_module("counted-module-no-logger", &module));

    bool started = true;
    CHECK_NOTHROW(started = start(manager));
    CHECK_FALSE(started);
    CHECK(module.calls == std::array<unsigned, 4>{1, 0, 0, 0});
}

TEST_CASE("Startup exceptions preserve the first module and phase without cleanup or exception translation") {
    for (std::size_t failed = 0; failed < phase_names.size(); ++failed)
    {
        for (const auto mode : {failure_mode::standard_exception, failure_mode::unknown_exception})
        {
            CAPTURE(std::string(phase_names[failed]));
            ark::plugin_manager manager;
            counted_module module(static_cast<startup_phase>(failed), mode);
            REQUIRE(manager.add_module("throwing-module", &module));
            if (mode == failure_mode::standard_exception)
            {
                CHECK_THROWS_WITH_AS(start(manager), "startup module exception", std::runtime_error);
            }
            else
            {
                CHECK_THROWS_AS(start(manager), int);
            }
            CHECK(manager.get_startup_failure() == std::string(phase_names[failed]) + ": throwing-module");
            CHECK(module.late_calls == 0);
            for (std::size_t phase = 0; phase < phase_names.size(); ++phase)
            {
                CHECK(module.calls[phase] == (phase <= failed ? 1u : 0u));
            }
        }
    }
}

TEST_CASE("Nullable module lookup preserves RTTI keys without asserting or mutating registrations") {
    ark::plugin_manager manager;
    counted_module expected;
    ark::module other;
    const auto key = std::string(typeid(counted_module).name());

    CHECK(manager.try_find_module<counted_module>() == nullptr);
    REQUIRE(manager.add_module(key, &expected));
    CHECK(manager.try_find_module<counted_module>() == &expected);
    CHECK(manager.find_module<counted_module>() == &expected);
    manager.remove_module(key);
    CHECK(manager.try_find_module<counted_module>() == nullptr);

    REQUIRE(manager.add_module(key, &other));
    CHECK(manager.try_find_module<counted_module>() == nullptr);
    manager.remove_module(key);
    REQUIRE(manager.add_module(key, &expected));
    CHECK(manager.try_find_module<counted_module>() == &expected);
}

TEST_CASE("Nullable module lookup rejects a foreign thread and preserves owner access") {
    for (const bool registered : {false, true})
    {
        CAPTURE(registered);
        ark::plugin_manager manager;
        counted_module expected;
        if (registered)
        {
            REQUIRE(manager.add_module(typeid(counted_module).name(), &expected));
        }
        bool rejected = false;
        std::thread foreign([&] {
            try
            { (void)manager.try_find_module<counted_module>(); }
            catch (const std::logic_error&)
            { rejected = true; }
        });
        foreign.join();
        CHECK(rejected);
        CHECK(manager.try_find_module<counted_module>() == (registered ? &expected : nullptr));
    }
}
