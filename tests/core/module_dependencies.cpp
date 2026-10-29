// Copyright (c) 2026 OpenArkStudio. SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "../network/doctest.h"

#include <ark/module_base.hpp>
#include <ark/plugin_manager.hpp>

namespace {

constexpr std::array dependency_phases{"init", "post_init", "check_config", "pre_update"};

class dependency_module final : public ark::module
{
public:
    dependency_module(std::string name, std::vector<std::string> dependencies, std::vector<std::string>& events)
        : name(std::move(name))
        , dependencies(std::move(dependencies))
        , events(events) {
    }

    std::vector<std::string> get_dependencies() const override {
        if (metadata_throws)
        {
            throw std::runtime_error("dependency metadata failed");
        }
        return dependencies;
    }

    bool init() override {
        return record(0);
    }

    bool post_init() override {
        return record(1);
    }

    bool check_config() override {
        return record(2);
    }

    bool pre_update() override {
        return record(3);
    }

    std::string name;
    std::vector<std::string> dependencies;
    std::vector<std::string>& events;
    int failed_phase = -1;
    bool throws = false;
    bool metadata_throws = false;

private:
    bool record(int phase) {
        events.emplace_back(std::string(dependency_phases[phase]) + ":" + name);
        if (phase == failed_phase)
        {
            if (throws)
            {
                throw std::runtime_error("ordered startup failed");
            }
            return false;
        }
        return true;
    }
};

bool start_dependencies(ark::plugin_manager& manager) {
    return manager.start(0, {}, {}, {});
}

std::vector<std::string> phase_events(const std::vector<std::string>& order) {
    std::vector<std::string> events;
    for (const auto* phase : dependency_phases)
    {
        for (const auto& name : order)
        {
            events.emplace_back(std::string(phase) + ":" + name);
        }
    }
    return events;
}

class dependency_provider : public ark::module
{
public:
    bool initialized{};
};

class dependency_provider_impl final : public ark::module_base<dependency_provider>
{
public:
    bool init() override {
        initialized = true;
        return true;
    }
};

class dependency_consumer : public ark::module
{
};

class dependency_consumer_impl final : public ark::module_base<dependency_consumer, dependency_provider>
{
public:
    bool init() override {
        return find_module<dependency_provider>()->initialized;
    }
};

} // namespace

TEST_CASE("Typed module dependencies preserve interface registration keys and default independent nodes") {
    ark::module undeclared;
    CHECK(undeclared.get_dependencies().empty());
    dependency_provider_impl provider;
    dependency_consumer_impl consumer;
    CHECK(provider.get_dependencies().empty());
    CHECK(consumer.get_dependencies() == std::vector<std::string>{typeid(dependency_provider).name()});
    ark::plugin_manager manager;
    provider.set_plugin_manager(&manager);
    consumer.set_plugin_manager(&manager);
    REQUIRE(manager.add_module(typeid(dependency_consumer).name(), &consumer));
    REQUIRE(manager.add_module(typeid(dependency_provider).name(), &provider));
    CHECK(start_dependencies(manager));
    CHECK(manager.find_module<dependency_provider>() == &provider);
    CHECK(manager.find_module<dependency_consumer>() == &consumer);
}

TEST_CASE("Module dependencies reject missing providers before any startup hook") {
    ark::plugin_manager manager;
    std::vector<std::string> events;
    dependency_module consumer("consumer", {"missing"}, events);
    REQUIRE(manager.add_module(consumer.name, &consumer));
    CHECK_FALSE(start_dependencies(manager));
    CHECK(manager.get_startup_failure() == "dependencies: consumer requires missing module 'missing'");
    CHECK(events.empty());
}

TEST_CASE("Module dependencies reject self and indirect cycles before independent modules start") {
    for (const bool self : {false, true})
    {
        ark::plugin_manager manager;
        std::vector<std::string> events;
        dependency_module first("first", {self ? "first" : "second"}, events);
        dependency_module second("second", {"first"}, events);
        dependency_module independent("independent", {}, events);
        REQUIRE(manager.add_module(second.name, &second));
        REQUIRE(manager.add_module(independent.name, &independent));
        REQUIRE(manager.add_module(first.name, &first));
        CHECK_FALSE(start_dependencies(manager));
        CHECK(manager.get_startup_failure() == "dependencies: cycle prevents startup; blocked modules: first, second");
        CHECK(events.empty());
    }
}

TEST_CASE("Module dependencies order transitive diamond providers once in every startup phase") {
    for (const bool reverse : {false, true})
    {
        ark::plugin_manager manager;
        std::vector<std::string> events;
        dependency_module root("root", {}, events);
        dependency_module left("left", {"root"}, events);
        dependency_module right("right", {"root", "root"}, events);
        dependency_module sink("sink", {"right", "left"}, events);
        std::array modules{&root, &left, &right, &sink};
        if (reverse)
        {
            std::reverse(modules.begin(), modules.end());
        }
        for (auto* module : modules)
        {
            REQUIRE(manager.add_module(module->name, module));
        }
        REQUIRE(start_dependencies(manager));
        CHECK(events == phase_events({"root", "left", "right", "sink"}));
        CHECK(manager.get_startup_failure().empty());
    }
}

TEST_CASE("Module dependencies use stable registration-key order for independent ready nodes") {
    for (const bool reverse : {false, true})
    {
        ark::plugin_manager manager;
        std::vector<std::string> events;
        dependency_module first("alpha", {}, events);
        dependency_module second("middle", {}, events);
        dependency_module third("omega", {}, events);
        std::array modules{&first, &second, &third};
        if (reverse)
        {
            std::reverse(modules.begin(), modules.end());
        }
        for (auto* module : modules)
        {
            REQUIRE(manager.add_module(module->name, module));
        }
        REQUIRE(start_dependencies(manager));
        CHECK(events == phase_events({"alpha", "middle", "omega"}));
    }
}

TEST_CASE("Module dependencies stop ordered startup at its first false or exception") {
    for (int phase = 0; phase < static_cast<int>(dependency_phases.size()); ++phase)
    {
        for (const bool throws : {false, true})
        {
            ark::plugin_manager manager;
            std::vector<std::string> events;
            dependency_module provider("provider", {}, events);
            dependency_module failing("failing", {"provider"}, events);
            dependency_module later("later", {"failing"}, events);
            failing.failed_phase = phase;
            failing.throws = throws;
            for (auto* module : {&later, &failing, &provider})
            {
                REQUIRE(manager.add_module(module->name, module));
            }
            if (throws)
            {
                CHECK_THROWS_WITH_AS(start_dependencies(manager), "ordered startup failed", std::runtime_error);
            }
            else
            {
                CHECK_FALSE(start_dependencies(manager));
            }
            auto expected = phase_events({"provider", "failing", "later"});
            expected.resize(static_cast<std::size_t>(phase) * 3 + 2);
            CHECK(events == expected);
            CHECK(manager.get_startup_failure() == std::string(dependency_phases[phase]) + ": failing");
        }
    }
}

TEST_CASE("Module dependency metadata exceptions propagate before initialization") {
    ark::plugin_manager manager;
    std::vector<std::string> events;
    dependency_module invalid("invalid", {}, events);
    invalid.metadata_throws = true;
    REQUIRE(manager.add_module(invalid.name, &invalid));
    CHECK_THROWS_WITH_AS(start_dependencies(manager), "dependency metadata failed", std::runtime_error);
    CHECK(manager.get_startup_failure() == "dependencies: invalid");
    CHECK(events.empty());
}
