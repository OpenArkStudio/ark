// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <algorithm>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <vector>

#include "../network/doctest.h"

#include <ark/bus/bus_module.hpp>
#include <ark/module_base.hpp>
#include <ark/plugin_manager.hpp>
#include <ark/time/timer.hpp>
#include <ark/utility/timer_module.hpp>
#include "net/net_plugin.hpp"

namespace {
using namespace std::chrono_literals;

class bus_provider final : public ark::module_base<ark::bus_module>
{
public:
    const std::string get_app_whole_name(ark::bus_id_t) override {
        return {};
    }

    const std::string& get_app_name(const ark::app_type&) override {
        return name_;
    }

    ark::app_type get_app_type(const std::string&) override {
        return {};
    }

    const ark::proc_config& get_self_proc() override {
        return proc_;
    }

    ark::app_type get_self_app_type() override {
        return {};
    }

    ark::bus_id_t get_self_bus_id() override {
        return proc_.bus_id;
    }

    const std::string get_self_bus_name() override {
        return {};
    }

    int combine_bus_id(ark::app_type, std::uint8_t) override {
        return 0;
    }

    const ark::reg_center& get_reg_center() override {
        return registry_;
    }

    bool get_target_bus_relations(std::vector<ark::app_type>&) override {
        if (throw_before_registration)
        {
            throw std::runtime_error("controlled Bus configuration failure");
        }
        return true;
    }

    bool get_other_proc(ark::bus_id_t, ark::proc_config&) const override {
        return false;
    }

    int get_other_proc(ark::app_type, std::vector<const ark::proc_config*>&) const override {
        return 0;
    }

    bool throw_before_registration{};

private:
    std::string name_;
    ark::proc_config proc_;
    ark::reg_center registry_;
};

struct registration
{
    std::uint64_t id;
    std::uint32_t count;
    std::chrono::milliseconds interval;
    std::weak_ptr<int> callback_lifetime;
};

class timer_provider final : public ark::module_base<ark::timer_module>
{
public:
    using ark::timer_module::add_forever_timer;

    timer_provider() {
        wheel_.init(0);
    }

    bool pre_shut() override {
        wheel_.shut();
        return true;
    }

    bool remove_timer(std::uint64_t id) override {
        removals.push_back(id);
        return wheel_.remove_timer(id);
    }

    void pump(std::int64_t now) {
        wheel_.update(now);
    }

    bool reject_registration{};
    unsigned attempts{};
    std::vector<registration> registrations;
    std::vector<std::uint64_t> removals;
    std::vector<std::uint64_t> invoked;

protected:
    std::uint64_t add_single_timer(const ark::guid_t& entity, std::chrono::milliseconds interval, std::uint32_t count,
        ark::timer_functor&& callback) override {
        ++attempts;
        if (reject_registration)
        {
            return 0;
        }
        auto lifetime = std::make_shared<int>(1);
        // The real wheel owns the actual production bound member callback.
        // Expiration proves that callback storage was released, independently
        // of whether the currently empty health_check body touches its receiver.
        ark::timer_functor observed = [this, callback = std::move(callback), lifetime](auto id, const auto& owner) {
            invoked.push_back(id);
            callback(id, owner);
        };
        const auto id = wheel_.add_timer(entity, count, interval, std::move(observed));
        registrations.push_back({id, count, interval, lifetime});
        return id;
    }

    std::uint64_t add_forever_timer(
        const ark::guid_t& entity, std::chrono::milliseconds interval, ark::timer_functor&& callback) override {
        return add_single_timer(entity, interval, 0, std::move(callback));
    }

private:
    ark::time_wheel_manager wheel_;
};

struct receiver
{
    void on_timer(std::uint64_t, const ark::guid_t&) {
        ++calls;
    }

    unsigned calls{};
};

struct fixture
{
    fixture() {
        REQUIRE(manager.add_module(typeid(ark::bus_module).name(), &bus));
        REQUIRE(manager.add_module(typeid(ark::timer_module).name(), &timer));
        network->set_plugin_manager(&manager);
        REQUIRE(manager.add_module(typeid(ark::net_service_manager_module).name(), network.get()));
    }

    ark::plugin_manager manager;
    bus_provider bus;
    timer_provider timer;
    std::unique_ptr<ark::net_service_manager_module> network{ark::detail::make_net_service_manager_module()};
};

} // namespace

