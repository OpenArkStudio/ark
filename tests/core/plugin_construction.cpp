// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <typeinfo>

#include "../network/doctest.h"

#include <ark/module_base.hpp>
#include <ark/plugin_base.hpp>
#include "module_factory.hpp"

namespace {

using construction_test::counts;
using construction_test::make_service;
using construction_test::service;

class test_plugin final : public ark::plugin_base<test_plugin>
{
public:
    using plugin_base::register_module;
    using plugin_base::unregister_module;

    void install() override {
    }

    void uninstall() override {
        unregister_module<service>();
    }
};

class other_plugin final : public ark::plugin_base<other_plugin>
{
public:
    using plugin_base::register_module;
    using plugin_base::unregister_module;

    void install() override {
    }

    void uninstall() override {
    }
};

static_assert(!std::is_copy_constructible_v<test_plugin>);
static_assert(!std::is_copy_assignable_v<test_plugin>);
static_assert(!std::is_move_constructible_v<test_plugin>);
static_assert(!std::is_move_assignable_v<test_plugin>);

class throwing_service final : public service
{
public:
    explicit throwing_service(counts& events)
        : events_(events) {
    }

    ~throwing_service() override {
        ++events_.destroyed;
    }

    void set_name(const std::string&) override {
        throw std::runtime_error("metadata rejected");
    }

    service* lookup_self() override {
        return this;
    }

private:
    counts& events_;
};

struct metadata_counts : counts
{
    unsigned manager_assignments{};
    unsigned name_assignments{};
};

class metadata_service final : public ark::module_base<service>
{
public:
    explicit metadata_service(metadata_counts& events)
        : events_(events) {
    }

    ~metadata_service() override {
        ++events_.destroyed;
    }

    void set_plugin_manager(ark::plugin_manager* manager) override {
        ++events_.manager_assignments;
        module_base::set_plugin_manager(manager);
    }

    void set_name(const std::string& name) override {
        ++events_.name_assignments;
        module_base::set_name(name);
    }

    service* lookup_self() override {
        return find_module<service>();
    }

private:
    metadata_counts& events_;
};

class registration_guard final : public ark::module_base<ark::module>
{
public:
    registration_guard(test_plugin& owner, metadata_counts& events)
        : owner_(owner)
        , events_(events) {
    }

    bool update() override {
        CHECK_THROWS_AS(
            owner_.register_module<service>(std::make_unique<metadata_service>(events_), ark::module_update::enabled),
            std::logic_error);
        CHECK_THROWS_AS(owner_.unregister_module<service>(), std::logic_error);
        ++guarded_updates;
        return true;
    }

    unsigned guarded_updates{};

private:
    test_plugin& owner_;
    metadata_counts& events_;
};

class guarded_service final : public ark::module_base<service>
{
public:
    explicit guarded_service(test_plugin& owner)
        : owner_(owner) {
    }

    service* lookup_self() override {
        return find_module<service>();
    }

    bool update() override {
        CHECK_THROWS_AS(owner_.unregister_module<service>(), std::logic_error);
        ++guarded_updates;
        return true;
    }

    unsigned guarded_updates{};

private:
    test_plugin& owner_;
};

ark::task<> accepted_work() {
    co_return;
}

} // namespace

TEST_CASE("Typed private factories preserve interface identity and explicit update admission") {
    for (const auto mode : {ark::module_update::disabled, ark::module_update::enabled})
    {
        ark::plugin_manager manager;
        test_plugin owner;
        owner.set_plugin_manager(&manager);
        counts events;
        auto candidate = make_service(events);
        auto* expected = candidate.get();
        REQUIRE(owner.register_module(std::move(candidate), mode));
        CHECK(manager.find_module<service>() == expected);
        CHECK(expected->get_plugin_manager() == &manager);
        CHECK(expected->get_name() == typeid(service).name());
        CHECK(expected->lookup_self() == expected);
        CHECK(manager.update(1));
        CHECK(events.updates == (mode == ark::module_update::enabled ? 1 : 0));
        CHECK(owner.unregister_module<service>());
        CHECK_FALSE(owner.unregister_module<service>());
        CHECK(manager.update(2));
        CHECK(events.destroyed == 1);
        CHECK(events.updates == (mode == ark::module_update::enabled ? 1 : 0));
    }
}

