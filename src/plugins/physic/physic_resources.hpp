// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace ark::physic_resources {

struct settings
{
    bool simulate{};
    std::uint32_t dispatcher_threads{};
    std::uint32_t frequency_hz{};
    std::string debugger_host;
    std::uint16_t debugger_port{};
    std::uint32_t debugger_timeout_ms{};
};

enum class collider_type
{
    box,
    capsule,
    sphere,
    mesh,
    terrain
};

struct collider
{
    collider_type type{};
    std::array<float, 3> position{};
    std::array<float, 3> center{};
    std::array<float, 3> size{};
    float radius{};
    float height{};
    bool trigger{};
    bool convex{};
    std::vector<std::array<float, 3>> vertices;
    std::vector<std::uint32_t> triangles;
};

settings read_settings(const std::string& path);
std::vector<collider> read_colliders(const std::string& path);

} // namespace ark::physic_resources
