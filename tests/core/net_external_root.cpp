// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <chrono>
#include <functional>
#include <map>
#include <string>
#include <thread>

#include "../network/doctest.h"

#include <ark/net/external_root_callback.hpp>

namespace {

// Contract recorder: invokes the callback installed through the real public
// registration interface. Actual TCP/DSO dispatch is covered by check-net-plugin.
class recording_server final : public ark::net_server_service
{
public:
    bool start(std::shared_ptr<const ark::msg_header>, ark::bus_id_t, const ark::endpoint&, uint8_t, uint32_t,
        size_t) override {
        return false;
    }

    bool update() override {
        return true;
    }

    std::shared_ptr<ark::net> get_net() override {
        return {};
    }

    bool reg_msg_callback(ark::msg_id_t id, ark::net_msg_functor&& callback) override {
        ++registrations;
        return callbacks.emplace(id, std::move(callback)).second;
    }

    bool reg_forward_msg_callback(ark::net_msg_functor&&) override {
        return false;
    }

    bool reg_net_event_callback(ark::net_event_functor&&) override {
        return false;
    }

    bool reg_reg_server_call_back(ark::net_msg_session_functor&&) override {
        return false;
    }

    void set_timeout_interval(int32_t) override {
    }

    void dispatch(ark::msg_id_t id, const ark::net_msg* message, ark::conv_id_t session) {
        callbacks.at(id)(message, session);
    }

    unsigned registrations{};
    std::map<ark::msg_id_t, ark::net_msg_functor> callbacks;
};

class lifecycle_probe final : public ark::module
{
public:
    const std::string& get_name() const override {
        static const std::string name = "root-admission-probe";
        return name;
    }

    bool stop_accepting() override {
        if (on_stop)
        {
            on_stop();
        }
        return stop_result;
    }

    bool ready_to_shut() const override {
        return ready;
    }

    bool update() override {
        if (on_update)
        {
            on_update();
        }
        return true;
    }

    bool ready{};
    bool stop_result{true};
    std::function<void()> on_stop;
    std::function<void()> on_update;
};

struct message_owner
{
    message_owner()
        : message(ark::net_msg::alloc_msg(new ark::s_msg_header)) {
    }

    ~message_owner() {
        ark::net_msg::release(message);
    }

    ark::net_msg* message;
};

using state = ark::plugin_manager::shutdown_state;

} // namespace

TEST_CASE("External-root contract validates both handlers before registration and retains duplicate behavior") {
    ark::plugin_manager manager;
    recording_server service;
    message_owner message;
    REQUIRE(message.message != nullptr);
    unsigned accepted{}, rejected{};
    auto accept = [&](const ark::net_msg*, ark::conv_id_t) { ++accepted; };
    auto reject = [&](const ark::net_msg*, ark::conv_id_t) { ++rejected; };
    CHECK_FALSE(ark::reg_external_root_callback(service, manager, 71, {}, reject));
    CHECK_FALSE(ark::reg_external_root_callback(service, manager, 71, accept, {}));
    CHECK_FALSE(ark::reg_external_root_callback(service, manager, 71, {}, {}));
    CHECK(service.registrations == 0);
    CHECK(service.callbacks.empty());
    REQUIRE(ark::reg_external_root_callback(service, manager, 71, accept, reject));
    CHECK(accepted == 0);
    CHECK(rejected == 0);
    CHECK_FALSE(ark::reg_external_root_callback(service, manager, 71, reject, accept));
    service.dispatch(71, message.message, 2048);
    CHECK(accepted == 1);
    CHECK(rejected == 0);
}

TEST_CASE("External-root contract rejects stopping, draining and stopped while ordinary callbacks still dispatch") {
    ark::plugin_manager manager;
    lifecycle_probe module;
    recording_server service;
    message_owner message;
    REQUIRE(message.message != nullptr);
    REQUIRE(manager.add_module(module.get_name(), &module));
    const auto owner = std::this_thread::get_id();
    unsigned accepted{}, rejected{}, ordinary{};
    auto check_arguments = [&](const ark::net_msg* received, ark::conv_id_t session) {
        CHECK(std::this_thread::get_id() == owner);
        CHECK(received == message.message);
        CHECK(session == 2048);
    };
    REQUIRE(ark::reg_external_root_callback(
        service, manager, 71,
        [&](const ark::net_msg* received, ark::conv_id_t session) {
            check_arguments(received, session);
            ++accepted;
        },
        [&](const ark::net_msg* received, ark::conv_id_t session) {
            check_arguments(received, session);
            ++rejected;
        }));
    REQUIRE(service.reg_msg_callback(72, [&](const ark::net_msg*, ark::conv_id_t) { ++ordinary; }));
    auto dispatch = [&] {
        service.dispatch(71, message.message, 2048);
        service.dispatch(72, message.message, 2048);
    };
    module.on_update = dispatch;
    REQUIRE(manager.add_update_module(&module));
    REQUIRE(manager.update(1));
    CHECK(accepted == 1);
    module.on_stop = [&] {
        CHECK(manager.get_shutdown_state() == state::stopping_acceptance);
        dispatch();
    };
    CHECK_FALSE(manager.stop(2));
    CHECK(manager.get_shutdown_state() == state::draining);
    REQUIRE(manager.update(3));
    CHECK(rejected == 2);
    CHECK(ordinary == 3);
    module.ready = true;
    REQUIRE(manager.stop(4));
    CHECK(manager.get_shutdown_state() == state::stopped);
    CHECK_FALSE(manager.update(5));
    // Direct recorder dispatch tests the wrapper predicate after stop, not a
    // claim that a stopped manager still delivers real transport callbacks.
    dispatch();
    CHECK(accepted == 1);
    CHECK(rejected == 3);
    CHECK(ordinary == 4);
}

