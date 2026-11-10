// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <filesystem>
#include <fstream>
#include <iterator>
#include <utility>

#include "configuration_reader.hpp"

namespace ark::config_detail {

std::string diagnostic_path(std::string_view path) {
    std::string result;
    for (const auto character : path) {
        switch (character) {
        case '\0':
            result += "\\0";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        default:
            result += character;
            break;
        }
    }
    return result;
}

std::string error_message(const std::string& path, const std::string& reason, std::size_t line, std::size_t column) {
    auto message = diagnostic_path(path);
    if (line != 0) {
        message += ":" + std::to_string(line) + ":" + std::to_string(column);
    }
    return message + ": " + reason;
}

[[noreturn]] void fail_at(
    const std::string& path, const std::string& reason, std::string_view contents, std::size_t offset) {
    std::size_t line = 1;
    std::size_t column = 1;
    // Match the TOML parser's source positions: columns count code points,
    // and the optional UTF-8 BOM is not part of the first line.
    const std::size_t begin = contents.starts_with("\xef\xbb\xbf") ? 3 : 0;
    for (std::size_t index = begin; index < offset && index < contents.size(); ++index) {
        if (contents[index] == '\n') {
            ++line;
            column = 1;
        } else if ((static_cast<unsigned char>(contents[index]) & 0xc0u) != 0x80u) {
            ++column;
        }
    }
    throw read_error(path, reason, line, column);
}

read_error::read_error(const std::string& path, std::string reason, std::size_t line, std::size_t column)
    : std::runtime_error(error_message(path, reason, line, column))
    , reason(std::move(reason))
    , line(line)
    , column(column) {
}

std::string read_file(const std::string& path, std::string_view kind) {
    if (path.find('\0') != std::string::npos) {
        throw read_error(path, std::string(kind) + " path contains NUL");
    }
    std::string contents;
    try {
        const std::filesystem::path native_path(std::u8string(path.begin(), path.end()));
        if (std::filesystem::is_directory(native_path)) {
            throw read_error(path, std::string(kind) + " path is a directory");
        }
        std::ifstream input(native_path, std::ios::binary);
        input.exceptions(std::ios::badbit);
        if (!input) {
            throw read_error(path, "could not open " + std::string(kind));
        }
        contents.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    } catch (const std::filesystem::filesystem_error&) {
        throw read_error(path, "could not open " + std::string(kind) + " path");
    } catch (const std::ios_base::failure&) {
        throw read_error(path, "could not read " + std::string(kind));
    }
    const auto nul = contents.find('\0');
    if (nul != std::string::npos) {
        fail_at(path, std::string(kind) + " contains NUL", contents, nul);
    }
    return contents;
}

toml::table parse_toml(const std::string& path, std::string_view contents, const std::string& reason) {
    try {
        return toml::parse(contents);
    } catch (const toml::parse_error& error) {
        throw read_error(path, reason, error.source().begin.line, error.source().begin.column);
    }
}

toml::table read_toml(const std::string& path) {
    return parse_toml(path, read_file(path, "configuration"), "cannot parse TOML configuration");
}

} // namespace ark::config_detail
