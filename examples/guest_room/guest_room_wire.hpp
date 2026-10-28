// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>

#include "ark/kernel/entity_stream.hpp"
#include "ark/proto/guest_room.pb.h"
#include "guest_room_protocol.hpp"

namespace ark::guest_room {
namespace wire = protobuf::guest_room;
inline constexpr auto player_schema = "ark.guest-player.v1";
inline constexpr std::array<std::uint32_t, 9> player_fields{100, 101, 102, 103, 104, 105, 106, 107, 108};
inline constexpr entity_sync::snapshot_limits player_limits{9, 128};

inline guid_t decode_guid(std::uint64_t value) {
    if (value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
        value > static_cast<std::uint64_t>(std::numeric_limits<guid_t>::max()))
    {
        throw std::invalid_argument("Scalar GUID exceeds the native width");
    }
    return static_cast<guid_t>(value);
}

inline std::uint64_t encode_identity(guid_t value) {
    if (value <= 0 || value > std::numeric_limits<std::int64_t>::max())
    {
        throw std::invalid_argument("Room Entity GUID must be positive and fit signed 64 bits");
    }
    return static_cast<std::uint64_t>(value);
}

inline void encode_field(std::uint32_t index, const entity_sync::scalar_value& value, wire::scalar_field& output) {
    if (index == 0)
    {
        throw std::invalid_argument("Scalar field index must be positive");
    }
    output.set_index(index);
    std::visit(
        [&](const auto& item) {
            using type = std::decay_t<decltype(item)>;
            if constexpr (std::is_floating_point_v<type>)
            {
                if (!std::isfinite(item))
                {
                    throw std::invalid_argument("Non-finite scalar value");
                }
            }
            if constexpr (std::is_same_v<type, bool>)
            {
                output.set_bool_value(item);
            }
            else if constexpr (std::is_same_v<type, std::int32_t>)
            {
                output.set_int32_value(item);
            }
            else if constexpr (std::is_same_v<type, std::uint32_t>)
            {
                output.set_uint32_value(item);
            }
            else if constexpr (std::is_same_v<type, std::int64_t>)
            {
                output.set_int64_value(item);
            }
            else if constexpr (std::is_same_v<type, std::uint64_t>)
            {
                output.set_uint64_value(item);
            }
            else if constexpr (std::is_same_v<type, float>)
            {
                output.set_float_value(item);
            }
            else if constexpr (std::is_same_v<type, double>)
            {
                output.set_double_value(item);
            }
            else if constexpr (std::is_same_v<type, std::string>)
            {
                output.set_string_value(item);
            }
            else if constexpr (std::is_same_v<type, entity_sync::guid_value>)
            {
                if (item.value < std::numeric_limits<std::int64_t>::min() ||
                    item.value > std::numeric_limits<std::int64_t>::max())
                {
                    throw std::invalid_argument("Scalar GUID exceeds the wire width");
                }
                output.set_guid_value(static_cast<std::int64_t>(item.value));
            }
        },
        value);
}

inline entity_sync::scalar_value decode_field(const wire::scalar_field& input) {
    if (input.index() == 0)
    {
        throw std::invalid_argument("Scalar field index must be positive");
    }
    switch (input.value_case())
    {
    case wire::scalar_field::kBoolValue:
        return input.bool_value();
    case wire::scalar_field::kInt32Value:
        return input.int32_value();
    case wire::scalar_field::kUint32Value:
        return input.uint32_value();
    case wire::scalar_field::kInt64Value:
        return input.int64_value();
    case wire::scalar_field::kUint64Value:
        return input.uint64_value();
    case wire::scalar_field::kFloatValue:
        if (std::isfinite(input.float_value()))
        {
            return input.float_value();
        }
        break;
    case wire::scalar_field::kDoubleValue:
        if (std::isfinite(input.double_value()))
        {
            return input.double_value();
        }
        break;
    case wire::scalar_field::kStringValue:
        return input.string_value();
    case wire::scalar_field::kGuidValue:
        if (input.guid_value() >= std::numeric_limits<guid_t>::min() &&
            input.guid_value() <= std::numeric_limits<guid_t>::max())
        {
            return entity_sync::guid_value{static_cast<guid_t>(input.guid_value())};
        }
        break;
    default:
        break;
    }
    throw std::invalid_argument("Missing or non-finite scalar value");
}

inline void encode(const entity_sync::scalar_baseline& input, wire::scalar_baseline& output) {
    entity_sync::scalar_publisher validated(input);
    if (input.snapshot.entity_id <= 0)
    {
        throw std::invalid_argument("Room Entity GUID must be positive");
    }
    output.Clear();
    output.set_schema_id(input.schema_id);
    output.set_generation(input.generation);
    output.set_revision(input.revision);
    output.set_entity_id(encode_identity(input.snapshot.entity_id));
    output.set_class_name(input.snapshot.class_name);
    for (const auto& [index, value] : input.snapshot.fields)
    {
        encode_field(index, value, *output.add_fields());
    }
}

inline entity_sync::scalar_baseline decode(
    const wire::scalar_baseline& input, const entity_sync::snapshot_limits& limits = {}) {
    entity_sync::scalar_baseline output{input.schema_id(), input.generation(), input.revision(),
        {decode_guid(input.entity_id()), input.class_name(), {}}};
    if (static_cast<std::size_t>(input.fields_size()) > limits.max_fields)
    {
        throw std::length_error("Scalar field count exceeds limit");
    }
    for (const auto& field : input.fields())
    {
        if (!output.snapshot.fields.emplace(field.index(), decode_field(field)).second)
        {
            throw std::invalid_argument("Duplicate scalar field index");
        }
    }
    entity_sync::scalar_publisher validated(output, limits);
    return output;
}

inline void encode(const entity_sync::scalar_delta& input, wire::scalar_delta& output) {
    entity_sync::detail::validate_scalar_schema(input.schema_id, {});
    entity_sync::detail::validate_scalar_state(input.entity_id, input.class_name, input.fields, {});
    if (input.entity_id <= 0 || input.generation == 0 || input.required_revision == 0 ||
        input.required_revision == std::numeric_limits<std::uint64_t>::max() ||
        input.revision != input.required_revision + 1 || (input.removed && !input.fields.empty()) ||
        (!input.removed && input.fields.empty()))
    {
        throw std::invalid_argument("Invalid scalar delta envelope");
    }
    output.Clear();
    output.set_schema_id(input.schema_id);
    output.set_generation(input.generation);
    output.set_required_revision(input.required_revision);
    output.set_revision(input.revision);
    output.set_entity_id(encode_identity(input.entity_id));
    output.set_class_name(input.class_name);
    output.set_removed(input.removed);
    for (const auto& [index, value] : input.fields)
    {
        encode_field(index, value, *output.add_fields());
    }
}

inline entity_sync::scalar_delta decode(
    const wire::scalar_delta& input, const entity_sync::snapshot_limits& limits = {}) {
    entity_sync::scalar_delta output{input.schema_id(), input.generation(), input.required_revision(), input.revision(),
        decode_guid(input.entity_id()), input.class_name(), {}, input.removed()};
    if (static_cast<std::size_t>(input.fields_size()) > limits.max_fields)
    {
        throw std::length_error("Scalar field count exceeds limit");
    }
    for (const auto& field : input.fields())
    {
        if (!output.fields.emplace(field.index(), decode_field(field)).second)
        {
            throw std::invalid_argument("Duplicate scalar field index");
        }
    }
    wire::scalar_delta validated;
    encode(output, validated);
    entity_sync::detail::validate_scalar_state(output.entity_id, output.class_name, output.fields, limits);
    return output;
}

inline void validate_player(const entity_sync::scalar_baseline& input) {
    const auto& state = input.snapshot;
    if (input.schema_id != player_schema || state.class_name != "GuestPlayer" ||
        state.fields.size() != player_fields.size())
    {
        throw std::invalid_argument("Unsupported GuestPlayer projection");
    }
    for (const auto index : player_fields)
    {
        const auto field = state.fields.find(index);
        if (field == state.fields.end() || (index == 100      ? !std::holds_alternative<std::string>(field->second)
                                               : index <= 102 ? !std::holds_alternative<double>(field->second)
                                                              : !std::holds_alternative<std::uint32_t>(field->second)))
        {
            throw std::invalid_argument("Wrong GuestPlayer scalar field or type");
        }
    }
    const auto x = std::get<double>(state.fields.at(101));
    const auto y = std::get<double>(state.fields.at(102));
    if (!valid_name(std::get<std::string>(state.fields.at(100))) || !std::isfinite(x) || !std::isfinite(y) || x < 0 ||
        x > 960 || y < 0 || y > 540 || std::get<std::uint32_t>(state.fields.at(106)) == 0 ||
        std::get<std::uint32_t>(state.fields.at(107)) == 0 ||
        std::get<std::uint32_t>(state.fields.at(104)) > std::get<std::uint32_t>(state.fields.at(106)) ||
        std::get<std::uint32_t>(state.fields.at(105)) > std::get<std::uint32_t>(state.fields.at(107)))
    {
        throw std::invalid_argument("Invalid GuestPlayer public state");
    }
}

} // namespace ark::guest_room
