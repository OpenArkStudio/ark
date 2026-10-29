// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <cmath>
#include <set>
#include <stdexcept>

#include "ark/kernel/entity_impl.hpp"
#include "ark/kernel/entity_nested_snapshot.hpp"
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

    static std::uint64_t incarnation(const entity_impl& source) {
        return source.incarnation_;
    }

    static const table* find_table(const entity_impl& source, std::uint32_t index) {
        return source.table_manager_ ? source.table_manager_->get_table_list().find_value(index) : nullptr;
    }

    static const container* find_container(const entity_impl& source, std::uint32_t index) {
        if (!source.opt_charactor_ || !source.opt_charactor_->containers)
        {
            return nullptr;
        }
        return source.opt_charactor_->containers->get_container_list().find_value(index).get();
    }

    static const auto& rows(const table_impl& source) {
        return source.data_;
    }

    static const auto& children(const container_impl& source) {
        return source.entity_data_list_;
    }

    static std::uint64_t incarnation(const row_impl& source) {
        return source.incarnation_;
    }

    static const node* find_node(const row_impl& source, std::uint32_t index) {
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

namespace {

void consume(std::size_t count, std::size_t& remaining) {
    if (count > remaining)
    {
        throw std::length_error("Nested snapshot limit exceeded");
    }
    remaining -= count;
}

struct nested_budget
{
    explicit nested_budget(const nested_snapshot_limits& input)
        : limits(input)
        , entities(input.max_entities)
        , inspected_children(input.max_entities)
        , rows(input.max_rows)
        , fields(input.max_fields)
        , strings(input.max_string_bytes) {
    }

    const nested_snapshot_limits& limits;
    std::size_t entities, inspected_children, rows, fields, strings;
    std::set<guid_t> identities;
};

void validate_indices(const std::vector<std::uint32_t>& indices, nested_budget& budget) {
    consume(indices.size(), budget.fields);
    std::set<std::uint32_t> unique;
    for (const auto index : indices)
    {
        if (index == 0 || !unique.insert(index).second)
        {
            throw std::invalid_argument("Nested projection indices must be nonzero and unique");
        }
    }
}

// Bound authorization input as well as output, including currently absent children.
void validate_projection(const nested_entity_projection& projection, std::size_t depth, nested_budget& budget) {
    if (depth > budget.limits.max_depth)
    {
        throw std::length_error("Nested projection depth limit exceeded");
    }
    if (projection.class_name.empty())
    {
        throw std::invalid_argument("Nested projection class binding is missing");
    }
    consume(1, budget.entities);
    consume(projection.class_name.size(), budget.strings);
    validate_indices(projection.fields, budget);
    consume(projection.tables.size(), budget.fields);
    for (const auto& [index, columns] : projection.tables)
    {
        if (index == 0)
        {
            throw std::invalid_argument("Nested table index must be nonzero");
        }
        validate_indices(columns, budget);
    }
    consume(projection.containers.size(), budget.fields);
    for (const auto& [index, children] : projection.containers)
    {
        if (index == 0)
        {
            throw std::invalid_argument("Nested container index must be nonzero");
        }
        for (const auto& [id, child] : children)
        {
            if (id == null_guid)
            {
                throw std::invalid_argument("Nested child authorization requires a GUID binding");
            }
            validate_projection(child, depth + 1, budget);
        }
    }
}

std::map<std::uint32_t, data_type> selected_columns(
    const class_meta& schema, const std::vector<std::uint32_t>& columns) {
    std::map<std::uint32_t, data_type> selected;
    for (const auto index : columns)
    {
        const auto meta = schema.find_data_meta(index);
        if (!meta)
        {
            throw std::invalid_argument("Nested column index is not declared in the row class");
        }
        std::size_t bytes = 0;
        validate_field({meta->get_type(), nullptr}, bytes, {});
        selected.emplace(index, meta->get_type());
    }
    return selected;
}

nested_row_snapshot capture_row(
    const row& source, const std::map<std::uint32_t, data_type>& columns, nested_budget& budget) {
    const auto* implementation = dynamic_cast<const row_impl*>(&source);
    if (!implementation || snapshot_access::incarnation(*implementation) == 0)
    {
        throw std::invalid_argument("Nested snapshots require a published framework row");
    }
    consume(columns.size(), budget.fields);
    std::map<std::uint32_t, selected_field> selected;
    std::size_t bytes = 0;
    for (const auto& [index, type] : columns)
    {
        selected_field field{type, snapshot_access::find_node(*implementation, index)};
        validate_field(field, bytes, {columns.size(), budget.strings});
        selected.emplace(index, field);
    }
    consume(bytes, budget.strings);
    nested_row_snapshot result{snapshot_access::incarnation(*implementation), {}};
    for (const auto& [index, field] : selected)
    {
        result.fields.emplace(index, copy_field(field));
    }
    return result;
}

nested_entity_snapshot capture_nested_entity(
    const entity& source, const nested_entity_projection& projection, std::size_t depth, nested_budget& budget) {
    if (depth > budget.limits.max_depth)
    {
        throw std::length_error("Nested snapshot depth limit exceeded");
    }
    consume(1, budget.entities);
    const auto* implementation = dynamic_cast<const entity_impl*>(&source);
    const auto* schema = implementation ? snapshot_access::schema(*implementation) : nullptr;
    if (!schema || source.get_id() == null_guid || schema->get_name() != projection.class_name ||
        !budget.identities.insert(source.get_id()).second)
    {
        throw std::invalid_argument("Nested entity binding is invalid, repeated or cyclic");
    }
    consume(projection.fields.size(), budget.fields);
    consume(projection.tables.size(), budget.fields);
    consume(projection.containers.size(), budget.fields);
    auto scalars = capture_selected_scalars(source, projection.fields, {projection.fields.size(), budget.strings});
    consume(scalars.class_name.size(), budget.strings);
    for (const auto& [index, value] : scalars.fields)
    {
        if (const auto text = std::get_if<std::string>(&value))
        {
            consume(text->size(), budget.strings);
        }
    }
    nested_entity_snapshot result{snapshot_access::incarnation(*implementation), std::move(scalars), {}, {}};
    for (const auto& [index, columns] : projection.tables)
    {
        const auto meta = schema->find_table_meta(index);
        const auto row_schema = meta ? meta->get_class_meta() : nullptr;
        if (!row_schema || row_schema->get_name().empty())
        {
            throw std::invalid_argument("Nested table index or row class is not declared");
        }
        const auto selected = selected_columns(*row_schema, columns);
        consume(row_schema->get_name().size(), budget.strings);
        nested_table_snapshot table_state{row_schema->get_name(), {}};
        if (const auto* table = snapshot_access::find_table(*implementation, index))
        {
            const auto* actual = dynamic_cast<const table_impl*>(table);
            if (!actual)
            {
                throw std::invalid_argument("Nested snapshots require a framework table");
            }
            consume(snapshot_access::rows(*actual).size(), budget.rows);
            for (const auto& [number, row] : snapshot_access::rows(*actual))
            {
                if (!row || number == 0 || row->get_row() != number)
                {
                    throw std::invalid_argument("Nested row binding is invalid");
                }
                table_state.rows.emplace(number, capture_row(*row, selected, budget));
            }
        }
        result.tables.emplace(index, std::move(table_state));
    }
    for (const auto& [index, allowed_children] : projection.containers)
    {
        const auto meta = schema->find_container_meta(index);
        if (!meta || meta->get_class_name().empty())
        {
            throw std::invalid_argument("Nested container index or child class is not declared");
        }
        for (const auto& [id, child_projection] : allowed_children)
        {
            if (child_projection.class_name != meta->get_class_name())
            {
                throw std::invalid_argument("Nested child class does not match its container schema");
            }
        }
        auto& children = result.containers[index];
        if (allowed_children.empty())
        {
            continue;
        }
        const auto* container = snapshot_access::find_container(*implementation, index);
        if (!container)
        {
            continue;
        }
        const auto* actual = dynamic_cast<const container_impl*>(container);
        if (!actual)
        {
            throw std::invalid_argument("Nested snapshots require a framework container");
        }
        consume(snapshot_access::children(*actual).size(), budget.inspected_children);
        for (const auto& [slot, child] : snapshot_access::children(*actual))
        {
            if (!child)
            {
                throw std::invalid_argument("Nested container has a null child");
            }
            const auto allowed = allowed_children.find(child->get_id());
            if (allowed == allowed_children.end())
            {
                continue;
            }
            if (slot == 0 || child->get_parent_container().get() != container)
            {
                throw std::invalid_argument("Nested child relationship is invalid");
            }
            children.emplace(slot, capture_nested_entity(*child, allowed->second, depth + 1, budget));
        }
    }
    return result;
}

nested_snapshot capture_selected_nested(
    const entity& source, const nested_projection& projection, const nested_snapshot_limits& limits) {
    if (projection.schema_id.empty() || projection.root_id == null_guid || projection.root_id != source.get_id())
    {
        throw std::invalid_argument("Nested capture requires an explicit schema and authorized root binding");
    }
    nested_budget authorization(limits);
    consume(projection.schema_id.size(), authorization.strings);
    validate_projection(projection.root, 0, authorization);
    nested_budget state(limits);
    consume(projection.schema_id.size(), state.strings);
    return {projection.schema_id, capture_nested_entity(source, projection.root, 0, state)};
}

} // namespace

} // namespace ark::entity_sync

namespace ark {

entity_sync::scalar_snapshot kernel_module_impl::capture_scalar_snapshot(
    const entity& source, std::span<const std::uint32_t> allowed_indices, const entity_sync::snapshot_limits& limits) {
    return entity_sync::capture_selected_scalars(source, allowed_indices, limits);
}

entity_sync::nested_snapshot kernel_module_impl::capture_nested_snapshot(const entity& source,
    const entity_sync::nested_projection& projection, const entity_sync::nested_snapshot_limits& limits) {
    return entity_sync::capture_selected_nested(source, projection, limits);
}

} // namespace ark
