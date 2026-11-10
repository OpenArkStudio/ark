// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>

#include "../../config/configuration_reader.hpp"

namespace ark::plugin_config_detail {

[[noreturn]] inline void fail(const std::string& path, std::string_view key, std::string_view reason) {
    throw std::runtime_error(
        config_detail::diagnostic_path(path) + ": " + std::string(key) + ": " + std::string(reason));
}

inline void check_keys(const toml::table& table, const std::string& path, std::string_view section,
    std::initializer_list<std::string_view> allowed) {
    for (const auto& [key, value] : table) {
        if (std::ranges::find(allowed, key.str()) == allowed.end()) {
            fail(path, std::string(section) + "." + std::string(key.str()), "unknown setting");
        }
    }
}

inline toml::table read(const std::string& path, std::string_view section) {
    auto document = config_detail::read_toml(path);
    check_keys(document, path, "root", {section});
    if (!document[section].is_table()) {
        fail(path, section, "required table is missing");
    }
    return document;
}

inline std::string host(const toml::table& table, const std::string& path) {
    const auto value = table["host"].value<std::string>();
    if (!value || value->empty() || value->find('\0') != std::string::npos) {
        fail(path, "host", "expected a nonempty string without NUL");
    }
    return *value;
}

inline std::uint32_t integer(const toml::table& table, const std::string& path, std::string_view key,
    std::int64_t minimum, std::int64_t maximum) {
    const auto value = table[key].value<std::int64_t>();
    if (!value || !table[key].is_integer() || *value < minimum || *value > maximum) {
        fail(path, key, "expected an integer from " + std::to_string(minimum) + " to " + std::to_string(maximum));
    }
    return static_cast<std::uint32_t>(*value);
}

struct http_configuration {
    std::string host;
    std::uint32_t port;
    std::uint32_t threads;
};

inline http_configuration load_http(const std::string& path) {
    const auto document = read(path, "http");
    const auto& table = *document["http"].as_table();
    check_keys(table, path, "http", {"host", "port", "threads"});
    return {host(table, path), integer(table, path, "port", 0, 65535), integer(table, path, "threads", 1, 255)};
}

struct consul_configuration {
    std::string host;
    std::uint32_t port;
};

inline consul_configuration load_consul(const std::string& path) {
    const auto document = read(path, "consul");
    const auto& table = *document["consul"].as_table();
    check_keys(table, path, "consul", {"host", "port"});
    return {host(table, path), integer(table, path, "port", 1, 65535)};
}

} // namespace ark::plugin_config_detail
