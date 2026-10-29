// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#include "ark/kernel/entity_nested_snapshot.hpp"

namespace ark::entity_sync {

// Local type names only; neither these ordinals nor variant indices are a wire ABI.
enum class nested_scalar_type
{
    boolean,
    int32,
    uint32,
    int64,
    uint64,
    real32,
    real64,
    bytes,
    guid
};
static_assert(std::variant_size_v<scalar_value> == 9);
using nested_field_layout = std::map<std::uint32_t, nested_scalar_type>;

struct nested_table_layout
{
    std::string class_name;
    nested_field_layout columns;
};

// Trusted application schema/projection, supplied before any remote baseline.
struct nested_entity_layout
{
    std::string class_name;
    nested_field_layout fields;
    std::map<std::uint32_t, nested_table_layout> tables;
    std::map<std::uint32_t, std::map<guid_t, nested_entity_layout>> containers;
};

struct nested_stream_binding
{
    std::string schema_id;
    guid_t root_id{};
    std::uint64_t root_incarnation{};
    std::uint64_t generation{};
    nested_entity_layout root;
};

struct nested_baseline
{
    std::uint64_t generation{};
    std::uint64_t revision{};
    nested_snapshot snapshot;
};

// required=0: complete add; incarnation=0: remove; equal incarnations:
// sparse column patch; newer incarnation: complete replacement of that row.
struct nested_row_delta
{
    std::uint64_t required_incarnation{};
    std::uint64_t incarnation{};
    std::map<std::uint32_t, scalar_value> fields;
};

struct nested_container_delta;

struct nested_entity_delta
{
    guid_t entity_id{};
    std::uint64_t incarnation{};
    std::map<std::uint32_t, scalar_value> fields;
    std::map<std::uint32_t, std::map<std::uint32_t, nested_row_delta>> tables;
    std::map<std::uint32_t, nested_container_delta> containers;
};

struct nested_container_delta
{
    // Structural changes replace the container's final authorized membership.
    bool replace{false};
    std::map<std::uint32_t, nested_entity_snapshot> slots;
    std::map<std::uint32_t, nested_entity_delta> changed;
};

struct nested_delta
{
    std::string schema_id;
    std::uint64_t generation{};
    std::uint64_t required_revision{};
    std::uint64_t revision{};
    nested_entity_delta root;
    bool removed{false};
};

static_assert(std::is_nothrow_move_constructible_v<std::shared_ptr<const nested_baseline>>);
static_assert(std::is_nothrow_move_constructible_v<std::shared_ptr<const nested_delta>>);

struct nested_stream_limits
{
    nested_snapshot_limits snapshot;
    std::size_t max_patch_operations{65536};
    std::size_t max_patch_values{65536};
    std::size_t max_patch_string_bytes{1024 * 1024};
    // Entity records + table records + last-visible row identities. No packets.
    std::size_t max_lifetime_entries{132096};
};

enum class nested_apply_result
{
    applied,
    duplicate,
    rejected
};

namespace nested_detail {

inline void require(bool condition) {
    if (!condition)
    {
        throw std::invalid_argument("Invalid nested stream binding, state or operation");
    }
}

inline void consume(std::size_t count, std::size_t& remaining) {
    if (count > remaining)
    {
        throw std::length_error("Nested stream limit exceeded");
    }
    remaining -= count;
}

inline void value(const scalar_value& input, nested_scalar_type expected, std::size_t& bytes) {
    require(!input.valueless_by_exception() && input.index() == static_cast<std::size_t>(expected));
    std::visit(
        [&](const auto& item) {
            using type = std::decay_t<decltype(item)>;
            if constexpr (std::is_floating_point_v<type>)
            {
                require(std::isfinite(item));
            }
            else if constexpr (std::is_same_v<type, std::string>)
            {
                consume(item.size(), bytes);
            }
        },
        input);
}

inline void fields(const std::map<std::uint32_t, scalar_value>& input, const nested_field_layout& layout, bool complete,
    std::size_t& count, std::size_t& bytes) {
    require(!complete || input.size() == layout.size());
    consume(input.size(), count);
    for (const auto& [index, item] : input)
    {
        const auto expected = layout.find(index);
        require(expected != layout.end());
        value(item, expected->second, bytes);
    }
}

struct state_budget
{
    explicit state_budget(const nested_snapshot_limits& input)
        : limits(input)
        , entities(input.max_entities)
        , rows(input.max_rows)
        , fields(input.max_fields)
        , strings(input.max_string_bytes) {
    }

