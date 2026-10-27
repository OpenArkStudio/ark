// Copyright (c) 2026 OpenArkStudio. SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <stdexcept>
#include <string>

#include <ark/bus/bus_module.hpp>
#include <ark/module_base.hpp>
#include <ark/net/net_service_manager_module.hpp>

namespace {

void require_startup(bool condition, const char* message) {
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

// These providers control only startup outcomes. The role factory, public
// assembly and manager lifecycle under test are the production implementations.
class startup_bus final : public ark::module_base<ark::bus_module>
{
public:
    const std::string get_app_whole_name(ark::bus_id_t) override {
        return "startup-test";
    }

    const std::string& get_app_name(const ark::app_type&) override {
        return name_;
    }

    ark::app_type get_app_type(const std::string&) override {
        return {};
    }

    const ark::proc_config& get_self_proc() override {
        return process_;
    }

    ark::app_type get_self_app_type() override {
        return {};
    }

    ark::bus_id_t get_self_bus_id() override {
        return {};
    }

    const std::string get_self_bus_name() override {
        return "startup-test";
    }

    int combine_bus_id(const ark::app_type, const uint8_t) override {
        return 0;
    }

    const ark::reg_center& get_reg_center() override {
        return registry_;
    }

    bool get_target_bus_relations(std::vector<ark::app_type>&) override {
        return true;
    }

    bool get_other_proc(ark::bus_id_t, ark::proc_config&) const override {
        return false;
    }

    int get_other_proc(const ark::app_type, std::vector<const ark::proc_config*>&) const override {
        return 0;
    }

private:
    std::string name_{"startup-test"};
    ark::proc_config process_{};
    ark::reg_center registry_{};
};

class startup_service final : public ark::net_server_service
{
public:
    bool start(std::shared_ptr<const ark::msg_header>, ark::bus_id_t, const ark::endpoint&, uint8_t, uint32_t,
        const size_t) override {
        throw std::logic_error("Role must delegate listener creation to Net");
    }

    bool update() override {
        return true;
    }

    std::shared_ptr<ark::net> get_net() override {
        return {};
    }

    bool reg_msg_callback(ark::msg_id_t, ark::net_msg_functor&&) override {
        return true;
    }

    bool reg_forward_msg_callback(ark::net_msg_functor&&) override {
        return true;
    }

    bool reg_net_event_callback(ark::net_event_functor&&) override {
        return true;
    }

    bool reg_reg_server_call_back(ark::net_msg_session_functor&&) override {
        return true;
    }

    void set_timeout_interval(const int32_t) override {
    }
};

class startup_net final : public ark::module_base<ark::net_service_manager_module>
{
public:
    bool created = true;
    unsigned create_calls{};
    unsigned service_reads{};
    unsigned late_calls{};
    std::shared_ptr<ark::net_server_service> service = std::make_shared<startup_service>();

    bool create_server(const size_t, const ark::msg_header&, const ark::msg_header&) override {
        ++create_calls;
        return created;
    }

    std::shared_ptr<ark::net_server_service> get_inter_net_server() override {
        ++service_reads;
        return service;
    }

    bool pre_update() override {
        ++late_calls;
        return true;
    }

    std::shared_ptr<ark::net_server_service> get_intra_net_server() override {
        return {};
    }

    bool add_net_connection_bus(ark::bus_id_t, std::shared_ptr<ark::net>) override {
        return false;
    }

    bool remove_net_connection_bus(ark::bus_id_t) override {
        return false;
    }

    std::shared_ptr<ark::net> get_net_connection_bus(ark::bus_id_t, ark::bus_id_t) override {
        return {};
    }

    std::shared_ptr<ark::net_client_service> get_client_service(const ark::app_type) override {
        return {};
    }

    ark::conv_id_t get_session_id(ark::bus_id_t) override {
        return {};
    }

    bool remove_session_id(ark::conv_id_t) override {
        return false;
    }

    bool create_client_service(const ark::bus_addr&, ark::proto_type, const std::string&, uint16_t,
        std::shared_ptr<const ark::msg_header>) override {
        return false;
    }
};

void check_startup(bool with_bus, bool with_net, bool created, bool has_service, bool game) {
    ark::plugin_manager manager;
    startup_bus bus;
    startup_net net;
    net.created = created;
    if (!has_service)
    {
        net.service.reset();
    }
    if (with_bus)
    {
        require_startup(manager.add_module(typeid(ark::bus_module).name(), &bus), "Bus fixture registration failed");
    }
    if (with_net)
    {
        require_startup(manager.add_module(typeid(ark::net_service_manager_module).name(), &net),
            "Net fixture registration failed");
    }
    server_plugin owner;
    owner.set_plugin_manager(&manager);
    owner.install();
    const bool expected_init = with_bus && with_net && !game;
    const bool expected_start = expected_init && created && has_service;
    require_startup(manager.start(42, {}, {}, {}) == expected_start, "Real role startup result was lost");
    const auto failure = manager.get_startup_failure();
    require_startup(expected_start ? failure.empty()
                                   : failure == std::string(expected_init ? "post_init: " : "init: ") +
                                                    typeid(server_module).name(),
        "Manager did not retain the real role and failing phase");
    require_startup(
        net.create_calls == (expected_init ? 1u : 0u), "Role created a listener before checking dependencies");
    require_startup(
        net.service_reads == (expected_init && created ? 1u : 0u), "Role read the listener after failed creation");
    require_startup(
        net.late_calls == (expected_start ? 1u : 0u), "Manager continued to pre_update after startup failure");
    owner.uninstall();
    if (with_bus)
    {
        manager.remove_module(typeid(ark::bus_module).name());
    }
    if (with_net)
    {
        manager.remove_module(typeid(ark::net_service_manager_module).name());
    }
}

void check_role_startup(bool game) {
    check_startup(false, false, true, true, game);
    check_startup(false, true, true, true, game);
    check_startup(true, false, true, true, game);
    if (game)
    {
        // Game registers Kernel callbacks in init. An absent Kernel must fail
        // before those dereferences; this fixture does not simulate Kernel.
        check_startup(true, true, true, true, true);
    }
    else
    {
        check_startup(true, true, false, true, false);
        check_startup(true, true, true, false, false);
        check_startup(true, true, true, true, false);
    }
}

} // namespace
