// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <cmath>
#include <stdexcept>

#include "ark/kernel/entity_impl.hpp"
#include "ark/kernel/entity_snapshot.hpp"
#include "ark/kernel/kernel_module_impl.hpp"

namespace ark::entity_sync {

// The public entity interface exposes only materialized node types. This
// private bridge reads selected schema entries without materializing any nodes.
class snapshot_access
{
public:
    static const class_meta* schema(const entity_impl& source) {
        return source.class_meta_.get();
    }

    static const node* find_node(const entity_impl& source, std::uint32_t index) {
        return source.node_manager_ ? source.node_manager_->get_node(index) : nullptr;
    }
};

namespace {

struct selected_field
{
    data_type type;
    const node* value;
};

void add_string_bytes(std::size_t bytes, std::size_t& total, const snapshot_limits& limits) {
    if (bytes > limits.max_string_bytes - total)
    {
        throw std::length_error("scalar snapshot string byte limit exceeded");
    }
    total += bytes;
}

void validate_field(const selected_field& field, std::size_t& string_bytes, const snapshot_limits& limits) {
    if (field.value && field.value->get_type() != field.type)
    {
        throw std::invalid_argument("scalar snapshot node type does not match schema");
    }
    switch (field.type)
    {
    case data_type::dt_boolean:
    case data_type::dt_int32:
    case data_type::dt_uint32:
    case data_type::dt_int64:
    case data_type::dt_uint64:
    case data_type::dt_guid:
        break;
    case data_type::dt_float:
        if (field.value && !std::isfinite(field.value->get_float()))
        {
            throw std::invalid_argument("scalar snapshot requires finite float values");
        }
        break;
    case data_type::dt_double:
        if (field.value && !std::isfinite(field.value->get_double()))
        {
            throw std::invalid_argument("scalar snapshot requires finite double values");
        }
        break;
    case data_type::dt_string:
        if (field.value)
        {
            add_string_bytes(field.value->get_string().size(), string_bytes, limits);
        }
        break;
    default:
        throw std::invalid_argument("scalar snapshot field type is not supported");
    }
}

scalar_value copy_field(const selected_field& field) {
    switch (field.type)
    {
    case data_type::dt_boolean:
        return field.value ? field.value->get_bool() : false;
    case data_type::dt_int32:
        return field.value ? field.value->get_int32() : std::int32_t{};
    case data_type::dt_uint32:
        return field.value ? field.value->get_u_int32() : std::uint32_t{};
    case data_type::dt_int64:
        return field.value ? field.value->get_int64() : std::int64_t{};
    case data_type::dt_uint64:
        return field.value ? field.value->get_u_int64() : std::uint64_t{};
    case data_type::dt_float:
        return field.value ? field.value->get_float() : float{};
    case data_type::dt_double:
        return field.value ? field.value->get_double() : double{};
    case data_type::dt_string:
        return field.value ? field.value->get_string() : std::string{};
    case data_type::dt_guid:
        return guid_value{field.value ? field.value->get_object() : guid_t{}};
    default:
        throw std::invalid_argument("scalar snapshot field type is not supported");
    }
}

} // namespace

static scalar_snapshot capture_selected_scalars(
    const entity& source, std::span<const std::uint32_t> allowed_indices, const snapshot_limits& limits) {
    if (allowed_indices.size() > limits.max_fields)
    {
        throw std::length_error("scalar snapshot field limit exceeded");
    }
    const auto* implementation = dynamic_cast<const entity_impl*>(&source);
    if (!implementation)
    {
        throw std::invalid_argument("scalar snapshots require the framework entity_impl");
    }
    const auto* schema = snapshot_access::schema(*implementation);
    if (!schema)
    {
        throw std::invalid_argument("scalar snapshot entity has no schema");
    }

    std::size_t string_bytes = 0;
    add_string_bytes(schema->get_name().size(), string_bytes, limits);
    std::map<std::uint32_t, selected_field> selected;
    for (const auto index : allowed_indices)
    {
        if (index == 0)
        {
            throw std::invalid_argument("scalar snapshot field indices must be nonzero");
        }
        const auto meta = schema->find_data_meta(index);
        if (!meta)
        {
            throw std::invalid_argument("scalar snapshot field index is not declared");
        }
        const auto [position, inserted] = selected.emplace(
            index, selected_field{meta->get_type(), snapshot_access::find_node(*implementation, index)});
        if (!inserted)
        {
            throw std::invalid_argument("scalar snapshot field indices must be unique");
        }
        validate_field(position->second, string_bytes, limits);
    }

    // Validate every selection and cumulative text limit before copying strings.
    scalar_snapshot result{source.get_id(), schema->get_name(), {}};
    for (const auto& [index, field] : selected)
    {
        result.fields.emplace(index, copy_field(field));
    }
    return result;
}

} // namespace ark::entity_sync

namespace ark {

entity_sync::scalar_snapshot kernel_module_impl::capture_scalar_snapshot(
    const entity& source, std::span<const std::uint32_t> allowed_indices, const entity_sync::snapshot_limits& limits) {
    return entity_sync::capture_selected_scalars(source, allowed_indices, limits);
}

} // namespace ark