    const nested_snapshot_limits& limits;
    std::size_t entities, rows, fields, strings;
    std::set<guid_t> seen;
};

struct schema_member
{
    unsigned kind;
    nested_scalar_type type{};
    std::string_view reference;
};

using schema_registry = std::map<std::pair<std::string_view, std::uint32_t>, schema_member>;

inline void member(schema_registry& registry, std::string_view owner, std::uint32_t index, schema_member next) {
    require(index != 0);
    const auto [position, added] = registry.emplace(std::make_pair(owner, index), next);
    if (!added)
    {
        auto& old = position->second;
        require(old.kind == next.kind && (old.kind != 0 || old.type == next.type));
        require(old.reference.empty() || next.reference.empty() || old.reference == next.reference);
        if (old.reference.empty())
        {
            old.reference = next.reference;
        }
    }
}

inline void field_layout(
    const nested_field_layout& input, std::string_view owner, state_budget& budget, schema_registry& registry) {
    consume(input.size(), budget.fields);
    for (const auto& [index, type] : input)
    {
        require(static_cast<std::size_t>(type) <= static_cast<std::size_t>(nested_scalar_type::guid));
        member(registry, owner, index, {0, type, {}});
    }
}

inline void layout(
    const nested_entity_layout& input, std::size_t depth, state_budget& budget, schema_registry& registry) {
    if (depth > budget.limits.max_depth)
    {
        throw std::length_error("Nested binding depth exceeded");
    }
    require(!input.class_name.empty());
    consume(1, budget.entities);
    consume(input.class_name.size(), budget.strings);
    field_layout(input.fields, input.class_name, budget, registry);
    consume(input.tables.size(), budget.fields);
    for (const auto& [index, table] : input.tables)
    {
        require(!table.class_name.empty());
        member(registry, input.class_name, index, {1, {}, table.class_name});
        consume(table.class_name.size(), budget.strings);
        field_layout(table.columns, table.class_name, budget, registry);
    }
    consume(input.containers.size(), budget.fields);
    for (const auto& [index, children] : input.containers)
    {
        member(registry, input.class_name, index, {2, {}, {}});
        for (const auto& [id, child] : children)
        {
            require(id != guid_t{});
            member(registry, input.class_name, index, {2, {}, child.class_name});
            layout(child, depth + 1, budget, registry);
        }
    }
}

inline void binding(const nested_stream_binding& input, const nested_stream_limits& limits) {
    require(!input.schema_id.empty() && input.root_id != guid_t{} && input.root_incarnation && input.generation);
    state_budget budget(limits.snapshot);
    consume(input.schema_id.size(), budget.strings);
    schema_registry registry;
    layout(input.root, 0, budget, registry);
}

inline void entity(
    const nested_entity_snapshot& input, const nested_entity_layout& trusted, std::size_t depth, state_budget& budget) {
    if (depth > budget.limits.max_depth)
    {
        throw std::length_error("Nested state depth exceeded");
    }
    consume(1, budget.entities);
    require(input.incarnation && input.scalars.entity_id != guid_t{} &&
            input.scalars.class_name == trusted.class_name && budget.seen.insert(input.scalars.entity_id).second);
    consume(input.scalars.class_name.size(), budget.strings);
    fields(input.scalars.fields, trusted.fields, true, budget.fields, budget.strings);
    require(input.tables.size() == trusted.tables.size() && input.containers.size() == trusted.containers.size());
    consume(input.tables.size(), budget.fields);
    for (const auto& [index, table] : input.tables)
    {
        const auto expected = trusted.tables.find(index);
        require(expected != trusted.tables.end() && table.class_name == expected->second.class_name);
        consume(table.class_name.size(), budget.strings);
        consume(table.rows.size(), budget.rows);
        std::set<std::uint64_t> incarnations;
        for (const auto& [number, row] : table.rows)
        {
            require(number && row.incarnation && incarnations.insert(row.incarnation).second);
            fields(row.fields, expected->second.columns, true, budget.fields, budget.strings);
        }
    }
    consume(input.containers.size(), budget.fields);
    for (const auto& [index, children] : input.containers)
    {
        const auto allowed = trusted.containers.find(index);
        require(allowed != trusted.containers.end());
        for (const auto& [slot, child] : children)
        {
            const auto selected = allowed->second.find(child.scalars.entity_id);
            require(slot && selected != allowed->second.end());
            entity(child, selected->second, depth + 1, budget);
        }
    }
}

inline void snapshot(
    const nested_snapshot& input, const nested_stream_binding& trusted, const nested_stream_limits& limits) {
    require(input.schema_id == trusted.schema_id && input.root.scalars.entity_id == trusted.root_id &&
            input.root.incarnation == trusted.root_incarnation);
    state_budget budget(limits.snapshot);
    consume(input.schema_id.size(), budget.strings);
    entity(input.root, trusted.root, 0, budget);
}

struct table_lifetime
{
    std::uint64_t watermark{};
    std::map<std::uint32_t, std::uint64_t> rows;
};

struct entity_lifetime
{
    std::uint64_t incarnation;
    std::string class_name;
};

struct lifetime_history
{
    std::map<guid_t, entity_lifetime> entities;
    std::map<std::tuple<guid_t, std::uint64_t, std::uint32_t>, table_lifetime> tables;
    std::size_t entries{};

