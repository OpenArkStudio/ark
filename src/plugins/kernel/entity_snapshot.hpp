// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <type_traits>
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

namespace detail {

// Construct the active alternative directly: a throwing string allocation must
// not unwind a partially copied variant. Publish only complete values by move.
static_assert(std::is_nothrow_move_constructible_v<scalar_value>);
static_assert(std::is_nothrow_move_assignable_v<scalar_value>);

inline scalar_value copy_scalar_value(const scalar_value& input) {
    return std::visit(
        [](const auto& item) -> scalar_value {
            return scalar_value{std::in_place_type<std::decay_t<decltype(item)>>, item};
        },
        input);
}

inline std::map<std::uint32_t, scalar_value> copy_scalar_fields(const std::map<std::uint32_t, scalar_value>& input) {
    std::map<std::uint32_t, scalar_value> result;
    for (const auto& [index, item] : input)
    {
        result.emplace_hint(result.end(), index, copy_scalar_value(item));
    }
    return result;
}

inline scalar_snapshot copy_scalar_snapshot(const scalar_snapshot& input) {
    return {input.entity_id, input.class_name, copy_scalar_fields(input.fields)};
}

} // namespace detail

} // namespace ark::entity_sync
