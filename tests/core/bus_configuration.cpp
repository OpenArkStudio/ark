// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <tuple>

#include "../network/doctest.h"

#include "bus/bus_configuration.hpp"
#include "bus/bus_plugin.hpp"

namespace ark {
// Only the module is linked here: the test does not install messaging/network services.
void bus_plugin::install() {
}

void bus_plugin::uninstall() {
}
} // namespace ark

namespace {

const std::string configuration = R"([bus]
[[bus.applications]]
name = 'world'
type = 30
[[bus.applications]]
name = 'game'
type = 31
[[bus.relations]]
source = 'game'
target = 'world'
[[bus.processes]]
bus_id = '1.1.30.1'
server_endpoint = 'udp://0.0.0.0:9101'
intranet_endpoint = 'tcp://world.internal:9111'
max_connections = 5000
threads = 2
[[bus.processes]]
bus_id = '1.1.31.1'
server_endpoint = 'tcp://127.0.0.1:9102'
intranet_endpoint = ''
max_connections = 100
threads = 1
[bus.registry]
host = 'consul.internal'
port = 8500
service_name = 'ark-service'
check_interval = '10s'
check_timeout = '3s'
)";

class configuration_file
{
public:
    configuration_file()
        : path_(std::filesystem::temp_directory_path() /
                ("ark-bus-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".toml")) {
    }

    ~configuration_file() {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }

    std::string write(const std::string& text) const {
        std::ofstream output(path_, std::ios::binary);
        output.exceptions(std::ios::badbit | std::ios::failbit);
        output << text;
        output.close();
        return path_.string();
    }

private:
    std::filesystem::path path_;
};

std::string replace(std::string text, const std::string& from, const std::string& to) {
    const auto position = text.find(from);
    REQUIRE(position != std::string::npos);
    text.replace(position, from.size(), to);
    return text;
}

} // namespace

TEST_CASE("Bus module loads TOML and preserves local target and registry queries") {
    configuration_file file;
    ark::bus_plugin plugin;
    plugin.set_plugin_conf(file.write(configuration));
    ark::plugin_manager manager;
    manager.set_bus_id(ark::bus_addr(1, 1, 31, 1).bus_id);
    auto owned_module = ark::detail::make_bus_module();
    auto& module = *owned_module;
    module.set_plugin_manager(&manager);
    REQUIRE(module.init());
    CHECK(module.get_app_type("game") == ark::app_type::ark_app_game);
    CHECK(module.get_app_name(ark::app_type::ark_app_world) == "world");
    CHECK(module.get_app_type("missing") == ark::app_type::ark_app_default);
    CHECK(module.get_self_bus_name() == "1.1.31.1");
    CHECK(module.get_self_proc().bus_id == manager.get_bus_id());
    CHECK(module.get_self_proc().server_ep.to_string() == "tcp://127.0.0.1:9102");
    CHECK(module.get_self_proc().intranet_ep.get_ip().empty());
    CHECK(module.get_self_proc().intranet_ep.get_port() == 0);
    CHECK(module.get_self_proc().thread_num == 1);
    CHECK(module.get_self_proc().max_connection == 100);
    std::vector<ark::app_type> targets;
    REQUIRE(module.get_target_bus_relations(targets));
    REQUIRE(targets.size() == 1);
    CHECK(targets.front() == ark::app_type::ark_app_world);
    std::vector<const ark::proc_config*> processes;
    REQUIRE(module.get_other_proc(ark::app_type::ark_app_world, processes) == 1);
    CHECK(processes.front()->server_ep.to_string() == "udp://0.0.0.0:9101");
    ark::proc_config other;
    REQUIRE(module.get_other_proc(processes.front()->bus_id, other));
    CHECK(other.intranet_ep.to_string() == "tcp://world.internal:9111");
    CHECK_FALSE(module.get_other_proc(manager.get_bus_id(), other));
    CHECK(module.get_app_whole_name(other.bus_id) == "world-1.1.30.1");
    CHECK(module.get_reg_center().ip == "consul.internal");
    CHECK(module.get_reg_center().port == 8500);
    CHECK(module.get_reg_center().service_name == "ark-service");
    CHECK(module.get_reg_center().check_interval == "10s");
    CHECK(module.get_reg_center().check_timeout == "3s");
}