TEST_CASE("Net retires only its owned health callback before receiver teardown") {
    fixture state;
    receiver unrelated;
    const auto unrelated_id = state.timer.add_forever_timer(ark::null_guid, 20s, &unrelated, &receiver::on_timer);
    REQUIRE(unrelated_id != 0);
    REQUIRE(state.manager.start(0, {}, {}, {}));
    REQUIRE(state.timer.registrations.size() == 2);
    const auto health = state.timer.registrations.back();
    CHECK(health.count == 0);
    CHECK(health.interval == 20s);
    CHECK_FALSE(health.callback_lifetime.expired());
    // Negative control: both registrations actually execute before retirement.
    state.timer.pump(20010);
    CHECK(std::count(state.timer.invoked.begin(), state.timer.invoked.end(), health.id) == 1);
    CHECK(unrelated.calls == 1);
    state.timer.invoked.clear();
    REQUIRE(state.network->pre_shut());
    REQUIRE(health.callback_lifetime.expired());
    CHECK_FALSE(state.timer.registrations.front().callback_lifetime.expired());
    CHECK(state.timer.removals == std::vector<std::uint64_t>{health.id});
    CHECK(state.network->pre_shut());
    CHECK(state.network->shut());
    CHECK(state.network->shut());
    CHECK(state.timer.removals == std::vector<std::uint64_t>{health.id});
    state.network.reset();
    state.timer.pump(40010);
    CHECK(state.timer.invoked == std::vector<std::uint64_t>{unrelated_id});
    CHECK(unrelated.calls == 2);
    CHECK(state.timer.remove_timer(unrelated_id));
}

TEST_CASE("Net explicit shut keeps health retirement compatibility") {
    fixture state;
    REQUIRE(state.network->init());
    REQUIRE(state.network->post_init());
    REQUIRE(state.timer.registrations.size() == 1);
    const auto health = state.timer.registrations.back();
    REQUIRE(state.network->shut());
    REQUIRE(health.callback_lifetime.expired());
    CHECK(state.network->pre_shut());
    CHECK(state.network->shut());
    CHECK(state.timer.removals == std::vector<std::uint64_t>{health.id});
    state.network.reset();
    state.timer.pump(40010);
    CHECK(state.timer.invoked.empty());
}

TEST_CASE("Net normal manager shutdown retires its health registration once") {
    fixture state;
    REQUIRE(state.manager.start(0, {}, {}, {}));
    REQUIRE(state.timer.registrations.size() == 1);
    const auto health = state.timer.registrations.back();
    CHECK_FALSE(health.callback_lifetime.expired());
    REQUIRE(state.manager.stop(0));
    CHECK(health.callback_lifetime.expired());
    CHECK(state.timer.removals == std::vector<std::uint64_t>{health.id});
    CHECK(state.manager.stop(0));
    CHECK(state.timer.removals == std::vector<std::uint64_t>{health.id});
    state.network.reset();
    state.timer.pump(40010);
    CHECK(state.timer.invoked.empty());
}

TEST_CASE("Net zero health registration result fails real manager startup") {
    fixture state;
    state.timer.reject_registration = true;
    CHECK_FALSE(state.manager.start(0, {}, {}, {}));
    CHECK(state.manager.get_startup_call().owner.data() == std::string(typeid(ark::net_service_manager_module).name()));
    CHECK(state.manager.get_startup_call().phase.data() == std::string("post_init"));
    CHECK(state.manager.get_startup_call().result == ark::startup_call_result::returned_false);
    CHECK(state.timer.attempts == 1);
    CHECK(state.timer.registrations.empty());
    CHECK(state.network->pre_shut());
    CHECK(state.network->shut());
    CHECK(state.timer.removals.empty());
}

TEST_CASE("Net health retirement tolerates no registration and an already cleared provider") {
    {
        auto network = ark::detail::make_net_service_manager_module();
        CHECK(network->pre_shut());
        CHECK(network->shut());
    }
    {
        fixture state;
        REQUIRE(state.network->init());
        state.bus.throw_before_registration = true;
        CHECK_THROWS_AS(state.network->post_init(), std::runtime_error);
        CHECK(state.timer.attempts == 0);
        CHECK(state.network->pre_shut());
        CHECK(state.network->shut());
        CHECK(state.timer.removals.empty());
    }
    {
        fixture state;
        REQUIRE(state.network->init());
        REQUIRE(state.network->post_init());
        const auto health = state.timer.registrations.back();
        REQUIRE(state.timer.pre_shut());
        REQUIRE(health.callback_lifetime.expired());
        CHECK(state.network->pre_shut()); // remove_timer returns false here.
        CHECK(state.network->pre_shut());
        CHECK(state.network->shut());
        CHECK(state.timer.removals == std::vector<std::uint64_t>{health.id});
    }
}

TEST_CASE("Net destructor never uses a Timer provider that has already been destroyed") {
    ark::plugin_manager manager;
    bus_provider bus;
    auto timer = std::make_unique<timer_provider>();
    auto network = ark::detail::make_net_service_manager_module();
    REQUIRE(manager.add_module(typeid(ark::bus_module).name(), &bus));
    REQUIRE(manager.add_module(typeid(ark::timer_module).name(), timer.get()));
    network->set_plugin_manager(&manager);
    REQUIRE(network->init());
    REQUIRE(network->post_init());
    const auto health = timer->registrations.back();
    REQUIRE(timer->pre_shut());
    CHECK(health.callback_lifetime.expired());
    manager.remove_module(typeid(ark::timer_module).name());
    timer.reset();
    network.reset();
    CHECK(health.callback_lifetime.expired());
}