    void swap(lifetime_history& other) noexcept {
        entities.swap(other.entities);
        tables.swap(other.tables);
        std::swap(entries, other.entries);
    }
};

inline void history_entry(lifetime_history& history, std::size_t limit) {
    if (history.entries >= limit)
    {
        throw std::length_error("Nested lifetime history exceeded");
    }
    ++history.entries;
}

inline void remember(const nested_entity_snapshot& input, lifetime_history& history, std::size_t limit) {
    const auto id = input.scalars.entity_id;
    auto known = history.entities.find(id);
    if (known == history.entities.end())
    {
        history_entry(history, limit);
        history.entities.emplace(id, entity_lifetime{input.incarnation, input.scalars.class_name});
    }
    else
    {
        require(input.incarnation >= known->second.incarnation);
        if (input.incarnation == known->second.incarnation)
        {
            require(input.scalars.class_name == known->second.class_name);
        }
        else
        {
            for (auto position = history.tables.lower_bound({id, 0, 0});
                 position != history.tables.end() && std::get<0>(position->first) == id;)
            {
                history.entries -= 1 + position->second.rows.size();
                position = history.tables.erase(position);
            }
            known->second = {input.incarnation, input.scalars.class_name};
        }
    }
    for (const auto& [index, table] : input.tables)
    {
        const auto key = std::make_tuple(id, input.incarnation, index);
        auto previous = history.tables.find(key);
        if (previous == history.tables.end())
        {
            history_entry(history, limit);
            previous = history.tables.emplace(key, table_lifetime{}).first;
        }
        auto& old = previous->second;
        const auto without_rows = history.entries - old.rows.size();
        require(without_rows <= limit);
        if (table.rows.size() > limit - without_rows)
        {
            throw std::length_error("Nested row history exceeded");
        }
        auto watermark = old.watermark;
        std::map<std::uint32_t, std::uint64_t> rows;
        for (const auto& [number, row] : table.rows)
        {
            const auto before = old.rows.find(number);
            require((before != old.rows.end() && before->second == row.incarnation) || row.incarnation > old.watermark);
            watermark = std::max(watermark, row.incarnation);
            rows.emplace(number, row.incarnation);
        }
        old.rows.swap(rows);
        old.watermark = watermark;
        history.entries = without_rows + old.rows.size();
    }
    for (const auto& [index, children] : input.containers)
    {
        for (const auto& [slot, child] : children)
        {
            remember(child, history, limit);
        }
    }
}

struct patch_budget
{
    explicit patch_budget(const nested_stream_limits& limits)
        : operations(limits.max_patch_operations)
        , values(limits.max_patch_values)
        , strings(limits.max_patch_string_bytes) {
    }