TEST_CASE("Bus rejects malformed identifiers endpoints numbers references and unknown fields") {
    configuration_file file;
    for (const auto& [from, to] :
        std::vector<std::pair<std::string, std::string>>{{"'1.1.30.1'", "'1.1.30'"}, {"'1.1.30.1'", "'1.1.30.1.2'"},
            {"'1.1.30.1'", "'256.1.30.1'"}, {"'1.1.30.1'", "'-1.1.30.1'"}, {"'1.1.30.1'", "'1.x.30.1'"},
            {"'1.1.30.1'", "'1..30.1'"}, {"'udp://0.0.0.0:9101'", "'tcp://localhost:65536'"},
            {"'udp://0.0.0.0:9101'", "'unknown://localhost:90'"}, {"'udp://0.0.0.0:9101'", "'tcp://localhost'"},
            {"'udp://0.0.0.0:9101'", "'tcp://localhost:-1'"}, {"'udp://0.0.0.0:9101'", "'tcp://bad host:90'"},
            {"'tcp://world.internal:9111'", "'invalid'"}, {"'tcp://world.internal:9111'", "'tcp://[localhost]:9011'"},
            {"'tcp://world.internal:9111'", "'tcp://[]:9011'"}, {"'tcp://world.internal:9111'", "'tcp://[::1:9011'"},
            {"'tcp://world.internal:9111'", "'tcp://::1:9011'"},
            {"'tcp://world.internal:9111'", "'tcp://[1:2:3]:9011'"},
            {"'tcp://world.internal:9111'", "'tcp://[::1]:65536'"}, {"type = 30", "type = 255"},
            {"type = 30", "type = 0"}, {"type = 30", "type = 30.0"}, {"threads = 2", "threads = 256"},
            {"threads = 2", "threads = -1"}, {"threads = 2", "threads = '2'"},
            {"max_connections = 5000", "max_connections = 4294967296"},
            {"max_connections = 5000", "max_connections = -1"}, {"port = 8500", "port = 65536"},
            {"port = 8500", "port = -1"}, {"target = 'world'", "target = 'missing'"},
            {"source = 'game'", "source = 'missing'"}, {"name = 'world'", "name = ''"},
            {"name = 'world'", "name = \"\\u0000\""}, {"host = 'consul.internal'", "host = ''"},
            {"check_timeout = '3s'", "check_timeout = 3"}, {"[bus]", "[bus]\nextra = 1"},
            {"type = 30", "type = 30\nextra = 1"}, {"source = 'game'", "source = 'game'\nextra = 1"},
            {"threads = 2", "threads = 2\nextra = 1"}, {"port = 8500", "port = 8500\nextra = 1"},
            {"[[bus.applications]]", "[[bus.wrong_applications]]"}, {"[bus.registry]", "[bus.wrong_registry]"},
            {"server_endpoint = 'udp://0.0.0.0:9101'", ""}})
    {
        INFO(from, " -> ", to);
        CHECK_THROWS_AS(
            ark::bus_config_detail::load(file.write(replace(configuration, from, to)), 0), std::runtime_error);
    }
    CHECK_THROWS_AS(ark::bus_config_detail::load(file.write("<xml/>"), 0), std::runtime_error);
    CHECK_THROWS_AS(ark::bus_config_detail::load(file.write("[bus"), 0), std::runtime_error);
}

TEST_CASE("Bus numeric boundaries retain exact public representation without narrowing") {
    configuration_file file;
    auto text = replace(configuration, "'1.1.30.1'", "'255.255.255.255'");
    text = replace(text, "max_connections = 5000", "max_connections = 4294967295");
    text = replace(text, "threads = 2", "threads = 255");
    text = replace(text, "port = 8500", "port = 65535");
    auto config = ark::bus_config_detail::load(file.write(text), -1);
    CHECK(config.self_proc.bus_id == -1);
    CHECK(config.self_proc.max_connection == 4294967295u);
    CHECK(config.self_proc.thread_num == 255);
    CHECK(config.reg_center.port == 65535);
    text = replace(text, "max_connections = 4294967295", "max_connections = 0");
    text = replace(text, "threads = 255", "threads = 0");
    text = replace(text, "port = 65535", "port = 0");
    config = ark::bus_config_detail::load(file.write(text), -1);
    CHECK(config.self_proc.max_connection == 0);
    CHECK(config.self_proc.thread_num == 0);
    CHECK(config.reg_center.port == 0);
}

TEST_CASE("Bus arrays may be empty and missing self remains unconfigured") {
    configuration_file file;
    const auto text = R"([bus]
applications = []
relations = []
processes = []
[bus.registry]
host = 'localhost'
port = 8500
service_name = 'ark-service'
check_interval = '10s'
check_timeout = '3s'
)";
    const auto config = ark::bus_config_detail::load(file.write(text), 1);
    CHECK(config.name2types.empty());
    CHECK(config.connection_relations.empty());
    CHECK(config.other_proc_list.empty());
    CHECK(config.self_proc.bus_id == 0);
    CHECK(config.self_proc.server_ep.get_port() == 0);
}

