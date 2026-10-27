// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

#include <array>
#include <bitset>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace ark::entity_json {

inline constexpr std::array<std::string_view, 8> mask_names = {
    "sync_view", "sync_self", "real_time", "save", "sync_team", "sync_guild", "sync_map", "log"};

enum class value_type
{
    boolean,
    int32,
    uint32,
    int64,
    uint64,
    float32,
    float64,
    string,
    guid
};

// Integers retain their signedness and full width. GUIDs use canonical decimal strings.
using value = std::variant<bool, std::int64_t, std::uint64_t, double, std::string>;

struct field
{
    std::string name;
    std::uint32_t index{0};
    value_type type{value_type::int32};
    std::bitset<16> mask{};
    std::string description;
};

struct relation
{
    std::string name;
    std::uint32_t index{0};
    std::string class_name;
    std::bitset<16> mask{};
    std::string description;
};

struct class_definition
{
    std::string name;
    std::vector<field> fields;
    std::vector<relation> tables;
    std::vector<relation> containers;
    std::string description;
};

using row = std::map<std::string, value>;

struct config_definition
{
    std::string class_name;
    std::vector<row> rows;
};

struct document
{
    std::vector<class_definition> classes;
    std::vector<config_definition> configs;
};

class error : public std::runtime_error
{
public:
    error(std::string source, std::string pointer, std::string reason);
    const std::string& source() const noexcept;
    const std::string& pointer() const noexcept;

private:
    std::string source_;
    std::string pointer_;
};

// These functions share validation between the runtime loader and offline exporters.
document parse(std::string_view text, std::string_view source = "<input>");
document load(const std::string& path);
void validate(const document& input, std::string_view source = "<model>");
std::string serialize(const document& input);
std::string_view type_name(value_type type);
value_type parse_type(std::string_view name, std::string_view source = "<input>", std::string_view pointer = "/type");

} // namespace ark::entity_json
