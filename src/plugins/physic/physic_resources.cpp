// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

#include <nlohmann/json.hpp>

#include "../../config/configuration_reader.hpp"
#include "physic_resources.hpp"

namespace ark::physic_resources {

namespace {

[[noreturn]] void fail(const std::string& path, const std::string& field, const std::string& reason) {
    throw std::runtime_error(path + ": " + field + ": " + reason);
}

std::uint32_t unsigned_setting(
    const toml::node_view<const toml::node>& node, const std::string& path, const std::string& field) {
    const auto value = node.value<std::int64_t>();
    if (!node.is_integer() || !value || *value < 0 ||
        static_cast<std::uint64_t>(*value) > std::numeric_limits<std::uint32_t>::max())
    {
        fail(path, field, "expected an unsigned 32-bit integer");
    }
    return static_cast<std::uint32_t>(*value);
}

float number(const nlohmann::json& value, const std::string& path, const std::string& field) {
    if (!value.is_number())
    {
        fail(path, field, "expected a finite number");
    }
    const auto result = value.get<double>();
    if (!std::isfinite(result) || std::abs(result) > std::numeric_limits<float>::max())
    {
        fail(path, field, "number exceeds the finite float range");
    }
    return static_cast<float>(result);
}

std::array<float, 3> vector(const nlohmann::json& value, const std::string& path, const std::string& field) {
    if (!value.is_array() || value.size() != 3)
    {
        fail(path, field, "expected three coordinates");
    }
    return {number(value[0], path, field), number(value[1], path, field), number(value[2], path, field)};
}

} // namespace

settings read_settings(const std::string& path) {
    const auto table = config_detail::read_toml(path);
    settings result;
    const auto simulate = table["simulate"].value<bool>();
    const auto host = table["debugger"]["host"].value<std::string>();
    if (!simulate || !host || host->find('\0') != std::string::npos)
    {
        fail(path, "settings", "expected simulate boolean and debugger.host string");
    }
    result.simulate = *simulate;
    result.dispatcher_threads = unsigned_setting(table["dispatcher_threads"], path, "dispatcher_threads");
    result.frequency_hz = unsigned_setting(table["frequency_hz"], path, "frequency_hz");
    if (result.frequency_hz == 0)
    {
        fail(path, "frequency_hz", "must be positive");
    }
    result.debugger_host = *host;
    const auto port = unsigned_setting(table["debugger"]["port"], path, "debugger.port");
    if (port > std::numeric_limits<std::uint16_t>::max())
    {
        fail(path, "debugger.port", "exceeds the port range");
    }
    result.debugger_port = static_cast<std::uint16_t>(port);
    result.debugger_timeout_ms = unsigned_setting(table["debugger"]["timeout_ms"], path, "debugger.timeout_ms");
    return result;
}

std::vector<collider> read_colliders(const std::string& path) {
    try
    {
        const auto document = nlohmann::json::parse(config_detail::read_file(path, "collider JSON"));
        if (!document.is_object() || !document.at("version").is_number_integer() || document.at("version") != 1 ||
            !document.at("colliders").is_array())
        {
            fail(path, "/", "expected version 1 and a colliders array");
        }
        std::vector<collider> result;
        for (const auto& entry : document.at("colliders"))
        {
            const auto field = "/colliders/" + std::to_string(result.size());
            collider value;
            const auto type = entry.at("type").get<std::string>();
            value.position = vector(entry.at("position"), path, field + "/position");
            if (type == "terrain")
            {
                value.type = collider_type::terrain;
            }
            else
            {
                value.trigger = entry.at("trigger").get<bool>();
                if (type == "mesh")
                {
                    value.type = collider_type::mesh;
                    value.convex = entry.at("convex").get<bool>();
                    const auto& vertices = entry.at("vertices");
                    const auto& triangles = entry.at("triangles");
                    if (!vertices.is_array() || vertices.empty() ||
                        vertices.size() > std::numeric_limits<std::uint32_t>::max() || !triangles.is_array() ||
                        triangles.empty() || triangles.size() % 3 != 0 ||
                        triangles.size() > std::numeric_limits<std::uint32_t>::max())
                    {
                        fail(path, field, "expected bounded vertices and triangle indices");
                    }
                    for (const auto& vertex : vertices)
                    {
                        value.vertices.push_back(vector(vertex, path, field + "/vertices"));
                    }
                    for (const auto& index : triangles)
                    {
                        if (!index.is_number_integer() || index < 0 || index >= value.vertices.size())
                        {
                            fail(path, field + "/triangles", "index is outside the vertices array");
                        }
                        value.triangles.push_back(index.get<std::uint32_t>());
                    }
                }
                else if (type == "box" || type == "capsule" || type == "sphere")
                {
                    value.center = vector(entry.at("center"), path, field + "/center");
                    if (type == "box")
                    {
                        value.type = collider_type::box;
                        value.size = vector(entry.at("size"), path, field + "/size");
                    }
                    else
                    {
                        value.type = type == "capsule" ? collider_type::capsule : collider_type::sphere;
                        value.radius = number(entry.at("radius"), path, field + "/radius");
                        if (value.radius < 0)
                        {
                            fail(path, field + "/radius", "must not be negative");
                        }
                        if (type == "capsule")
                        {
                            value.height = number(entry.at("height"), path, field + "/height");
                            if (value.height < 0)
                            {
                                fail(path, field + "/height", "must not be negative");
                            }
                        }
                    }
                }
                else
                {
                    fail(path, field + "/type", "unknown collider type");
                }
            }
            result.push_back(std::move(value));
        }
        return result;
    }
    catch (const nlohmann::json::exception& error)
    { fail(path, "/", error.what()); }
}

} // namespace ark::physic_resources