TEST_CASE("Bus diagnostics identify the file array index and invalid field") {
    configuration_file file;
    const auto path = file.write(replace(configuration, "threads = 2", "threads = 256"));
    try
    {
        static_cast<void>(ark::bus_config_detail::load(path, 0));
        FAIL("invalid threads accepted");
    }
    catch (const std::runtime_error& error)
    {
        const std::string message = error.what();
        CHECK(message.find(path) != std::string::npos);
        CHECK(message.find("bus.processes[0]") != std::string::npos);
        CHECK(message.find("threads") != std::string::npos);
    }
    std::filesystem::remove(path);
    CHECK_THROWS_AS(ark::bus_config_detail::load(path, 0), std::runtime_error);
}

TEST_CASE("Shipped Bus TOML retains the original configuration queries") {
    const auto config = ark::bus_config_detail::load(
        std::string(ARK_TEST_SOURCE_DIR) + "/config/bus.plugin.toml", ark::bus_addr(1, 1, 31, 1).bus_id);
    CHECK(config.name2types.size() == 7);
    CHECK(config.connection_relations.at(ark::app_type::ark_app_game).size() == 3);
    CHECK(config.self_proc.server_ep.to_string() == "tcp://0.0.0.0:10002");
    CHECK(config.self_proc.intranet_ep.to_string() == "tcp://127.0.0.1:10002");
    CHECK(config.self_proc.max_connection == 5000);
    CHECK(config.self_proc.thread_num == 2);
    CHECK(config.other_proc_list.size() == 8);
    CHECK(config.reg_center.ip == "127.0.0.1");
}

TEST_CASE("Bus rejects duplicate application names types relations and process IDs with field diagnostics") {
    configuration_file file;
    for (const auto& [from, to, field] :
        std::vector<std::tuple<std::string, std::string, std::string>>{{"name = 'game'", "name = 'world'", "name"},
            {"type = 31", "type = 30", "type"},
            {"[bus.registry]", "[[bus.relations]]\nsource = 'game'\ntarget = 'world'\n[bus.registry]", "source/target"},
            {"'1.1.31.1'", "'1.1.30.1'", "bus_id"}})
    {
        for (const auto self : {ark::bus_addr(1, 1, 30, 1).bus_id, ark::bus_addr(1, 1, 31, 1).bus_id})
        {
            const auto path = file.write(replace(configuration, from, to));
            try
            {
                static_cast<void>(ark::bus_config_detail::load(path, self));
                FAIL("duplicate configuration accepted");
            }
            catch (const std::runtime_error& error)
            {
                const std::string message = error.what();
                CHECK(message.find(path) != std::string::npos);
                CHECK(message.find("duplicate") != std::string::npos);
                CHECK(message.find(field) != std::string::npos);
            }
        }
    }
}

TEST_CASE("Bus module retains bracketed IPv6 endpoint hosts without brackets for arknet") {
    configuration_file file;
    auto text = replace(configuration, "'tcp://127.0.0.1:9102'", "'tcp://[::1]:9102'");
    text = replace(text, "intranet_endpoint = ''", "intranet_endpoint = 'tcp://[2001:db8::1]:0'");
    ark::bus_plugin plugin;
    plugin.set_plugin_conf(file.write(text));
    ark::plugin_manager manager;
    manager.set_bus_id(ark::bus_addr(1, 1, 31, 1).bus_id);
    auto owned_module = ark::detail::make_bus_module();
    auto& module = *owned_module;
    module.set_plugin_manager(&manager);
    REQUIRE(module.init());
    CHECK(module.get_self_proc().server_ep.get_ip() == "::1");
    CHECK(module.get_self_proc().server_ep.get_port() == 9102);
    CHECK(module.get_self_proc().intranet_ep.get_ip() == "2001:db8::1");
    CHECK(module.get_self_proc().intranet_ep.get_port() == 0);
    for (const auto& host : {"::", "::ffff:192.0.2.1", "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"})
    {
        const auto endpoint = ark::bus_config_detail::parse_endpoint(
            "tcp://[" + std::string(host) + "]:65535/path", "test", "server_endpoint");
        CHECK(endpoint.get_ip() == host);
        CHECK(endpoint.get_port() == 65535);
        CHECK(endpoint.get_path() == "/path");
    }
}