TEST_CASE("External-root contract rejects timed-out and failed states without claiming failed updates") {
    for (const bool failed : {false, true})
    {
        ark::plugin_manager manager;
        lifecycle_probe module;
        recording_server service;
        message_owner message;
        REQUIRE(message.message != nullptr);
        REQUIRE(manager.add_module(module.get_name(), &module));
        unsigned accepted{}, rejected{}, ordinary{};
        REQUIRE(ark::reg_external_root_callback(
            service, manager, 71, [&](const ark::net_msg*, ark::conv_id_t) { ++accepted; },
            [&](const ark::net_msg*, ark::conv_id_t) { ++rejected; }));
        REQUIRE(service.reg_msg_callback(72, [&](const ark::net_msg*, ark::conv_id_t) { ++ordinary; }));
        auto dispatch = [&] {
            service.dispatch(71, message.message, 9);
            service.dispatch(72, message.message, 9);
        };
        module.on_update = dispatch;
        REQUIRE(manager.add_update_module(&module));
        if (failed)
        {
            module.stop_result = false;
            CHECK_FALSE(manager.stop(1));
            CHECK(manager.get_shutdown_state() == state::failed);
            CHECK_FALSE(manager.update(2));
            CHECK(rejected == 0);
            CHECK(ordinary == 0);
            dispatch(); // Direct state contract only: failed manager cannot pump.
        }
        else
        {
            CHECK_FALSE(manager.stop_until(1, std::chrono::steady_clock::now()));
            CHECK(manager.get_shutdown_state() == state::timed_out);
            REQUIRE(manager.update(2));
        }
        CHECK(accepted == 0);
        CHECK(rejected == 1);
        CHECK(ordinary == 1);
    }
}

TEST_CASE("Accepted external-root callback may request stop without tearing down its active owner frame") {
    ark::plugin_manager manager;
    lifecycle_probe module;
    recording_server service;
    message_owner message;
    REQUIRE(message.message != nullptr);
    REQUIRE(manager.add_module(module.get_name(), &module));
    unsigned accepted{}, rejected{}, ordinary{};
    REQUIRE(ark::reg_external_root_callback(
        service, manager, 71,
        [&](const ark::net_msg*, ark::conv_id_t) {
            ++accepted;
            CHECK_FALSE(manager.stop(2));
            CHECK(manager.get_shutdown_state() == state::stopping_acceptance);
            CHECK_THROWS_AS(manager.update(3), std::logic_error);
        },
        [&](const ark::net_msg*, ark::conv_id_t) { ++rejected; }));
    REQUIRE(service.reg_msg_callback(72, [&](const ark::net_msg*, ark::conv_id_t) { ++ordinary; }));
    module.on_update = [&] {
        service.dispatch(71, message.message, 7);
        service.dispatch(71, message.message, 7);
        service.dispatch(72, message.message, 7);
    };
    REQUIRE(manager.add_update_module(&module));
    REQUIRE(manager.update(1));
    CHECK(accepted == 1);
    CHECK(rejected == 1);
    CHECK(ordinary == 1);
    module.ready = true;
    REQUIRE(manager.stop(4));
}

TEST_CASE("External-root contract retains manager owner-thread enforcement") {
    ark::plugin_manager manager;
    recording_server service;
    bool called{};
    REQUIRE(ark::reg_external_root_callback(
        service, manager, 71, [&](const ark::net_msg*, ark::conv_id_t) { called = true; },
        [&](const ark::net_msg*, ark::conv_id_t) { called = true; }));
    bool owner_error{};
    std::thread other([&] {
        try
        { service.dispatch(71, nullptr, 0); }
        catch (const std::logic_error&)
        { owner_error = true; }
    });
    other.join();
    CHECK(owner_error);
    CHECK_FALSE(called);
}
