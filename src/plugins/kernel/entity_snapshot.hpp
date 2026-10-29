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

// Forward factories instead of owning temporaries through container/shared_ptr
// APIs. C++20 constructs the conversion result directly in the destination;
// even a deleted copy/move constructor is supported. The factory must return a
// prvalue, and the adapter must be consumed synchronously within its captures.
template<typename factory_type>
struct direct_value
{
    factory_type create;
    using value_type = std::invoke_result_t<const factory_type&>;

    operator value_type() const {
        return create();
    }
};

template<typename factory_type>
direct_value(factory_type) -> direct_value<factory_type>;

// Construct the active alternative directly: a throwing string allocation must
// not unwind a partially copied variant. Do not move the result through another
// constructor: MSVC Debug strings can allocate proxies even in noexcept moves.
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
        result.emplace_hint(result.end(), index, direct_value{[&] { return copy_scalar_value(item); }});
    }
    return result;
}

inline scalar_snapshot copy_scalar_snapshot(const scalar_snapshot& input) {
    return {input.entity_id, input.class_name, copy_scalar_fields(input.fields)};
}

} // namespace detail

} // namespace ark::entity_sync
