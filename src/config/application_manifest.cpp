// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <cstdint>
#include <initializer_list>
#include <string_view>
#include <utility>

#include <ark/application_manifest.hpp>
#include "configuration_reader.hpp"

namespace ark {
namespace {

[[noreturn]] void fail_at(const std::string& path, const char* reason, const toml::source_region& source) {
    throw application_manifest_error(path, reason, source.begin.line, source.begin.column);
}

void validate_keys(const toml::table& table, std::initializer_list<std::string_view> allowed, const std::string& path) {
    for (const auto& [key, value] : table)
    {
        bool known = false;
        for (const auto candidate : allowed)
        {
            known = known || key.str() == candidate;
        }
        if (!known)
        {
            fail_at(path, "unknown manifest field", key.source());
        }
    }
}

std::string read_string(
    const toml::table& table, std::string_view key, const std::string& path, bool required = false) {
    const auto* node = table.get(key);
    if (node == nullptr)
    {
        if (required)
        {
            fail_at(path, "missing plugin name", table.source());
        }
        return {};
    }
    const auto* value = node->as_string();
    if (value == nullptr)
    {
        fail_at(path, "manifest field must be a string", node->source());
    }
    if (value->get().find('\0') != std::string::npos)
    {
        fail_at(path, "manifest string contains NUL", node->source());
    }
    if (required && value->get().empty())
    {
        fail_at(path, "plugin name must not be empty", node->source());
    }
    return value->get();
}

logger_settings parse_logger(
    const toml::table& table, const std::string& path, logger_settings result, const std::string& prefix = "logging.") {
    for (const auto& [key, node] : table)
    {
        const auto field = prefix + std::string(key.str());
        const auto invalid = [&](const char* reason) {
            throw application_manifest_error(
                path, field + " " + reason, node.source().begin.line, node.source().begin.column);
        };
        if (key == "loggers")
        {
            continue;
        }
        if (key == "console" || key == "file")
        {
            const auto value = node.value<bool>();
            if (!node.is_boolean() || !value)
            {
                invalid("must be a boolean");
            }
            (key == "console" ? result.console : result.file) = *value;
        }
        else if (key == "queue_count")
        {
            const auto value = node.value<std::int64_t>();
            if (!node.is_integer() || !value || *value < 0 || *value > 65535)
            {
                invalid("must be an integer between 0 and 65535");
            }
            result.queue_count = static_cast<std::size_t>(*value);
        }
        else if (key == "level" || key == "pattern" || key == "output_path")
        {
            const auto value = node.value<std::string>();
            if (!value || value->empty() || value->find('\0') != std::string::npos)
            {
                invalid("must be a non-empty string without NUL");
            }
            if (key == "level")
            {
                if (*value != "trace" && *value != "debug" && *value != "info" && *value != "warn" &&
                    *value != "error" && *value != "critical" && *value != "off")
                {
                    invalid("must be trace, debug, info, warn, error, critical or off");
                }
                result.level = *value;
            }
            else if (key == "pattern")
            {
                result.pattern = *value;
            }
            else
            {
                result.output_path = *value;
            }
        }
        else
        {
            invalid("is unknown");
        }
    }
    if (!result.console && !result.file)
    {
        fail_at(path, "logging requires console or file output", table.source());
    }
    return result;
}

logging_settings parse_logging(const toml::node* node, const std::string& path) {
    logging_settings result;
    if (node == nullptr)
    {
        return result;
    }
    const auto* table = node->as_table();
    if (table == nullptr)
    {
        fail_at(path, "logging must be a table", node->source());
    }
    static_cast<logger_settings&>(result) = parse_logger(*table, path, result);
    if (const auto* loggers = table->get("loggers"))
    {
        const auto* named = loggers->as_table();
        if (named == nullptr)
        {
            fail_at(path, "logging.loggers must be a table", loggers->source());
        }
        for (const auto& [key, entry] : *named)
        {
            const auto* settings = entry.as_table();
            if (key.str().empty() || key.str().find('\0') != std::string_view::npos || settings == nullptr)
            {
                fail_at(path, "logging.loggers entries require a non-empty name and table", entry.source());
            }
            if (settings->contains("loggers"))
            {
                fail_at(path, "nested logging.loggers is invalid", settings->source());
            }
            result.loggers.emplace(std::string(key.str()),
                parse_logger(*settings, path, result, "logging.loggers." + std::string(key.str()) + "."));
        }
    }
    return result;
}

application_manifest parse_toml(const std::string& path, std::string_view contents) {
    const auto document = config_detail::parse_toml(path, contents, "invalid TOML syntax or encoding");
    validate_keys(document, {"application", "plugins", "logging"}, path);
    const auto* application_node = document.get("application");
    if (application_node == nullptr)
    {
        throw application_manifest_error(path, "missing application table", 1, 1);
    }
    const auto* application = application_node->as_table();
    if (application == nullptr)
    {
        fail_at(path, "application must be a table", application_node->source());
    }
    validate_keys(*application, {"plugin_library_dir", "plugin_config_dir"}, path);
    application_manifest result;
    result.logging = parse_logging(document.get("logging"), path);
    result.plugin_library_dir = read_string(*application, "plugin_library_dir", path);
    result.plugin_config_dir = read_string(*application, "plugin_config_dir", path);
    const auto* plugins_node = document.get("plugins");
    if (plugins_node == nullptr)
    {
        return result;
    }
    const auto* plugins = plugins_node->as_array();
    if (plugins == nullptr)
    {
        fail_at(path, "plugins must be an array of tables", plugins_node->source());
    }
    for (const auto& node : *plugins)
    {
        const auto* plugin = node.as_table();
        if (plugin == nullptr)
        {
            fail_at(path, "plugin entry must be a table", node.source());
        }
        validate_keys(*plugin, {"name", "config"}, path);
        auto name = read_string(*plugin, "name", path, true);
        auto config = read_string(*plugin, "config", path);
        if (!result.plugins.emplace(std::move(name), std::move(config)).second)
        {
            fail_at(path, "duplicate plugin name", plugin->source());
        }
    }
    return result;
}

} // namespace

application_manifest_error::application_manifest_error(
    const std::string& path, const std::string& reason, std::size_t line, std::size_t column)
    : std::runtime_error(config_detail::error_message(path, reason, line, column))
    , path_(path)
    , line_(line)
    , column_(column) {
}

const std::string& application_manifest_error::path() const noexcept {
    return path_;
}

std::size_t application_manifest_error::line() const noexcept {
    return line_;
}

std::size_t application_manifest_error::column() const noexcept {
    return column_;
}

application_manifest load_application_manifest(const std::string& path) {
    if (!path.ends_with(".toml"))
    {
        throw application_manifest_error(
            path, "application manifest path must end in .toml; XML manifests are no longer supported");
    }
    try
    {
        const auto contents = config_detail::read_file(path, "manifest");
        return parse_toml(path, contents);
    }
    catch (const config_detail::read_error& error)
    { throw application_manifest_error(path, error.reason, error.line, error.column); }
}

} // namespace ark