TEST_CASE("Plugin identity version and configuration retain per-derived legacy behavior") {
    test_plugin first, same_type;
    other_plugin other;
    first.set_plugin_conf("first.toml");
    other.set_plugin_conf("other.toml");
    CHECK(first.get_plugin_version() == 0);
    CHECK(first.get_plugin_name() == typeid(test_plugin).name());
    CHECK(same_type.get_plugin_conf() == "first.toml");
    CHECK(other.get_plugin_conf() == "other.toml");
    same_type.set_plugin_conf("replacement.toml");
    CHECK(first.get_plugin_conf() == "replacement.toml");
    CHECK(other.get_plugin_conf() == "other.toml");
}

TEST_CASE("Typed construction rejects foreign threads before candidate metadata and plugin bookkeeping") {
    ark::plugin_manager manager;
    test_plugin owner;
    owner.set_plugin_manager(&manager);
    metadata_counts rejected;
    bool registration_rejected{}, empty_removal_rejected{};
    std::exception_ptr unexpected;
    std::thread worker([&] {
        try
        { owner.register_module<service>(std::make_unique<metadata_service>(rejected), ark::module_update::enabled); }
        catch (const std::logic_error&)
        { registration_rejected = true; }
        catch (...)
        { unexpected = std::current_exception(); }
        try
        { owner.unregister_module<service>(); }
        catch (const std::logic_error&)
        { empty_removal_rejected = true; }
        catch (...)
        { unexpected = std::current_exception(); }
    });
    worker.join();
    CHECK(unexpected == nullptr);
    CHECK(registration_rejected);
    CHECK(empty_removal_rejected);
    CHECK(rejected.manager_assignments == 0);
    CHECK(rejected.name_assignments == 0);
    CHECK(rejected.destroyed == 1);
    CHECK_FALSE(owner.unregister_module<service>());

    counts accepted;
    REQUIRE(owner.register_module(make_service(accepted), ark::module_update::enabled));
    CHECK(manager.update(1));
    CHECK(accepted.updates == 1);
    CHECK(owner.unregister_module<service>());
    CHECK(accepted.destroyed == 1);
    CHECK(manager.update(2));
    CHECK(accepted.updates == 1);
}

TEST_CASE("Typed construction rejects update callbacks before metadata and missing identity lookups") {
    ark::plugin_manager manager;
    test_plugin owner;
    owner.set_plugin_manager(&manager);
    metadata_counts rejected;
    auto guard = std::make_unique<registration_guard>(owner, rejected);
    auto* active = guard.get();
    REQUIRE(owner.register_module<ark::module>(std::move(guard), ark::module_update::enabled));
    CHECK(manager.update(1));
    CHECK(active->guarded_updates == 1);
    CHECK(rejected.manager_assignments == 0);
    CHECK(rejected.name_assignments == 0);
    CHECK(rejected.destroyed == 1);
    CHECK_FALSE(owner.unregister_module<service>());
    REQUIRE(owner.unregister_module<ark::module>());

    counts accepted;
    REQUIRE(owner.register_module(make_service(accepted), ark::module_update::enabled));
    CHECK(manager.update(2));
    CHECK(accepted.updates == 1);
    CHECK(owner.unregister_module<service>());
    CHECK(accepted.destroyed == 1);
    CHECK(manager.update(3));
    CHECK(accepted.updates == 1);
}

