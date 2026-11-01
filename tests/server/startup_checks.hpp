// Copyright (c) 2026 OpenArkStudio. SPDX-License-Identifier: Apache-2.0
#pragma once

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

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
template<typename interface_type>
class startup_provider : public ark::module_base<interface_type>
{
public:
    std::vector<std::string> get_dependencies() const override {
        return dependencies;
    }

    bool init() override {
        phases.emplace_back("init");
        return true;
    }

    bool post_init() override {
        phases.emplace_back("post_init");
        return true;
    }

    bool check_config() override {
        phases.emplace_back("check_config");
        return true;
    }

    bool pre_update() override {
        phases.emplace_back("pre_update");
        return true;
    }

    bool stop_accepting() override {
        require_startup(!dependent_stopped || *dependent_stopped, "Provider stopped before the role dependent");
        return true;
    }

    std::vector<std::string> dependencies;
    std::vector<std::string> phases;
    const bool* dependent_stopped{};
};

class startup_bus final : public startup_provider<ark::bus_module>
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

class startup_net final : public startup_provider<ark::net_service_manager_module>
{
public:
    bool created = true;
    unsigned create_calls{};
    unsigned service_reads{};
    unsigned late_calls{};
    bool require_ready{};
    std::shared_ptr<ark::net_server_service> service = std::make_shared<startup_service>();

    bool create_server(const size_t, const ark::msg_header&, const ark::msg_header&) override {
        require_startup(!require_ready || phases == std::vector<std::string>{"init", "post_init"},
            "Role created the listener before Net post_init");
        ++create_calls;
        return created;
    }

    std::shared_ptr<ark::net_server_service> get_inter_net_server() override {
        ++service_reads;
        return service;
    }

    bool pre_update() override {
        ++late_calls;
        return startup_provider::pre_update();
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
    auto dependencies = manager.find_module<server_module>()->get_dependencies();
    std::sort(dependencies.begin(), dependencies.end());
    const auto missing = std::find_if(dependencies.begin(), dependencies.end(), [&](const auto& key) {
        return (key != typeid(ark::bus_module).name() || !with_bus) &&
               (key != typeid(ark::net_service_manager_module).name() || !with_net);
    });
    const bool rejected_dependencies = missing != dependencies.end();
    const bool expected_init = with_bus && with_net && !game;
    const bool expected_start = expected_init && created && has_service;
    require_startup(manager.start(42, {}, {}, {}) == expected_start, "Real role startup result was lost");
    const auto failure = manager.get_startup_failure();
    if (rejected_dependencies)
    {
        require_startup(failure == std::string("dependencies: ") + typeid(server_module).name() +
                                       " requires missing module '" + *missing + "'",
            "Manager did not reject the missing declared role provider");
        require_startup(bus.phases.empty() && net.phases.empty(), "Missing provider allowed startup hooks");
    }
    require_startup(
        rejected_dependencies || (expected_start ? failure.empty()
                                                 : failure == std::string(expected_init ? "post_init: " : "init: ") +
                                                                  typeid(server_module).name()),
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

class startup_dependent final : public ark::module
{
public:
    startup_dependent(startup_bus& bus, startup_net& net)
        : bus_(bus)
        , net_(net) {
    }

    std::vector<std::string> get_dependencies() const override {
        return {typeid(server_module).name()};
    }

    bool init() override {
        check_phases({"init"});
        return true;
    }

    bool post_init() override {
        check_phases({"init", "post_init"});
        require_startup(net_.create_calls == 1 && net_.service_reads == 1,
            "Dependent ran before the actual role created its listener");
        return true;
    }

    bool check_config() override {
        check_phases({"init", "post_init", "check_config"});
        return true;
    }

    bool pre_update() override {
        check_phases({"init", "post_init", "check_config", "pre_update"});
        return true;
    }

    bool stop_accepting() override {
        stopped = true;
        return true;
    }

    bool stopped{};

private:
    void check_phases(const std::vector<std::string>& expected) {
        require_startup(bus_.phases == expected && net_.phases == expected,
            "Role dependent ran before a required provider completed its startup phase");
    }

    startup_bus& bus_;
    startup_net& net_;
};

void check_declared_role_order() {
    ark::plugin_manager manager;
    startup_bus bus;
    startup_net net;
    ark::module readiness;
    startup_dependent dependent(bus, net);
    net.require_ready = true;
    // Raw key order would visit the role/dependent before these blocked
    // providers. Only the actual production role's edges make this valid.
    bus.dependencies = {"zz_provider_ready"};
    net.dependencies = {"zz_provider_ready"};
    bus.dependent_stopped = &dependent.stopped;
    net.dependent_stopped = &dependent.stopped;
    require_startup(manager.add_module(typeid(ark::bus_module).name(), &bus), "Bus registration failed");
    require_startup(
        manager.add_module(typeid(ark::net_service_manager_module).name(), &net), "Net registration failed");
    require_startup(manager.add_module("zz_provider_ready", &readiness), "Readiness registration failed");
    require_startup(manager.add_module("!role_dependent", &dependent), "Dependent registration failed");
    server_plugin owner;
    owner.set_plugin_manager(&manager);
    owner.install();
    require_startup(manager.start(42, {}, {}, {}), "Declared role graph failed startup");
    require_startup(manager.stop(42), "Declared role graph failed shutdown");
    require_startup(dependent.stopped, "Role dependent did not stop");
    owner.uninstall();
    manager.remove_module(typeid(ark::bus_module).name());
    manager.remove_module(typeid(ark::net_service_manager_module).name());
    manager.remove_module("zz_provider_ready");
    manager.remove_module("!role_dependent");
}

void check_role_startup(const std::string& role) {
    const bool game = role == "ark_game_plugin";
    const bool declared = role == "ark_login_plugin" || role == "ark_master_plugin" || role == "ark_proxy_plugin" ||
                          role == "ark_world_plugin" || role == "ark_dir_plugin" || role == "ark_router_plugin";
    if (declared)
    {
        check_declared_role_order();
        require_startup(
            make_server_module()->get_dependencies() == std::vector<std::string>{typeid(ark::bus_module).name(),
                                                            typeid(ark::net_service_manager_module).name()},
            "Production role must declare exactly its required Bus and Net providers");
    }
    check_startup(false, false, true, true, game);
    check_startup(false, true, true, true, game);
    check_startup(true, false, true, true, game);
    if (game)
    {
        // Game validates Kernel in init and registers its callbacks in post_init.
        // This dependency fixture does not simulate Kernel.
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
