// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <typeinfo>

#include <ark/bus/bus_module.hpp>
#include <ark/bus/msg_module.hpp>
#include <ark/net/net_service_manager_module.hpp>
#include <ark/plugin_manager.hpp>
#include "owner_drain.hpp"

namespace {

void require(bool condition, const std::string& message) {
    if (!condition)
    {
        std::cerr << message << std::endl;
        std::_Exit(1);
    }
}

// Observe Bus serialization through the existing public Net relation seam.
class recording_net final : public ark::net
{
public:
    void update() override {
    }

    void shutdown() override {
    }

    void close_session(ark::conv_id_t) override {
    }

    bool send_msg(ark::msg_header* header, const char* bytes, ark::conv_id_t session) override {
        source = header->source_bus_id();
        destination = header->dest_bus_id();
        actor = header->actor_id();
        message_id = header->message_id();
        session_id = session;
        body.assign(bytes, header->message_length());
        ++calls;
        return accepted;
    }

    ark::bus_id_t source{};
    ark::bus_id_t destination{};
    ark::guid_t actor{};
    ark::msg_id_t message_id{};
    ark::conv_id_t session_id{};
    std::string body;
    int calls{};
    bool accepted{true};
};

} // namespace

int main(int argc, char* argv[]) {
    require(argc == 2, "Expected plugin directory");
    std::ofstream config("bus.toml");
    config << R"([bus]
[[bus.applications]]
name = 'game'
type = 31
[[bus.applications]]
name = 'world'
type = 30
[[bus.relations]]
source = 'game'
target = 'world'
[[bus.processes]]
bus_id = '1.1.31.1'
server_endpoint = ''
intranet_endpoint = ''
max_connections = 10
threads = 1
[[bus.processes]]
bus_id = '1.2.30.2'
server_endpoint = ''
intranet_endpoint = ''
max_connections = 10
threads = 1
[bus.registry]
host = '127.0.0.1'
port = 8500
service_name = 'test'
check_interval = '10s'
check_timeout = '3s'
)";
    config.close();
    ark::logger log;
    ark::logger::init(&log);
    log.set_log_func([](spdlog::level::level_enum, const std::string& text) { std::cout << text << std::endl; });
    ark::plugin_manager manager;
    const ark::bus_addr self(1, 1, 31, 1);
    // Keep the serialization seam independent of automatic same-zone dialing.
    const ark::bus_addr target(1, 2, 30, 2);
    manager.set_bus_id(self.bus_id);
    const bool started = manager.start(0, std::string(argv[1]) + "/", "",
        {{"ark_bus_plugin", "bus.toml"}, {"ark_net_plugin", ""}, {"ark_utility_plugin", ""}});
    require(started, "Bus DSO startup failed: " + manager.get_startup_failure());
    auto* bus = manager.find_module<ark::bus_module>();
    auto* messages = manager.find_module<ark::msg_module>();
    auto* network = manager.find_module<ark::net_service_manager_module>();
    require(bus && messages && network, "Bus or Net public interface lookup failed");
    require(bus->get_plugin_manager() == &manager && messages->get_plugin_manager() == &manager &&
                bus->get_name() == typeid(ark::bus_module).name() &&
                messages->get_name() == typeid(ark::msg_module).name(),
        "Bus registration lost manager ownership or interface RTTI keys");
    require(bus->get_self_bus_id() == self.bus_id && bus->get_self_bus_name() == "1.1.31.1" &&
                bus->get_app_whole_name(self.bus_id) == "game-1.1.31.1",
        "Bus process identity changed");
    std::vector<ark::app_type> relations;
    require(bus->get_target_bus_relations(relations) && relations == std::vector{ark::app_type::ark_app_world},
        "Bus relation queries changed");
    ark::proc_config other;
    require(bus->get_other_proc(target.bus_id, other) && other.bus_id == target.bus_id && other.thread_num == 1,
        "Bus configured other-process query changed");
    require(bus->get_reg_center().port == 8500 && bus->get_reg_center().check_interval == "10s",
        "Bus registry field queries changed");

    ark::protobuf::point_3d input;
    input.set_x(12.5F);
    input.set_y(-7.0F);
    input.set_z(0.0F);
    const auto actor = ark::guid_t{42};
    require(
        !messages->send_msg_by_bus_id(target.bus_id, 45001, input, actor), "Missing Net relation must reject Bus send");
    require(!messages->send_def_cs_msg_by_app_type(ark::app_type::ark_app_world, 45001, input) &&
                !messages->send_def_ss_msg_by_app_type(ark::app_type::ark_app_world, 45001, input),
        "Missing client service must retain both existing send failures");
    auto transport = std::make_shared<recording_net>();
    require(network->add_net_connection_bus(target.bus_id, transport), "Net relation injection rejected");
    require(messages->send_msg_by_bus_id(target.bus_id, 45001, input, actor), "Bus routed send rejected");
    require(transport->calls == 1 && transport->source == self.bus_id && transport->destination == target.bus_id &&
                transport->actor == actor && transport->message_id == 45001 && transport->session_id == 0,
        "Bus message header or default session changed");
    ark::protobuf::point_3d output;
    require(output.ParseFromString(transport->body) && output.x() == input.x() && output.y() == input.y(),
        "Bus Protobuf serialization changed");
    transport->accepted = false;
    require(!messages->send_msg_by_bus_id(target.bus_id, 45001, input, actor) && transport->calls == 2,
        "Bus send did not propagate the transport's failure");
    require(network->remove_net_connection_bus(target.bus_id), "Net relation removal failed");
    require(manager.update(10), "Bus/Net owner update failed");
    int64_t tick = 19;
    ark::test::drain_owner(
        manager, [&] { return ++tick; }, std::chrono::steady_clock::now() + std::chrono::seconds(5), "Bus actual DSOs");
    require(manager.stop(++tick) && manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::stopped,
        "Bus normal repeated shutdown failed");
    std::cout << "Bus actual DSOs: interface keys, TOML queries, routed Protobuf header/body, send failures "
                 "and normal repeated shutdown passed\n";
}