    std::size_t operations, values, strings;
};

inline void count_values(const std::map<std::uint32_t, scalar_value>& values, patch_budget& budget) {
    consume(values.size(), budget.values);
    for (const auto& [index, item] : values)
    {
        require(!item.valueless_by_exception());
        value(item, static_cast<nested_scalar_type>(item.index()), budget.strings);
    }
}

inline void count_entity(const nested_entity_snapshot& input, patch_budget& budget) {
    consume(1, budget.operations);
    consume(input.scalars.class_name.size(), budget.strings);
    count_values(input.scalars.fields, budget);
    consume(input.tables.size(), budget.operations);
    for (const auto& [index, table] : input.tables)
    {
        consume(table.class_name.size(), budget.strings);
        consume(table.rows.size(), budget.operations);
        for (const auto& [number, row] : table.rows)
        {
            count_values(row.fields, budget);
        }
    }
    consume(input.containers.size(), budget.operations);
    for (const auto& [index, children] : input.containers)
    {
        for (const auto& [slot, child] : children)
        {
            count_entity(child, budget);
        }
    }
}

inline bool empty(const nested_entity_delta& input) {
    return input.fields.empty() && input.tables.empty() && input.containers.empty();
}

inline void patch_fields(const std::map<std::uint32_t, scalar_value>& before,
    const std::map<std::uint32_t, scalar_value>& next, std::map<std::uint32_t, scalar_value>& output,
    patch_budget& budget) {
    for (const auto& [index, item] : next)
    {
        const auto old = before.find(index);
        if (old == before.end() || old->second != item)
        {
            consume(1, budget.values);
            value(item, static_cast<nested_scalar_type>(item.index()), budget.strings);
            output.emplace(index, item);
        }
    }
}

inline bool same_membership(const std::map<std::uint32_t, nested_entity_snapshot>& before,
    const std::map<std::uint32_t, nested_entity_snapshot>& next) {
    if (before.size() != next.size())
    {
        return false;
    }
    auto left = before.begin();
    for (const auto& [slot, child] : next)
    {
        if (left->first != slot || left->second.scalars.entity_id != child.scalars.entity_id ||
            left->second.incarnation != child.incarnation)
        {
            return false;
        }
        ++left;
    }
    return true;
}

inline nested_entity_delta difference(
    const nested_entity_snapshot& before, const nested_entity_snapshot& next, patch_budget& budget) {
    consume(1, budget.operations);
    nested_entity_delta result{next.scalars.entity_id, next.incarnation, {}, {}, {}};
    patch_fields(before.scalars.fields, next.scalars.fields, result.fields, budget);
    for (const auto& [index, table] : next.tables)
    {
        const auto& old = before.tables.at(index);
        std::map<std::uint32_t, nested_row_delta> changes;
        for (const auto& [number, row] : old.rows)
        {
            if (!table.rows.contains(number))
            {
                consume(1, budget.operations);
                changes.emplace(number, nested_row_delta{row.incarnation, 0, {}});
            }
        }
        for (const auto& [number, row] : table.rows)
        {
            const auto previous = old.rows.find(number);
            if (previous == old.rows.end() || previous->second.incarnation != row.incarnation)
            {
                consume(1, budget.operations);
                count_values(row.fields, budget);
                changes.emplace(number, nested_row_delta{previous == old.rows.end() ? 0 : previous->second.incarnation,
                                            row.incarnation, row.fields});
            }
            else
            {
                nested_row_delta update{row.incarnation, row.incarnation, {}};
                patch_fields(previous->second.fields, row.fields, update.fields, budget);
                if (!update.fields.empty())
                {
                    consume(1, budget.operations);
                    changes.emplace(number, std::move(update));
                }
            }
        }
        if (!changes.empty())
        {
            consume(1, budget.operations);
            result.tables.emplace(index, std::move(changes));
        }
    }
    for (const auto& [index, children] : next.containers)
    {
        const auto& old = before.containers.at(index);
        nested_container_delta update;
        if (!same_membership(old, children))
        {
            consume(1, budget.operations);
            for (const auto& [slot, child] : children)
            {
                count_entity(child, budget);
            }
            update.replace = true;
            update.slots = children;
            result.containers.emplace(index, std::move(update));
        }
        else
        {
            for (const auto& [slot, child] : children)
            {
                if (!(child == old.at(slot)))
                {
                    auto delta = difference(old.at(slot), child, budget);
                    update.changed.emplace(slot, std::move(delta));
                }
            }
            if (!update.changed.empty())
            {
                consume(1, budget.operations);
                result.containers.emplace(index, std::move(update));
            }
        }
    }
    return result;
}

inline void validate_patch(const nested_entity_delta& input, const nested_entity_layout& trusted, std::size_t depth,
    patch_budget& budget, state_budget& replacements) {
    if (depth > replacements.limits.max_depth)
    {
        throw std::length_error("Nested patch depth exceeded");
    }
    consume(1, budget.operations);
    require(input.entity_id != guid_t{} && input.incarnation && replacements.seen.insert(input.entity_id).second);
    fields(input.fields, trusted.fields, false, budget.values, budget.strings);
    consume(input.tables.size(), budget.operations);
    for (const auto& [index, rows] : input.tables)
    {
        const auto expected = trusted.tables.find(index);
        require(expected != trusted.tables.end() && !rows.empty());
        consume(rows.size(), budget.operations);
        for (const auto& [number, row] : rows)
        {
            require(number && (row.required_incarnation || row.incarnation));
            if (!row.incarnation)
            {
                require(row.fields.empty());
            }
            else
            {
                const bool complete = row.incarnation != row.required_incarnation;
                require(complete ? row.incarnation > row.required_incarnation : !row.fields.empty());
                fields(row.fields, expected->second.columns, complete, budget.values, budget.strings);
            }
        }
    }
    consume(input.containers.size(), budget.operations);
    for (const auto& [index, update] : input.containers)
    {
        const auto allowed = trusted.containers.find(index);
        require(allowed != trusted.containers.end());
        if (update.replace)
        {
            require(update.changed.empty());
            for (const auto& [slot, child] : update.slots)
            {
                const auto selection = allowed->second.find(child.scalars.entity_id);
                require(slot && selection != allowed->second.end());
                entity(child, selection->second, depth + 1, replacements);
                count_entity(child, budget);
            }
        }
        else
        {
            require(update.slots.empty() && !update.changed.empty());
            for (const auto& [slot, child] : update.changed)
            {
                const auto selection = allowed->second.find(child.entity_id);
                require(slot && selection != allowed->second.end() && !empty(child));
                validate_patch(child, selection->second, depth + 1, budget, replacements);
            }
        }
    }
}

inline void delta(const nested_delta& input, const nested_stream_binding& trusted, const nested_stream_limits& limits) {
    require(input.schema_id == trusted.schema_id && input.generation == trusted.generation && input.required_revision &&
            input.required_revision != std::numeric_limits<std::uint64_t>::max() &&
            input.revision == input.required_revision + 1 && input.root.entity_id == trusted.root_id &&
            input.root.incarnation == trusted.root_incarnation &&
            (input.removed ? empty(input.root) : !empty(input.root)));
    patch_budget budget(limits);
    consume(input.schema_id.size(), budget.strings);
    state_budget replacements(limits.snapshot);
    validate_patch(input.root, trusted.root, 0, budget, replacements);
}

inline void apply_patch(nested_entity_snapshot& candidate, const nested_entity_delta& patch) {
    require(candidate.scalars.entity_id == patch.entity_id && candidate.incarnation == patch.incarnation);
    for (const auto& [index, value] : patch.fields)
    {
        candidate.scalars.fields.at(index) = value;
    }
    for (const auto& [index, rows] : patch.tables)
    {
        auto& table = candidate.tables.at(index);
        for (const auto& [number, change] : rows)
        {
            const auto old = table.rows.find(number);
            require(change.required_incarnation
                        ? old != table.rows.end() && old->second.incarnation == change.required_incarnation
                        : old == table.rows.end());
            if (!change.incarnation)
            {
                table.rows.erase(old);
            }
            else if (change.incarnation != change.required_incarnation)
            {
                table.rows.insert_or_assign(number, nested_row_snapshot{change.incarnation, change.fields});
            }
            else
            {
                for (const auto& [column, value] : change.fields)
                {
                    old->second.fields.at(column) = value;
                }
            }
        }
    }
    for (const auto& [index, change] : patch.containers)
    {
        auto& children = candidate.containers.at(index);
        if (change.replace)
        {
            children = change.slots;
        }
        else
        {
            for (const auto& [slot, child] : change.changed)
            {
                const auto previous = children.find(slot);
                require(previous != children.end());
                apply_patch(previous->second, child);
            }
        }
    }
}

} // namespace nested_detail

// Single owner thread. Returned immutable publications do not acknowledge delivery.
class nested_publisher final
{
public:
    nested_publisher(
        const nested_stream_binding& trusted, const nested_baseline& initial, nested_stream_limits limits = {})
        : limits_(limits) {
        nested_detail::binding(trusted, limits_);
        nested_detail::require(initial.generation == trusted.generation && initial.revision);
        nested_detail::snapshot(initial.snapshot, trusted, limits_);
        binding_ = trusted;
        nested_detail::remember(initial.snapshot.root, history_, limits_.max_lifetime_entries);
        current_ = std::make_shared<const nested_baseline>(initial);
    }

