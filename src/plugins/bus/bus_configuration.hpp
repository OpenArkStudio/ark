// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

#include <algorithm>
#include <bit>
#include <charconv>
#include <limits>
#include <regex>
#include <set>

#include <arknet/external/asio.hpp>

#include "../detail/plugin_configuration.hpp"
#include "ark/bus.hpp"

namespace ark::bus_config_detail {

inline std::string string(
    const toml::table& table, const std::string& path, std::string_view key, bool allow_empty = false) {
    const auto value = table[key].value<std::string>();
    if (!value || (!allow_empty && value->empty()) || value->find('\0') != std::string::npos)
    {
        plugin_config_detail::fail(path, key,
            allow_empty ? "expected a string without NUL bytes" : "expected a nonempty string without NUL bytes");
    }
    return *value;
}

inline const toml::array& array(const toml::table& table, const std::string& path, std::string_view key) {
    const auto* value = table[key].as_array();
    if (!value || (!value->empty() && !value->is_array_of_tables()))
    {
        plugin_config_detail::fail(path, key, "expected an array of tables");
    }
    return *value;
}

inline bus_id_t bus_id(const std::string& value, const std::string& path) {
    std::uint32_t id = 0;
    std::size_t begin = 0;
    for (unsigned component = 0; component < 4; ++component)
    {
        const auto end = value.find('.', begin);
        const auto limit = end == std::string::npos ? value.size() : end;
        unsigned number = 0;
        const auto result = std::from_chars(value.data() + begin, value.data() + limit, number);
        if (result.ec != std::errc{} || result.ptr != value.data() + limit || number > 255 ||
            (component < 3) != (end != std::string::npos))
        {
            plugin_config_detail::fail(path, "bus_id", "expected four decimal components in 0..255");
        }
        id = (id << 8) | number;
        begin = limit + 1;
    }
    static_assert(sizeof(bus_id_t) == sizeof(id));
    return std::bit_cast<bus_id_t>(id);
}

inline endpoint parse_endpoint(const std::string& value, const std::string& path, std::string_view key) {
    endpoint ep;
    ep.set_protocol(proto_type::unknown);
    ep.set_ip("");
    ep.set_port(0);
    ep.set_path("");
    if (value.empty())
    {
        return ep; // An empty endpoint keeps the corresponding listener disabled.
    }
    static const std::regex pattern(R"(([^:/]+)://(\[[^\]]+\]|[^:/[:space:]\[\]]+):([0-9]+)(/[^[:space:]]*)?)");
    std::smatch match;
    if (!std::regex_match(value, match, pattern))
    {
        plugin_config_detail::fail(path, key, "expected protocol://host:port or an empty string");
    }
    unsigned port = 0;
    const auto port_text = match[3].str();
    const auto result = std::from_chars(port_text.data(), port_text.data() + port_text.size(), port);
    const auto protocol = endpoint::socket::str2proto(match[1].str());
    if (result.ec != std::errc{} || port > 65535 || protocol == proto_type::unknown)
    {
        plugin_config_detail::fail(path, key, "unsupported protocol or port outside 0..65535");
    }
    auto host = match[2].str();
    if (host.front() == '[')
    {
        host = host.substr(1, host.size() - 2);
        asio::error_code error;
        static_cast<void>(asio::ip::make_address_v6(host, error));
        if (error)
        {
            plugin_config_detail::fail(path, key, "expected a valid bracketed IPv6 literal");
        }
    }
    ep.set_protocol(protocol);
    ep.set_ip(host);
    ep.set_port(static_cast<std::uint16_t>(port));
    ep.set_path(match[4].str());
    return ep;
}

inline app_config load(const std::string& path, bus_id_t self_bus_id) {
    using plugin_config_detail::check_keys;
    using plugin_config_detail::fail;
    using plugin_config_detail::integer;
    const auto document = plugin_config_detail::read(path, "bus");
    const auto& table = *document["bus"].as_table();
    check_keys(table, path, "bus", {"applications", "relations", "processes", "registry"});
    app_config config;
    // Public endpoint's default constructor does not initialize its numeric fields.
    config.self_proc.server_ep = parse_endpoint("", path, "server_endpoint");
    config.self_proc.intranet_ep = parse_endpoint("", path, "intranet_endpoint");
    std::size_t index = 0;
    for (const auto& node : array(table, path, "applications"))
    {
        const auto location = path + ": bus.applications[" + std::to_string(index++) + "]";
        const auto& app = *node.as_table();
        check_keys(app, location, "application", {"name", "type"});
        const auto name = string(app, location, "name");
        const auto type = static_cast<app_type>(integer(app, location, "type", 1, 254));
        if (!config.name2types.insert({name, type}).second)
        {
            fail(location, "name", "duplicate application name: " + name);
        }
        if (!config.type2names.insert({type, name}).second)
        {
            fail(location, "type", "duplicate application type");
        }
    }
    index = 0;
    for (const auto& node : array(table, path, "relations"))
    {
        const auto location = path + ": bus.relations[" + std::to_string(index++) + "]";
        const auto& relation = *node.as_table();
        check_keys(relation, location, "relation", {"source", "target"});
        const auto source = config.name2types.find(string(relation, location, "source"));
        const auto target = config.name2types.find(string(relation, location, "target"));
        if (source == config.name2types.end() || target == config.name2types.end())
        {
            fail(location, "source/target", "expected configured application names");
        }
        auto& targets = config.connection_relations[source->second];
        if (std::find(targets.begin(), targets.end(), target->second) != targets.end())
        {
            fail(location, "source/target", "duplicate directed relation");
        }
        targets.push_back(target->second);
    }
    const auto* registry = table["registry"].as_table();
    if (!registry)
    {
        fail(path, "bus.registry", "required table is missing");
    }
    const auto registry_path = path + ": bus.registry";
    check_keys(
        *registry, registry_path, "registry", {"host", "port", "service_name", "check_interval", "check_timeout"});
    config.reg_center.ip = string(*registry, registry_path, "host");
    config.reg_center.port = static_cast<std::uint16_t>(integer(*registry, registry_path, "port", 0, 65535));
    config.reg_center.service_name = string(*registry, registry_path, "service_name");
    config.reg_center.check_interval = string(*registry, registry_path, "check_interval");
    config.reg_center.check_timeout = string(*registry, registry_path, "check_timeout");
    index = 0;
    std::set<bus_id_t> process_ids;
    for (const auto& node : array(table, path, "processes"))
    {
        const auto location = path + ": bus.processes[" + std::to_string(index++) + "]";
        const auto& process = *node.as_table();
        check_keys(process, location, "process",
            {"bus_id", "server_endpoint", "intranet_endpoint", "max_connections", "threads"});
        proc_config proc;
        proc.bus_id = bus_id(string(process, location, "bus_id"), location);
        if (!process_ids.insert(proc.bus_id).second)
        {
            fail(location, "bus_id", "duplicate process Bus ID");
        }
        proc.max_connection =
            integer(process, location, "max_connections", 0, std::numeric_limits<std::uint32_t>::max());
        proc.thread_num = static_cast<std::uint8_t>(integer(process, location, "threads", 0, 255));
        proc.server_ep =
            parse_endpoint(string(process, location, "server_endpoint", true), location, "server_endpoint");
        proc.intranet_ep =
            parse_endpoint(string(process, location, "intranet_endpoint", true), location, "intranet_endpoint");
        if (proc.bus_id == self_bus_id)
        {
#ifdef ARK_DOCKER_ALPINE
            proc.intranet_ep.set_ip(getenv("MY_POD_IP"));
#endif
            config.self_proc = proc;
        }
        else
        {
            config.other_proc_list.insert({proc.bus_id, proc});
        }
    }
    return config;
}

} // namespace ark::bus_config_detail
