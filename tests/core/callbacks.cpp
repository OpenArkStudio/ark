// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <chrono>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "../network/doctest.h"

#include <ark/utility/schedule_module.hpp>
#include <ark/utility/timer_module.hpp>

#if defined(ARK_HAS_PROTOBUF)
#include <ark/net/net_client_service.hpp>
#include <ark/net/net_server_service.hpp>
#endif

namespace {

struct receiver {
    void on_timer(std::uint64_t timer_id, const ark::guid_t& entity_id) {
        timer_id_ = timer_id;
        entity_id_ = entity_id;
    }

    void on_schedule() {
        ++calls_;
    }

    std::uint64_t timer_id_{};
    ark::guid_t entity_id_{};
    int calls_{};
};

class capturing_timer final : public ark::timer_module {
public:
    using ark::timer_module::add_forever_timer;
    using ark::timer_module::add_single_timer;

    bool remove_timer(std::uint64_t) override {
        return true;
    }

    ark::timer_functor callback_;

private:
    std::uint64_t add_single_timer(
        const ark::guid_t&, std::chrono::milliseconds, std::uint32_t, ark::timer_functor&& callback) override {
        callback_ = std::move(callback);
        return 91;
    }

    std::uint64_t add_forever_timer(
        const ark::guid_t&, std::chrono::milliseconds, ark::timer_functor&& callback) override {
        callback_ = std::move(callback);
        return 92;
    }
};

class capturing_schedule final : public ark::schedule_module {
public:
    using ark::schedule_module::add_at_schedule;
    using ark::schedule_module::add_interval_schedule;

    std::string expression_;
    ark::scheduler_functor callback_;

private:
    void add_interval_schedule(std::chrono::system_clock::duration, ark::scheduler_functor&& callback) override {
        callback_ = std::move(callback);
    }

    void add_at_schedule(const std::string& target, ark::scheduler_functor&& callback) override {
        expression_ = target;
        callback_ = std::move(callback);
    }

    void add_cron_schedule(const std::string& expression, ark::scheduler_functor&& callback) override {
        expression_ = expression;
        callback_ = std::move(callback);
    }
};

} // namespace

DOCTEST_TEST_CASE("Member timer callbacks preserve receiver and invocation arguments") {
    receiver target;
    capturing_timer timers;
    const ark::guid_t entity_id = 77;
    DOCTEST_REQUIRE(
        timers.add_single_timer(entity_id, std::chrono::milliseconds(10), 2, &target, &receiver::on_timer) == 91);
    timers.callback_(123, entity_id);
    DOCTEST_REQUIRE(target.timer_id_ == 123);
    DOCTEST_REQUIRE(target.entity_id_ == entity_id);
    DOCTEST_REQUIRE(
        timers.add_forever_timer(entity_id, std::chrono::milliseconds(10), &target, &receiver::on_timer) == 92);
    timers.callback_(456, entity_id);
    DOCTEST_REQUIRE(target.timer_id_ == 456);
}

DOCTEST_TEST_CASE("Member schedule callbacks preserve receiver and scheduling expressions") {
    receiver target;
    capturing_schedule schedules;
    schedules.add_schedule("1 2 * * *", &target, &receiver::on_schedule);
    DOCTEST_REQUIRE(schedules.expression_ == "1 2 * * *");
    schedules.callback_();
    schedules.add_daily_schedule(&target, &receiver::on_schedule);
    DOCTEST_REQUIRE(schedules.expression_ == "0 0 * * * ");
    schedules.callback_();
    schedules.add_weekly_schedule(&target, &receiver::on_schedule);
    DOCTEST_REQUIRE(schedules.expression_ == "0 0 * * 1");
    schedules.callback_();
    schedules.add_monthly_schedule(&target, &receiver::on_schedule);
    DOCTEST_REQUIRE(schedules.expression_ == "0 0 1 * * ");
    schedules.callback_();
    schedules.add_interval_schedule(std::chrono::seconds(1), &target, &receiver::on_schedule);
    schedules.callback_();
    schedules.add_at_schedule("12:00", &target, &receiver::on_schedule);
    DOCTEST_REQUIRE(schedules.expression_ == "12:00");
    schedules.callback_();
    DOCTEST_REQUIRE(target.calls_ == 6);
}

#if defined(ARK_HAS_PROTOBUF)
namespace {

struct network_receiver {
    network_receiver() = default;
    network_receiver(const network_receiver&) = delete;

    void on_message(const ark::net_msg* message, ark::conv_id_t session) {
        messages.emplace_back(message, session);
        observed_marker = marker;
    }

    void on_forward(const ark::net_msg* message, int session) {
        on_message(message, static_cast<ark::conv_id_t>(session));
    }

    void on_event(const ark::net_event* event) {
        events.push_back(event);
        observed_marker = marker;
    }