    std::shared_ptr<const nested_baseline> baseline() const {
        active();
        return current_;
    }

    std::shared_ptr<const nested_delta> update(const nested_snapshot& next) {
        active();
        nested_detail::snapshot(next, binding_, limits_);
        if (next == current_->snapshot)
        {
            return {};
        }
        advance();
        auto history = history_;
        nested_detail::remember(next.root, history, limits_.max_lifetime_entries);
        nested_detail::patch_budget budget(limits_);
        nested_detail::consume(binding_.schema_id.size(), budget.strings);
        auto change = nested_detail::difference(current_->snapshot.root, next.root, budget);
        nested_delta envelope{binding_.schema_id, binding_.generation, current_->revision, current_->revision + 1,
            std::move(change), false};
        nested_detail::delta(envelope, binding_, limits_);
        auto packet = std::make_shared<const nested_delta>(std::move(envelope));
        auto baseline =
            std::make_shared<const nested_baseline>(nested_baseline{binding_.generation, current_->revision + 1, next});
        current_.swap(baseline);
        history_.swap(history);
        return packet;
    }

    std::shared_ptr<const nested_delta> remove() {
        active();
        advance();
        nested_delta envelope{binding_.schema_id, binding_.generation, current_->revision, current_->revision + 1,
            {binding_.root_id, binding_.root_incarnation, {}, {}, {}}, true};
        nested_detail::delta(envelope, binding_, limits_);
        auto packet = std::make_shared<const nested_delta>(std::move(envelope));
        removed_ = true;
        return packet;
    }

private:
    void active() const {
        if (removed_)
        {
            throw std::logic_error("Nested root was removed; bind a new stream");
        }
    }

