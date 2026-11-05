// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

#include "ark/kernel/nested_wire.hpp"
#include "ark/proto/guest_room.pb.h"
#include "guest_room_protocol.hpp"

namespace ark::guest_room {

namespace wire = protobuf::guest_room;
namespace state_wire = protobuf::entity_sync;
inline constexpr auto player_schema = "ark.guest-player";
inline constexpr std::array<std::uint32_t, 9> player_fields{100, 101, 102, 103, 104, 105, 106, 107, 108};
inline constexpr std::uint32_t inventory_index = 200;
inline constexpr std::uint32_t equipment_index = 300;

inline entity_sync::nested_wire::limits player_limits() {
    return {{{1, 3, 8, 64, 512}, 128, 64, 512, 64}, max_body, 16};
}

inline guid_t decode_guid(std::uint64_t value) {
    if (value == 0 || value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
        value > static_cast<std::uint64_t>(std::numeric_limits<guid_t>::max()))
    {
        throw std::invalid_argument("Room Entity GUID must be positive and fit the native width");
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

inline entity_sync::nested_entity_layout player_layout(const std::array<guid_t, 2>& equipment) {
    using type = entity_sync::nested_scalar_type;
    entity_sync::nested_entity_layout layout{"GuestPlayer", {}, {}, {}};
    for (const auto index : player_fields)
    {
        layout.fields.emplace(index, index == 100 ? type::bytes : index <= 102 ? type::real64 : type::uint32);
    }
    layout.tables.emplace(
        inventory_index, entity_sync::nested_table_layout{"InventoryItem", {{1, type::uint32}, {2, type::uint32}}});
    auto& children = layout.containers[equipment_index];
    for (const auto id : equipment)
    {
        encode_identity(id);
        if (!children
                 .emplace(id, entity_sync::nested_entity_layout{"GuestEquipment",
                                  {{1, type::uint32}, {2, type::uint32}}, {}, {}})
                 .second)
        {
            throw std::invalid_argument("Duplicate equipment binding");
        }
    }
    return layout;
}

inline entity_sync::nested_projection player_projection(guid_t root, const std::array<guid_t, 2>& equipment) {
    entity_sync::nested_projection projection{player_schema, root,
        {"GuestPlayer", {player_fields.begin(), player_fields.end()}, {{inventory_index, {1, 2}}}, {}}};
    for (const auto id : equipment)
    {
        projection.root.containers[equipment_index].emplace(
            id, entity_sync::nested_entity_projection{"GuestEquipment", {1, 2}, {}, {}});
    }
    return projection;
}

inline entity_sync::nested_stream_binding decode_binding(const wire::player_binding& input) {
    if (input.equipment_ids_size() != 2 || input.incarnation() == 0 || input.generation() == 0)
    {
        throw std::invalid_argument("Missing fixed player instance binding");
    }
    const auto root = decode_guid(input.entity_id());
    std::array<guid_t, 2> equipment{};
    for (std::size_t i = 0; i < equipment.size(); ++i)
    {
        if (input.equipment_ids(static_cast<int>(i)) <= 0)
        {
            throw std::invalid_argument("Invalid equipment binding");
        }
        equipment[i] = decode_guid(static_cast<std::uint64_t>(input.equipment_ids(static_cast<int>(i))));
        if (equipment[i] == root)
        {
            throw std::invalid_argument("Equipment aliases its root");
        }
    }
    return {player_schema, root, input.incarnation(), input.generation(), player_layout(equipment)};
}

inline void encode_binding(const entity_sync::nested_stream_binding& input, wire::player_binding& output) {
    wire::player_binding staged;
    staged.set_entity_id(encode_identity(input.root_id));
    staged.set_incarnation(input.root_incarnation);
    staged.set_generation(input.generation);
    for (const auto& [id, layout] : input.root.containers.at(equipment_index))
    {
        staged.add_equipment_ids(static_cast<std::int64_t>(encode_identity(id)));
    }
    static_cast<void>(decode_binding(staged));
    output.Swap(&staged);
}

inline void validate_player(const entity_sync::nested_snapshot& input) {
    const auto& root = input.root;
    const auto& fields = root.scalars.fields;
    if (input.schema_id != player_schema || root.scalars.class_name != "GuestPlayer" ||
        fields.size() != player_fields.size() || root.tables.size() != 1 || root.containers.size() != 1)
    {
        throw std::invalid_argument("Unsupported GuestPlayer projection");
    }
    for (const auto index : player_fields)
    {
        const auto field = fields.find(index);
        if (field == fields.end() || (index == 100      ? !std::holds_alternative<std::string>(field->second)
                                         : index <= 102 ? !std::holds_alternative<double>(field->second)
                                                        : !std::holds_alternative<std::uint32_t>(field->second)))
        {
            throw std::invalid_argument("Wrong GuestPlayer field or type");
        }
    }
    const auto x = std::get<double>(fields.at(101));
    const auto y = std::get<double>(fields.at(102));
    if (!valid_name(std::get<std::string>(fields.at(100))) || !std::isfinite(x) || !std::isfinite(y) || x < 0 ||
        x > 960 || y < 0 || y > 540 || std::get<std::uint32_t>(fields.at(106)) == 0 ||
        std::get<std::uint32_t>(fields.at(107)) == 0 ||
        std::get<std::uint32_t>(fields.at(104)) > std::get<std::uint32_t>(fields.at(106)) ||
        std::get<std::uint32_t>(fields.at(105)) > std::get<std::uint32_t>(fields.at(107)))
    {
        throw std::invalid_argument("Invalid GuestPlayer public state");
    }
    if (!root.tables.contains(inventory_index) || !root.containers.contains(equipment_index))
    {
        throw std::invalid_argument("Missing demo inventory or equipment");
    }
    const auto& inventory = root.tables.at(inventory_index);
    if (inventory.class_name != "InventoryItem" || inventory.rows.size() > 1)
    {
        throw std::invalid_argument("Invalid demo inventory");
    }
    for (const auto& [number, row] : inventory.rows)
    {
        if (row.fields.size() != 2 || !row.fields.contains(1) || !row.fields.contains(2) ||
            !std::holds_alternative<std::uint32_t>(row.fields.at(1)) ||
            !std::holds_alternative<std::uint32_t>(row.fields.at(2)))
        {
            throw std::invalid_argument("Wrong inventory column or type");
        }
        if (number != 1 || std::get<std::uint32_t>(row.fields.at(1)) != 1 ||
            std::get<std::uint32_t>(row.fields.at(2)) == 0 || std::get<std::uint32_t>(row.fields.at(2)) > 99)
        {
            throw std::invalid_argument("Invalid potion row");
        }
    }
    const auto& slots = root.containers.at(equipment_index);
    if (slots.size() != 1 || !slots.contains(1))
    {
        throw std::invalid_argument("Invalid demo equipment slot");
    }
    const auto& child = slots.at(1);
    if (child.scalars.fields.size() != 2 || !child.scalars.fields.contains(1) || !child.scalars.fields.contains(2) ||
        !std::holds_alternative<std::uint32_t>(child.scalars.fields.at(1)) ||
        !std::holds_alternative<std::uint32_t>(child.scalars.fields.at(2)))
    {
        throw std::invalid_argument("Wrong equipment field or type");
    }
    const auto item = std::get<std::uint32_t>(child.scalars.fields.at(1));
    if (child.scalars.class_name != "GuestEquipment" || (item != 1001 && item != 1002) ||
        std::get<std::uint32_t>(child.scalars.fields.at(2)) > 100)
    {
        throw std::invalid_argument("Invalid demo equipment attributes");
    }
}

inline void encode(const entity_sync::nested_baseline& input, state_wire::nested_baseline& output,
    const entity_sync::nested_stream_binding& binding) {
    validate_player(input.snapshot);
    entity_sync::nested_wire::encode(input, output, binding, player_limits());
}

inline void encode(const entity_sync::nested_delta& input, state_wire::nested_delta& output,
    const entity_sync::nested_stream_binding& binding) {
    entity_sync::nested_wire::encode(input, output, binding, player_limits());
}

inline entity_sync::nested_baseline decode(
    const state_wire::nested_baseline& input, const entity_sync::nested_stream_binding& binding) {
    auto result = entity_sync::nested_wire::decode(input, binding, player_limits());
    validate_player(result.snapshot);
    return result;
}

inline entity_sync::nested_delta decode(
    const state_wire::nested_delta& input, const entity_sync::nested_stream_binding& binding) {
    return entity_sync::nested_wire::decode(input, binding, player_limits());
}

} // namespace ark::guest_room
