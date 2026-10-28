// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <variant>

#include "ark/platform.hpp"

namespace ark::entity_sync {

struct guid_value
{
    guid_t value{};
    bool operator==(const guid_value&) const = default;
};

using scalar_value = std::variant<bool, std::int32_t, std::uint32_t, std::int64_t, std::uint64_t, float, double,
    std::string, guid_value>;

struct scalar_snapshot
{
    guid_t entity_id{};
    std::string class_name;
    std::map<std::uint32_t, scalar_value> fields;
    bool operator==(const scalar_snapshot&) const = default;
};

struct snapshot_limits
{
    std::size_t max_fields{1024};
    // Combined class-name and selected string-field bytes, including embedded NULs.
    std::size_t max_string_bytes{1024 * 1024};
};

} // namespace ark::entity_sync