    void advance() const {
        if (current_->revision == std::numeric_limits<std::uint64_t>::max())
        {
            throw std::overflow_error("Nested revision exhausted");
        }
    }

    nested_stream_limits limits_;
    nested_stream_binding binding_;
    nested_detail::lifetime_history history_;
    std::shared_ptr<const nested_baseline> current_;
    bool removed_{false};
};

class nested_replica final
{
public:
    explicit nested_replica(const nested_stream_binding& trusted, nested_stream_limits limits = {})
        : limits_(limits) {
        nested_detail::binding(trusted, limits_);
        binding_ = trusted;
    }

    nested_apply_result apply(const nested_baseline& input) {
        if (failed_)
        {
            return nested_apply_result::rejected;
        }
        try
        {
            nested_detail::require(input.generation == binding_.generation && input.revision);
            nested_detail::snapshot(input.snapshot, binding_, limits_);
            if (installed_)
            {
                if (input.revision < revision_ || (input.revision == revision_ && state_ && *state_ == input.snapshot))
                {
                    return nested_apply_result::duplicate;
                }
                return reject();
            }
            nested_detail::lifetime_history history;
            nested_detail::remember(input.snapshot.root, history, limits_.max_lifetime_entries);
            auto replacement = std::make_shared<const nested_snapshot>(input.snapshot);
            state_.swap(replacement);
            history_.swap(history);
            revision_ = input.revision;
            installed_ = true;
            return nested_apply_result::applied;
        }
        catch (const std::invalid_argument&)
        { return reject(); }
        catch (const std::length_error&)
        { return reject(); }
    }

