// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <ark/kernel/entity_json.hpp>

namespace ark::entity_export {

inline std::filesystem::path utf8_path(std::string_view value) {
    std::u8string text;
    text.reserve(value.size());
    for (unsigned char c : value)
    {
        text.push_back(static_cast<char8_t>(c));
    }
    return std::filesystem::path(text);
}

inline std::string utf8_string(const std::filesystem::path& value) {
    const auto text = value.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

enum class cell_kind
{
    text,
    number,
    boolean,
    empty
};

struct cell
{
    std::string text;
    cell_kind kind{cell_kind::text};
};

struct table
{
    std::string source;
    std::vector<std::vector<cell>> rows;
};

table parse_csv(std::string_view input, std::string source);
table read_table(const std::filesystem::path& path, const std::string& sheet = "");
std::map<std::string, table> read_workbook(const std::filesystem::path& path);
entity_json::document read_fields(const table& input);
void add_config(entity_json::document& output, const std::string& class_name, const table& input);
std::string cpp_bindings(const entity_json::document& input);
std::string csharp_bindings(const entity_json::document& input);
void export_bundle(const entity_json::document& input, const std::filesystem::path& output, bool replace = false,
    bool generate_code = true);

} // namespace ark::entity_export