TEST_CASE("Rejected factories leave no dangling registration and destroy candidates once") {
    ark::plugin_manager manager;
    test_plugin owner;
    other_plugin other;
    owner.set_plugin_manager(&manager);
    other.set_plugin_manager(&manager);
    counts original, duplicate, cross_owner, metadata, conflict, retried;
    CHECK_FALSE(owner.register_module<service>(nullptr, ark::module_update::disabled));
    CHECK_THROWS_AS(
        owner.register_module<service>(std::make_unique<throwing_service>(metadata), ark::module_update::enabled),
        std::runtime_error);
    CHECK(metadata.destroyed == 1);
    auto candidate = make_service(original);
    auto* expected = candidate.get();
    REQUIRE(owner.register_module(std::move(candidate), ark::module_update::enabled));
    CHECK_FALSE(owner.register_module(make_service(duplicate), ark::module_update::enabled));
    CHECK_FALSE(other.register_module(make_service(cross_owner), ark::module_update::disabled));
    CHECK(duplicate.destroyed == 1);
    CHECK(cross_owner.destroyed == 1);
    CHECK(manager.find_module<service>() == expected);
    CHECK(manager.update(1));
    CHECK(original.updates == 1);
    REQUIRE(owner.unregister_module<service>());
    CHECK(original.destroyed == 1);

    // The legacy API permits an update observer without a module entry.
    auto orphan = make_service(conflict);
    orphan->set_name(typeid(service).name());
    REQUIRE(manager.add_update_module(orphan.get()));
    CHECK_FALSE(owner.register_module(make_service(retried), ark::module_update::disabled));
    CHECK(retried.destroyed == 1);
    manager.remove_update_module(typeid(service).name());
    REQUIRE(owner.register_module(make_service(retried), ark::module_update::disabled));
    CHECK(owner.unregister_module<service>());
    CHECK(retried.destroyed == 2);
}

TEST_CASE("Typed unregister retains ownership while managed work borrows a module") {
    ark::plugin_manager manager;
    test_plugin owner;
    owner.set_plugin_manager(&manager);
    counts events;
    REQUIRE(owner.register_module(make_service(events), ark::module_update::enabled));
    auto* instance = manager.find_module<service>();
    auto& tasks = manager.create_task_group(*instance);
    REQUIRE(tasks.start(accepted_work()) != 0);
    CHECK_THROWS_AS(owner.unregister_module<service>(), std::logic_error);
    CHECK(events.destroyed == 0);
    CHECK(manager.find_module<service>() == instance);
    CHECK(manager.update(1));
    CHECK(manager.pending_tasks() == 0);
    CHECK(manager.stop(2));
    CHECK(events.destroyed == 1);
    CHECK(owner.unregister_module<service>());
    CHECK_FALSE(owner.unregister_module<service>());
    CHECK(events.destroyed == 1);
    counts rejected;
    CHECK_THROWS_AS(owner.register_module(make_service(rejected), ark::module_update::disabled), std::logic_error);
    CHECK(rejected.destroyed == 1);
}

TEST_CASE("Plugin destruction alone preserves legacy explicit uninstall ownership") {
    ark::plugin_manager manager;
    counts events;
    service* retained{};
    {
        test_plugin owner;
        owner.set_plugin_manager(&manager);
        REQUIRE(owner.register_module(make_service(events), ark::module_update::disabled));
        retained = manager.find_module<service>();
    }
    CHECK(events.destroyed == 0);
    CHECK(manager.find_module<service>() == retained);
    // A dead standalone plugin leaves the explicit caller-owned path intact.
    // Shutdown must not call the old plugin's destruction callback.
    CHECK(manager.stop(1));
    CHECK(events.destroyed == 0);
    manager.remove_module(typeid(service).name());
    delete retained;
    CHECK(events.destroyed == 1);
}

TEST_CASE("Typed removal respects active callbacks and borrowed registry identity") {
    ark::plugin_manager manager;
    test_plugin owner;
    owner.set_plugin_manager(&manager);
    auto guarded = std::make_unique<guarded_service>(owner);
    auto* active = guarded.get();
    REQUIRE(owner.register_module<service>(std::move(guarded), ark::module_update::enabled));
    CHECK(manager.update(1));
    CHECK(active->guarded_updates == 1);
    CHECK(manager.find_module<service>() == active);
    REQUIRE(owner.unregister_module<service>());

    counts original, replacement;
    REQUIRE(owner.register_module(make_service(original), ark::module_update::disabled));
    auto* retained = manager.find_module<service>();
    manager.remove_module(typeid(service).name());
    auto foreign = make_service(replacement);
    foreign->set_name(typeid(service).name());
    REQUIRE(manager.add_module(typeid(service).name(), foreign.get()));
    CHECK_FALSE(owner.unregister_module<service>());
    CHECK(original.destroyed == 0);
    CHECK(replacement.destroyed == 0);
    CHECK(manager.find_module<service>() == foreign.get());
    manager.remove_module(typeid(service).name());
    REQUIRE(manager.add_module(typeid(service).name(), retained));
    CHECK(owner.unregister_module<service>());
    CHECK(original.destroyed == 1);
}