    nested_apply_result apply(const nested_delta& input) {
        if (failed_)
        {
            return nested_apply_result::rejected;
        }
        try
        {
            nested_detail::delta(input, binding_, limits_);
            nested_detail::require(installed_);
            if (input.revision <= revision_)
            {
                return nested_apply_result::duplicate;
            }
            nested_detail::require(state_ && input.required_revision == revision_);
            if (input.removed)
            {
                state_.reset();
                revision_ = input.revision;
                return nested_apply_result::applied;
            }
            auto candidate = *state_;
            nested_detail::apply_patch(candidate.root, input.root);
            nested_detail::snapshot(candidate, binding_, limits_);
            auto history = history_;
            nested_detail::remember(candidate.root, history, limits_.max_lifetime_entries);
            auto replacement = std::make_shared<const nested_snapshot>(std::move(candidate));
            state_.swap(replacement);
            history_.swap(history);
            revision_ = input.revision;
            return nested_apply_result::applied;
        }
        catch (const std::invalid_argument&)
        { return reject(); }
        catch (const std::length_error&)
        { return reject(); }
        catch (const std::out_of_range&)
        { return reject(); }
    }

    bool valid() const noexcept {
        return installed_ && !failed_;
    }

    std::shared_ptr<const nested_snapshot> state() const noexcept {
        return state_;
    }

    std::uint64_t revision() const noexcept {
        return revision_;
    }

private:
    nested_apply_result reject() noexcept {
        state_.reset();
        failed_ = true;
        return nested_apply_result::rejected;
    }

    nested_stream_limits limits_;
    nested_stream_binding binding_;
    nested_detail::lifetime_history history_;
    std::shared_ptr<const nested_snapshot> state_;
    std::uint64_t revision_{};
    bool installed_{false}, failed_{false};
};

} // namespace ark::entity_sync
