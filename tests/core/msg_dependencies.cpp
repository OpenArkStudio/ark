// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <memory>
#include <string>
#include <typeinfo>
#include <vector>

#include "../network/doctest.h"

#include <ark/bus/msg_module.hpp>
#include <ark/module_base.hpp>
#include <ark/net/net_service_manager_module.hpp>
#include <ark/plugin_manager.hpp>
#include "bus/bus_plugin.hpp"

namespace {
template<typename interface_type>
class observed_provider : public ark::module_base<interface_type> {
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

    bool pre_shut() override {
        if (dependent_stopped) {
            CHECK(*dependent_stopped);
        }
        phases.emplace_back("pre_shut");
        return true;
    }

    std::vector<std::string> dependencies;
    std::vector<std::string> phases;
    const bool* dependent_stopped{};
};

class bus_provider final : public observed_provider<ark::bus_module> {
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
        ++identity_reads;
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
        return true;
    }

    bool get_other_proc(ark::bus_id_t, ark::proc_config&) const override {
        return false;
    }

    int get_other_proc(ark::app_type, std::vector<const ark::proc_config*>&) const override {
        return 0;
    }

    unsigned identity_reads{};

private:
    std::string name_;
    ark::proc_config proc_;
    ark::reg_center registry_;
};

class net_provider final : public observed_provider<ark::net_service_manager_module> {
public:
    bool create_server(std::size_t, const ark::msg_header&, const ark::msg_header&) override {
        return false;
    }

    std::shared_ptr<ark::net_server_service> get_inter_net_server() override {
        ++lookups;
        return {};
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

    std::shared_ptr<ark::net_client_service> get_client_service(ark::app_type) override {
        return {};
    }

    ark::conv_id_t get_session_id(ark::bus_id_t) override {
        return {};
    }

    bool remove_session_id(ark::conv_id_t) override {
        return false;
    }

    bool create_client_service(const ark::bus_addr&, ark::proto_type, const std::string&, std::uint16_t,
        std::shared_ptr<const ark::msg_header>) override {
        return false;
    }

    unsigned lookups{};
};

struct fixture {
    fixture() {
        REQUIRE(manager.add_module(typeid(ark::bus_module).name(), &bus));
        REQUIRE(manager.add_module(typeid(ark::net_service_manager_module).name(), &network));
        messages->set_plugin_manager(&manager);
        REQUIRE(manager.add_module(typeid(ark::msg_module).name(), messages.get()));
    }

    ark::plugin_manager manager;
    bus_provider bus;
    net_provider network;
    std::unique_ptr<ark::msg_module> messages{ark::detail::make_msg_module()};
};

class message_dependent final : public ark::module_base<ark::module, ark::msg_module> {
public:
    explicit message_dependent(fixture& state)
        : state_(state) {
    }

    bool init() override {
        check_phases({"init"});
        // Exercise the actual consumer's cached Net and Bus pointers after its init.
        CHECK_FALSE(state_.messages->send_to_client(1, 2, std::string("body")));
        CHECK(state_.network.lookups == 1);
        CHECK(state_.bus.identity_reads == 1);
        return true;
    }

    bool post_init() override {
        check_phases({"init", "post_init"});
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

    bool pre_shut() override {
        check_phases({"init", "post_init", "check_config", "pre_update"});
        stopped = true;
        return true;
    }

    bool stopped{};

private:
    void check_phases(const std::vector<std::string>& expected) {
        CHECK(state_.bus.phases == expected);
        CHECK(state_.network.phases == expected);
    }

    fixture& state_;
};
} // namespace

TEST_CASE("Message production factory declares its retained Bus and Net providers") {
    const auto messages = ark::detail::make_msg_module();
    CHECK(messages->get_dependencies() ==
          std::vector<std::string>{typeid(ark::bus_module).name(), typeid(ark::net_service_manager_module).name()});
}

TEST_CASE("Message missing providers fail manager validation before any startup hook") {
    for (const bool missing_bus : {true, false}) {
        CAPTURE(missing_bus);
        fixture state;
        const std::string missing =
            missing_bus ? typeid(ark::bus_module).name() : typeid(ark::net_service_manager_module).name();
        state.manager.remove_module(missing);
        CHECK_FALSE(state.manager.start(0, {}, {}, {}));
        CHECK(state.manager.get_startup_failure() == std::string("dependencies: ") + typeid(ark::msg_module).name() +
                                                         " requires missing module '" + missing + "'");
        CHECK(state.manager.get_startup_call().phase.data() == std::string("dependencies"));
        CHECK(state.manager.get_startup_call().result == ark::startup_call_result::returned_false);
        CHECK(state.bus.phases.empty());
        CHECK(state.network.phases.empty());
        CHECK(state.network.lookups == 0);
    }
}

TEST_CASE("Message provider cycles fail validation before any startup hook") {
    for (const bool cycle_via_bus : {true, false}) {
        CAPTURE(cycle_via_bus);
        fixture state;
        auto& dependencies = cycle_via_bus ? state.bus.dependencies : state.network.dependencies;
        dependencies = {typeid(ark::msg_module).name()};
        CHECK_FALSE(state.manager.start(0, {}, {}, {}));
        CHECK(state.manager.get_startup_failure().find("dependencies: cycle prevents startup;") == 0);
        CHECK(state.bus.phases.empty());
        CHECK(state.network.phases.empty());
        CHECK(state.network.lookups == 0);
    }
}

TEST_CASE("Message graph orders provider startup and reverses admission stop") {
    // The production send-failure path writes through the host logger.
    struct logger_scope {
        logger_scope() {
            current.set_log_func([](auto, const auto&) {});
            ark::logger::init(&current);
        }

        ~logger_scope() {
            ark::logger::init(previous);
        }

        ark::logger* previous{ark::logger::instance()};
        ark::logger current;
    } logging;

    fixture state;
    ark::module readiness;
    message_dependent dependent(state);
    // Force both providers behind a late key, so insertion/RTTI order cannot
    // accidentally satisfy the consumer's declaration or its early witness.
    state.bus.dependencies = {"zz_provider_ready"};
    state.network.dependencies = {typeid(ark::bus_module).name(), "zz_provider_ready"};
    REQUIRE(state.manager.add_module("zz_provider_ready", &readiness));
    REQUIRE(state.manager.add_module("!message_dependent", &dependent));
    state.bus.dependent_stopped = &dependent.stopped;
    state.network.dependent_stopped = &dependent.stopped;
    REQUIRE(state.manager.start(0, {}, {}, {}));
    REQUIRE(state.manager.stop(0));
    CHECK(dependent.stopped);
    CHECK(state.bus.phases == std::vector<std::string>{"init", "post_init", "check_config", "pre_update", "pre_shut"});
    CHECK(state.network.phases == state.bus.phases);
}
