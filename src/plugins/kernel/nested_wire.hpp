// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

// A caller may already have Win32's object-like GetMessage macro in scope.
// Keep Protobuf declarations independent of that macro's include order.
#pragma push_macro("GetMessage")
#undef GetMessage
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/message.h>
#include <google/protobuf/unknown_field_set.h>
#pragma pop_macro("GetMessage")

#include "ark/kernel/nested_stream.hpp"

// nested_stream can introduce Win32 macros through ark/platform.hpp after the
// Protobuf declarations above. Protect generated headers and our implementation
// separately, then restore the caller's macro state at the end of this header.
#pragma push_macro("GetMessage")
#undef GetMessage
#include "ark/proto/entity_sync.pb.h"

namespace ark::entity_sync::nested_wire {

namespace proto = protobuf::entity_sync;

struct limits
{
    nested_stream_limits stream;
    // Independent of native state/patch budgets and transport pending budgets.
    std::size_t max_message_bytes{4 * 1024 * 1024};
    // Protobuf message nesting, including record wrappers, not Entity depth.
    int max_recursion_depth{128};
};

namespace detail {

template<typename guid_type>
inline std::int64_t encode_guid(guid_type input) {
    if constexpr (std::numeric_limits<guid_type>::digits > std::numeric_limits<std::int64_t>::digits)
    {
        if (input < std::numeric_limits<std::int64_t>::min() || input > std::numeric_limits<std::int64_t>::max())
        {
            throw std::invalid_argument("Nested GUID exceeds signed 64-bit wire width");
        }
    }
    return static_cast<std::int64_t>(input);
}

inline guid_t decode_guid(std::int64_t input) {
    if (input < std::numeric_limits<guid_t>::min() || input > std::numeric_limits<guid_t>::max())
    {
        throw std::invalid_argument("Nested GUID exceeds native width");
    }
    return static_cast<guid_t>(input);
}

inline void check_limits(const limits& budget) {
    if (!budget.max_message_bytes ||
        budget.max_message_bytes > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        budget.max_recursion_depth <= 0)
    {
        throw std::invalid_argument("Invalid nested wire byte or recursion limit");
    }
}

inline void check_unknown(
    const google::protobuf::UnknownFieldSet& input, int depth, const limits& budget, std::size_t& records) {
    if (depth > budget.max_recursion_depth)
    {
        throw std::length_error("Nested wire unknown-group recursion limit exceeded");
    }
    for (int index = 0; index < input.field_count(); ++index)
    {
        if (!records)
        {
            throw std::length_error("Nested wire record limit exceeded");
        }
        --records;
        const auto& field = input.field(index);
        if (field.type() == google::protobuf::UnknownField::TYPE_GROUP)
        {
            check_unknown(field.group(), depth + 1, budget, records);
        }
    }
}

// Check a caller-supplied generated message before recursively computing its
// size or constructing native maps. Unknown fields remain forward compatible;
// their size and any unknown groups still obey the independent wire budgets.
inline void check_depth(const google::protobuf::Message& input, int depth, const limits& budget, std::size_t& records) {
    if (depth > budget.max_recursion_depth || !records)
    {
        throw std::length_error("Nested wire recursion or record limit exceeded");
    }
    --records;
    const auto* reflection = input.GetReflection();
    check_unknown(reflection->GetUnknownFields(input), depth, budget, records);
    std::vector<const google::protobuf::FieldDescriptor*> fields;
    reflection->ListFields(input, &fields);
    for (const auto* field : fields)
    {
        if (field->cpp_type() != google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE)
        {
            continue;
        }
        if (field->is_repeated())
        {
            const auto count = reflection->FieldSize(input, field);
            for (int index = 0; index < count; ++index)
            {
                check_depth(reflection->GetRepeatedMessage(input, field, index), depth + 1, budget, records);
            }
        }
        else
        {
            check_depth(reflection->GetMessage(input, field), depth + 1, budget, records);
        }
    }
}

inline void check_message(const google::protobuf::Message& input, const limits& budget) {
    check_limits(budget);
    // Each nested record needs at least a tag and a length byte on the wire.
    auto records = budget.max_message_bytes;
    check_depth(input, 0, budget, records);
    if (input.ByteSizeLong() > budget.max_message_bytes)
    {
        throw std::length_error("Nested wire message byte limit exceeded");
    }
}

inline void validate(const nested_baseline& input, const nested_stream_binding& trusted, const limits& budget) {
    nested_detail::binding(trusted, budget.stream);
    nested_detail::require(input.generation == trusted.generation && input.revision);
    nested_detail::snapshot(input.snapshot, trusted, budget.stream);
    nested_detail::lifetime_history history;
    nested_detail::remember(input.snapshot.root, history, budget.stream.max_lifetime_entries);
}

inline void validate(const nested_delta& input, const nested_stream_binding& trusted, const limits& budget) {
    nested_detail::binding(trusted, budget.stream);
    nested_detail::delta(input, trusted, budget.stream);
}

inline void encode_fields(const std::map<std::uint32_t, scalar_value>& input,
    google::protobuf::RepeatedPtrField<proto::scalar_field>& output) {
    for (const auto& [index, value] : input)
    {
        auto& field = *output.Add();
        field.set_index(index);
        std::visit(
            [&](const auto& item) {
                using type = std::decay_t<decltype(item)>;
                if constexpr (std::is_same_v<type, bool>)
                {
                    field.set_bool_value(item);
                }
                else if constexpr (std::is_same_v<type, std::int32_t>)
                {
                    field.set_int32_value(item);
                }
                else if constexpr (std::is_same_v<type, std::uint32_t>)
                {
                    field.set_uint32_value(item);
                }
                else if constexpr (std::is_same_v<type, std::int64_t>)
                {
                    field.set_int64_value(item);
                }
                else if constexpr (std::is_same_v<type, std::uint64_t>)
                {
                    field.set_uint64_value(item);
                }
                else if constexpr (std::is_same_v<type, float>)
                {
                    field.set_float_value(item);
                }
                else if constexpr (std::is_same_v<type, double>)
                {
                    field.set_double_value(item);
                }
                else if constexpr (std::is_same_v<type, std::string>)
                {
                    field.set_string_value(item);
                }
                else if constexpr (std::is_same_v<type, guid_value>)
                {
                    field.set_guid_value(encode_guid(item.value));
                }
            },
            value);
    }
}

inline scalar_value decode_field(const proto::scalar_field& input) {
    switch (input.value_case())
    {
    case proto::scalar_field::kBoolValue:
        return input.bool_value();
    case proto::scalar_field::kInt32Value:
        return input.int32_value();
    case proto::scalar_field::kUint32Value:
        return input.uint32_value();
    case proto::scalar_field::kInt64Value:
        return input.int64_value();
    case proto::scalar_field::kUint64Value:
        return input.uint64_value();
    case proto::scalar_field::kFloatValue:
        return input.float_value();
    case proto::scalar_field::kDoubleValue:
        return input.double_value();
    case proto::scalar_field::kStringValue:
        return input.string_value();
    case proto::scalar_field::kGuidValue:
        return guid_value{decode_guid(input.guid_value())};
    default:
        throw std::invalid_argument("Missing nested scalar alternative");
    }
}

inline std::map<std::uint32_t, scalar_value> decode_fields(
    const google::protobuf::RepeatedPtrField<proto::scalar_field>& input) {
    std::map<std::uint32_t, scalar_value> output;
    for (const auto& field : input)
    {
        nested_detail::require(
            field.index() &&
            output.emplace(field.index(), entity_sync::detail::direct_value{[&] { return decode_field(field); }})
                .second);
    }
    return output;
}

inline void encode_entity(const nested_entity_snapshot& input, proto::entity_snapshot& output) {
    output.set_entity_id(encode_guid(input.scalars.entity_id));
    output.set_incarnation(input.incarnation);
    output.set_class_name(input.scalars.class_name);
    encode_fields(input.scalars.fields, *output.mutable_fields());
    for (const auto& [index, table] : input.tables)
    {
        auto& result = *output.add_tables();
        result.set_index(index);
        result.set_class_name(table.class_name);
        for (const auto& [number, row] : table.rows)
        {
            auto& record = *result.add_rows();
            record.set_number(number);
            record.set_incarnation(row.incarnation);
            encode_fields(row.fields, *record.mutable_fields());
        }
    }
    for (const auto& [index, children] : input.containers)
    {
        auto& result = *output.add_containers();
        result.set_index(index);
        for (const auto& [slot, child] : children)
        {
            auto& record = *result.add_slots();
            record.set_slot(slot);
            encode_entity(child, *record.mutable_state());
        }
    }
}

inline nested_entity_snapshot decode_entity(const proto::entity_snapshot& input) {
    std::map<std::uint32_t, nested_table_snapshot> tables;
    for (const auto& table : input.tables())
    {
        std::map<std::uint32_t, nested_row_snapshot> rows;
        for (const auto& row : table.rows())
        {
            nested_detail::require(row.number() && rows.emplace(row.number(), entity_sync::detail::direct_value{[&] {
                                                           return nested_row_snapshot{
                                                               row.incarnation(), decode_fields(row.fields())};
                                                       }})
                                                       .second);
        }
        nested_detail::require(table.index() && tables
                                                    .emplace(table.index(), entity_sync::detail::direct_value{[&] {
                                                        return nested_table_snapshot{
                                                            table.class_name(), std::move(rows)};
                                                    }})
                                                    .second);
    }
    std::map<std::uint32_t, std::map<std::uint32_t, nested_entity_snapshot>> containers;
    for (const auto& container : input.containers())
    {
        std::map<std::uint32_t, nested_entity_snapshot> children;
        for (const auto& slot : container.slots())
        {
            nested_detail::require(slot.slot() && slot.has_state() &&
                                   children
                                       .emplace(slot.slot(), entity_sync::detail::direct_value{[&] {
                                           return decode_entity(slot.state());
                                       }})
                                       .second);
        }
        nested_detail::require(container.index() && containers.emplace(container.index(), std::move(children)).second);
    }
    return {input.incarnation(), {decode_guid(input.entity_id()), input.class_name(), decode_fields(input.fields())},
        std::move(tables), std::move(containers)};
}

inline void encode_change(const nested_entity_delta& input, proto::entity_delta& output) {
    output.set_entity_id(encode_guid(input.entity_id));
    output.set_incarnation(input.incarnation);
    encode_fields(input.fields, *output.mutable_fields());
    for (const auto& [index, rows] : input.tables)
    {
        auto& result = *output.add_tables();
        result.set_index(index);
        for (const auto& [number, row] : rows)
        {
            auto& record = *result.add_rows();
            record.set_number(number);
            record.set_required_incarnation(row.required_incarnation);
            record.set_incarnation(row.incarnation);
            encode_fields(row.fields, *record.mutable_fields());
        }
    }
    for (const auto& [index, change] : input.containers)
    {
        auto& result = *output.add_containers();
        result.set_index(index);
        if (change.replace)
        {
            auto& replacement = *result.mutable_replacement();
            for (const auto& [slot, child] : change.slots)
            {
                auto& record = *replacement.add_slots();
                record.set_slot(slot);
                encode_entity(child, *record.mutable_state());
            }
        }
        else
        {
            auto& patch = *result.mutable_patch();
            for (const auto& [slot, child] : change.changed)
            {
                auto& record = *patch.add_changed();
                record.set_slot(slot);
                encode_change(child, *record.mutable_change());
            }
        }
    }
}

inline nested_entity_delta decode_change(const proto::entity_delta& input) {
    std::map<std::uint32_t, std::map<std::uint32_t, nested_row_delta>> tables;
    for (const auto& table : input.tables())
    {
        std::map<std::uint32_t, nested_row_delta> rows;
        for (const auto& row : table.rows())
        {
            nested_detail::require(row.number() && rows.emplace(row.number(), entity_sync::detail::direct_value{[&] {
                                                           return nested_row_delta{row.required_incarnation(),
                                                               row.incarnation(), decode_fields(row.fields())};
                                                       }})
                                                       .second);
        }
        nested_detail::require(table.index() && tables.emplace(table.index(), std::move(rows)).second);
    }
    std::map<std::uint32_t, nested_container_delta> containers;
    for (const auto& container : input.containers())
    {
        nested_container_delta change;
        if (container.has_replacement())
        {
            change.replace = true;
            for (const auto& slot : container.replacement().slots())
            {
                nested_detail::require(slot.slot() && slot.has_state() &&
                                       change.slots
                                           .emplace(slot.slot(), entity_sync::detail::direct_value{[&] {
                                               return decode_entity(slot.state());
                                           }})
                                           .second);
            }
        }
        else
        {
            nested_detail::require(container.has_patch());
            for (const auto& slot : container.patch().changed())
            {
                nested_detail::require(slot.slot() && slot.has_change() &&
                                       change.changed
                                           .emplace(slot.slot(), entity_sync::detail::direct_value{[&] {
                                               return decode_change(slot.change());
                                           }})
                                           .second);
            }
        }
        nested_detail::require(container.index() && containers.emplace(container.index(), std::move(change)).second);
    }
    return {decode_guid(input.entity_id()), input.incarnation(), decode_fields(input.fields()), std::move(tables),
        std::move(containers)};
}

template<typename message_type>
inline void parse(std::string_view input, message_type& output, const limits& budget) {
    check_limits(budget);
    if (input.size() > budget.max_message_bytes)
    {
        throw std::length_error("Nested wire message byte limit exceeded");
    }
    google::protobuf::io::CodedInputStream stream(
        reinterpret_cast<const std::uint8_t*>(input.data()), static_cast<int>(input.size()));
    stream.SetTotalBytesLimit(static_cast<int>(budget.max_message_bytes));
    stream.SetRecursionLimit(budget.max_recursion_depth);
    if (!output.ParseFromCodedStream(&stream) || !stream.ConsumedEntireMessage())
    {
        throw std::invalid_argument("Malformed or excessively nested Entity message");
    }
}

} // namespace detail

// Encoding validates the independent binding and stages the complete result.
// A codec validates packet structure; ordering and lifetime history belong to
// nested_replica::apply(). A successful encode is not a delivery acknowledgement.
inline void encode(const nested_baseline& input, proto::nested_baseline& output, const nested_stream_binding& trusted,
    const limits& budget = {}) {
    detail::check_limits(budget);
    detail::validate(input, trusted, budget);
    // Sharing the destination arena makes the final Swap allocation-free.
    auto* candidate = google::protobuf::Arena::Create<proto::nested_baseline>(output.GetArena());
    std::unique_ptr<proto::nested_baseline> owner(output.GetArena() ? nullptr : candidate);
    candidate->set_generation(input.generation);
    candidate->set_revision(input.revision);
    auto& snapshot = *candidate->mutable_snapshot();
    snapshot.set_schema_id(input.snapshot.schema_id);
    detail::encode_entity(input.snapshot.root, *snapshot.mutable_root());
    detail::check_message(*candidate, budget);
    output.Swap(candidate);
}

inline void encode(const nested_delta& input, proto::nested_delta& output, const nested_stream_binding& trusted,
    const limits& budget = {}) {
    detail::check_limits(budget);
    detail::validate(input, trusted, budget);
    auto* candidate = google::protobuf::Arena::Create<proto::nested_delta>(output.GetArena());
    std::unique_ptr<proto::nested_delta> owner(output.GetArena() ? nullptr : candidate);
    candidate->set_schema_id(input.schema_id);
    candidate->set_generation(input.generation);
    candidate->set_required_revision(input.required_revision);
    candidate->set_revision(input.revision);
    candidate->set_removed(input.removed);
    detail::encode_change(input.root, *candidate->mutable_root());
    detail::check_message(*candidate, budget);
    output.Swap(candidate);
}

inline nested_baseline decode(
    const proto::nested_baseline& input, const nested_stream_binding& trusted, const limits& budget = {}) {
    detail::check_message(input, budget);
    nested_detail::binding(trusted, budget.stream);
    nested_detail::require(input.has_snapshot() && input.snapshot().has_root());
    nested_baseline candidate{input.generation(), input.revision(),
        {input.snapshot().schema_id(), detail::decode_entity(input.snapshot().root())}};
    detail::validate(candidate, trusted, budget);
    return {candidate.generation, candidate.revision, nested_detail::take_snapshot(candidate.snapshot)};
}

inline nested_delta decode(
    const proto::nested_delta& input, const nested_stream_binding& trusted, const limits& budget = {}) {
    detail::check_message(input, budget);
    nested_detail::binding(trusted, budget.stream);
    nested_detail::require(input.has_root());
    nested_delta candidate{input.schema_id(), input.generation(), input.required_revision(), input.revision(),
        detail::decode_change(input.root()), input.removed()};
    detail::validate(candidate, trusted, budget);
    return {candidate.schema_id, candidate.generation, candidate.required_revision, candidate.revision,
        std::move(candidate.root), candidate.removed};
}

template<typename publication_type>
inline std::string encode(
    const publication_type& input, const nested_stream_binding& trusted, const limits& budget = {}) {
    using message_type = std::conditional_t<std::is_same_v<publication_type, nested_baseline>, proto::nested_baseline,
        proto::nested_delta>;
    static_assert(std::is_same_v<publication_type, nested_baseline> || std::is_same_v<publication_type, nested_delta>);
    message_type message;
    encode(input, message, trusted, budget);
    std::string output;
    if (!message.SerializeToString(&output))
    {
        throw std::invalid_argument("Cannot serialize nested Entity message");
    }
    return output;
}

inline nested_baseline decode_baseline(
    std::string_view input, const nested_stream_binding& trusted, const limits& budget = {}) {
    proto::nested_baseline message;
    detail::parse(input, message, budget);
    return decode(message, trusted, budget);
}

inline nested_delta decode_delta(
    std::string_view input, const nested_stream_binding& trusted, const limits& budget = {}) {
    proto::nested_delta message;
    detail::parse(input, message, budget);
    return decode(message, trusted, budget);
}

} // namespace ark::entity_sync::nested_wire

#pragma pop_macro("GetMessage")