    int marker{};
    int observed_marker{};
    std::vector<std::pair<const ark::net_msg*, ark::conv_id_t>> messages;
    std::vector<const ark::net_event*> events;
};

struct callback_capture {
    ark::net_msg_functor message;
    ark::net_msg_functor forward;
    ark::net_event_functor event;
    ark::net_msg_session_functor session;
    ark::msg_id_t message_id{};
    bool accept{true};
};

class capturing_server final
    : public ark::net_server_service
    , public callback_capture {
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

    void set_timeout_interval(int32_t) override {
    }

    bool reg_msg_callback(ark::msg_id_t id, ark::net_msg_functor&& callback) override {
        message_id = id;
        message = std::move(callback);
        return accept;
    }

    bool reg_forward_msg_callback(ark::net_msg_functor&& callback) override {
        forward = std::move(callback);
        return accept;
    }

    bool reg_net_event_callback(ark::net_event_functor&& callback) override {
        event = std::move(callback);
        return accept;
    }

    bool reg_reg_server_call_back(ark::net_msg_session_functor&& callback) override {
        session = std::move(callback);
        return accept;
    }
};

class capturing_client final
    : public ark::net_client_service
    , public callback_capture {
public:
    bool start_client(std::shared_ptr<const ark::msg_header>, ark::bus_id_t, const ark::endpoint&) override {
        return false;
    }

    void update() override {
    }

    std::shared_ptr<ark::connection_data> get_connection_info(ark::bus_id_t) override {
        return {};
    }

    std::shared_ptr<ark::connection_data> get_suitable_connect(const std::string&) override {
        return {};
    }

    void add_account_bus_id(const std::string&, ark::bus_id_t) override {
    }

    void remove_account_bus_id(const std::string&) override {
    }

    int get_account_bus_id(const std::string&) override {
        return 0;
    }

    void add_actor_bus_id(const ark::guid_t&, ark::bus_id_t) override {
    }

    void remove_actor_bus_id(const ark::guid_t&) override {
    }

    int get_actor_bus_id(const ark::guid_t&) override {
        return 0;
    }

    bool reg_msg_callback(ark::msg_id_t id, const ark::net_msg_functor&& callback) override {
        message_id = id;
        message = callback;
        return accept;
    }

    bool reg_forward_msg_callback(const ark::net_msg_functor&& callback) override {
        forward = callback;
        return accept;
    }

    bool reg_net_event_callback(const ark::net_event_functor&& callback) override {
        event = callback;
        return accept;
    }
};

template<typename service_type>
void check_network_registration(service_type& service, callback_capture& captured) {
    constexpr bool server = std::is_same_v<service_type, ark::net_server_service>;
    network_receiver target;
    REQUIRE(service.reg_msg_callback(71, &target, &network_receiver::on_message));
    if constexpr (std::is_same_v<service_type, ark::net_server_service>) {
        // The existing server forward overload takes int rather than conv_id_t.
        REQUIRE(service.reg_forward_msg_callback(&target, &network_receiver::on_forward));
        REQUIRE(service.reg_reg_server_call_back(&target, &network_receiver::on_message));
    } else {
        REQUIRE(service.reg_forward_msg_callback(&target, &network_receiver::on_message));
    }
    REQUIRE(service.reg_net_event_callback(&target, &network_receiver::on_event));
    CHECK(captured.message_id == 71);
    CHECK(target.messages.empty());
    CHECK(target.events.empty());
    // Copying the erased callback still borrows the same noncopyable receiver.
    auto copied = captured.message;
    target.marker = 29;
    auto message = std::shared_ptr<ark::net_msg>(
        ark::net_msg::alloc_msg(new ark::s_msg_header), [](ark::net_msg* value) { ark::net_msg::release(value); });
    ark::net_event event;
    const ark::conv_id_t session = 2048;
    event.set_id(session);
    captured.message(message.get(), session);
    captured.forward(message.get(), session + 1);
    copied(message.get(), session + 2);
    if (server) {
        captured.session(message.get(), session + 3);
    }
    captured.event(&event);
    REQUIRE(target.messages.size() == (server ? 4 : 3));
    for (std::size_t index = 0; index < target.messages.size(); ++index) {
        CHECK(target.messages[index].first == message.get());
        CHECK(target.messages[index].second == session + index);
    }
    REQUIRE(target.events.size() == 1);
    CHECK(target.events.front() == &event);
    CHECK(target.events.front()->get_id() == session);
    CHECK(target.observed_marker == 29);
    CHECK(session == 2048);
    captured.accept = false;
    CHECK_FALSE(service.reg_msg_callback(72, &target, &network_receiver::on_message));
    CHECK_FALSE(service.reg_net_event_callback(&target, &network_receiver::on_event));
}

} // namespace

DOCTEST_TEST_CASE("Network service registration preserves borrowed receivers, messages, events and sessions") {
    SUBCASE("Client") {
        capturing_client service;
        check_network_registration<ark::net_client_service>(service, service);
    }
    SUBCASE("Server") {
        capturing_server service;
        check_network_registration<ark::net_server_service>(service, service);
    }
}
#endif
