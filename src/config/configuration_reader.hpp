// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

#include <toml++/toml.hpp>

namespace ark::config_detail {

// Private transport/parser diagnostics. Schema owners translate these when they
// expose a more specific public exception, such as application_manifest_error.
class read_error : public std::runtime_error {
public:
    read_error(const std::string& path, std::string reason, std::size_t line = 0, std::size_t column = 0);

    std::string reason;
    std::size_t line;
    std::size_t column;
};

std::string diagnostic_path(std::string_view path);
std::string error_message(const std::string& path, const std::string& reason, std::size_t line, std::size_t column);
[[noreturn]] void fail_at(
    const std::string& path, const std::string& reason, std::string_view contents, std::size_t offset);
std::string read_file(const std::string& path, std::string_view kind);
toml::table parse_toml(const std::string& path, std::string_view contents, const std::string& reason);
toml::table read_toml(const std::string& path);

} // namespace ark::config_detail
