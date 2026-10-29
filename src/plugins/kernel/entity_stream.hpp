// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#include "ark/kernel/entity_snapshot.hpp"

namespace ark::entity_sync {

// In-process value envelopes, not a serialized message format or transport API.
struct scalar_baseline
{
    std::string schema_id;
    std::uint64_t generation{};
    std::uint64_t revision{};
    scalar_snapshot snapshot;
};

struct scalar_delta
{
    std::string schema_id;
    std::uint64_t generation{};
    std::uint64_t required_revision{};
    std::uint64_t revision{};
    guid_t entity_id{};
    std::string class_name;
    std::map<std::uint32_t, scalar_value> fields;
    bool removed{false};
};

enum class scalar_apply_result
{
    applied,
    duplicate,
    rejected
};

namespace detail {

inline void swap_scalar_snapshots(scalar_snapshot& left, scalar_snapshot& right) noexcept {
    static_assert(noexcept(left.class_name.swap(right.class_name)) && noexcept(left.fields.swap(right.fields)));
    std::swap(left.entity_id, right.entity_id);
    left.class_name.swap(right.class_name);
    left.fields.swap(right.fields);
}

inline void validate_scalar_state(guid_t id, const std::string& name,
    const std::map<std::uint32_t, scalar_value>& fields, const snapshot_limits& limits) {
    if (id == guid_t{} || name.empty())
    {
        throw std::invalid_argument("Scalar state requires an entity ID and class");
    }
    if (fields.size() > limits.max_fields || name.size() > limits.max_string_bytes)
    {
        throw std::length_error("Scalar state exceeds snapshot limits");
    }
    auto remaining = limits.max_string_bytes - name.size();
    for (const auto& [index, value] : fields)
    {
        if (index == 0 || value.valueless_by_exception())
        {
            throw std::invalid_argument("Invalid scalar field");
        }
        std::visit(
            [&](const auto& item) {
                using type = std::decay_t<decltype(item)>;
                if constexpr (std::is_floating_point_v<type>)
                {
                    if (!std::isfinite(item))
                    {
                        throw std::invalid_argument("Non-finite scalar field");
                    }
                }
                else if constexpr (std::is_same_v<type, std::string>)
                {
                    if (item.size() > remaining)
                    {
                        throw std::length_error("Scalar strings exceed snapshot limits");
                    }
                    remaining -= item.size();
                }
            },
            value);
    }
}

inline void validate_scalar_schema(const std::string& schema, const snapshot_limits& limits) {
    if (schema.empty())
    {
        throw std::invalid_argument("A scalar stream requires an application schema ID");
    }
    if (schema.size() > limits.max_string_bytes)
    {
        throw std::length_error("Scalar schema ID exceeds snapshot limits");
    }
}

} // namespace detail

// Owner-thread state differ. Returning a delta does not acknowledge network delivery.
// Call update once at the application's explicit flush point. A changed selection
// or schema requires a new publisher/generation and a new baseline.
class scalar_publisher final
{
public:
    explicit scalar_publisher(const scalar_baseline& initial, snapshot_limits limits = {})
        : limits_(limits)
        , current_(prepare_initial(initial, limits)) {
    }

    scalar_baseline baseline() const {
        require_active();
        return {current_.schema_id, current_.generation, current_.revision,
            detail::copy_scalar_snapshot(current_.snapshot)};
    }

    std::shared_ptr<const scalar_delta> update(const scalar_snapshot& next) {
        require_active();
        detail::validate_scalar_state(next.entity_id, next.class_name, next.fields, limits_);
        if (next.entity_id != current_.snapshot.entity_id || next.class_name != current_.snapshot.class_name ||
            next.fields.size() != current_.snapshot.fields.size())
        {
            throw std::invalid_argument("Scalar identity or selection changed; establish a new baseline");
        }
        auto delta = envelope();
        for (const auto& [index, value] : next.fields)
        {
            const auto before = current_.snapshot.fields.find(index);
            if (before == current_.snapshot.fields.end() || before->second.index() != value.index())
            {
                throw std::invalid_argument("Scalar field selection or type changed");
            }
            if (before->second != value)
            {
                delta.fields.emplace(index, detail::direct_value{[&] { return detail::copy_scalar_value(value); }});
            }
        }
        if (delta.fields.empty())
        {
            return {};
        }
        advance(delta);
        auto replacement = detail::copy_scalar_snapshot(next);
        auto result = share_delta(delta);
        detail::swap_scalar_snapshots(current_.snapshot, replacement);
        ++current_.revision;
        return result;
    }

    std::shared_ptr<const scalar_delta> remove() {
        require_active();
        auto delta = envelope();
        advance(delta);
        delta.removed = true;
        auto result = share_delta(delta);
        ++current_.revision;
        removed_ = true;
        return result;
    }

private:
    static scalar_baseline prepare_initial(const scalar_baseline& initial, const snapshot_limits& limits) {
        detail::validate_scalar_schema(initial.schema_id, limits);
        detail::validate_scalar_state(
            initial.snapshot.entity_id, initial.snapshot.class_name, initial.snapshot.fields, limits);
        if (initial.generation == 0 || initial.revision == 0)
        {
            throw std::invalid_argument("Scalar generation and revision must be positive");
        }
        return {
            initial.schema_id, initial.generation, initial.revision, detail::copy_scalar_snapshot(initial.snapshot)};
    }

    static std::shared_ptr<const scalar_delta> share_delta(scalar_delta& staged) {
        return std::make_shared<const scalar_delta>(detail::direct_value{[&] {
            return scalar_delta{staged.schema_id, staged.generation, staged.required_revision, staged.revision,
                staged.entity_id, staged.class_name, std::move(staged.fields), staged.removed};
        }});
    }

    void require_active() const {
        if (removed_)
        {
            throw std::logic_error("Scalar entity was removed; establish a new generation");
        }
    }

    scalar_delta envelope() const {
        return {current_.schema_id, current_.generation, current_.revision, 0, current_.snapshot.entity_id,
            current_.snapshot.class_name, {}, false};
    }

    void advance(scalar_delta& delta) const {
        if (current_.revision == std::numeric_limits<std::uint64_t>::max())
        {
            throw std::overflow_error("Scalar revision exhausted; establish a new generation");
        }
        delta.revision = current_.revision + 1;
    }

    snapshot_limits limits_;
    scalar_baseline current_;
    bool removed_{false};
};

// Validates in-process envelopes for an explicitly bound root. It is not a wire
// decoder, schema-definition validator or authorization/session mechanism.
class scalar_replica final
{
public:
    scalar_replica(std::string schema, guid_t id, std::string class_name, snapshot_limits limits = {})
        : schema_(std::move(schema))
        , id_(id)
        , class_name_(std::move(class_name))
        , limits_(limits) {
        detail::validate_scalar_schema(schema_, limits_);
        detail::validate_scalar_state(id_, class_name_, {}, limits_);
    }

    scalar_apply_result apply(const scalar_baseline& input) {
        try
        {
            detail::validate_scalar_schema(input.schema_id, limits_);
            detail::validate_scalar_state(
                input.snapshot.entity_id, input.snapshot.class_name, input.snapshot.fields, limits_);
        }
        catch (const std::invalid_argument&)
        { return reject(); }
        catch (const std::length_error&)
        { return reject(); }
        if (input.schema_id != schema_ || input.snapshot.entity_id != id_ || input.snapshot.class_name != class_name_ ||
            input.generation == 0 || input.revision == 0 || input.generation < generation_)
        {
            return reject();
        }
        if (input.generation == generation_)
        {
            if (valid_ &&
                (input.revision < revision_ || (input.revision == revision_ && state_ && *state_ == input.snapshot)))
            {
                return scalar_apply_result::duplicate;
            }
            return reject();
        }
        auto replacement = std::make_shared<const scalar_snapshot>(
            detail::direct_value{[&] { return detail::copy_scalar_snapshot(input.snapshot); }});
        state_ = std::move(replacement);
        generation_ = input.generation;
        revision_ = input.revision;
        valid_ = true;
        return scalar_apply_result::applied;
    }

    scalar_apply_result apply(const scalar_delta& input) {
        try
        { detail::validate_scalar_state(input.entity_id, input.class_name, input.fields, limits_); }
        catch (const std::invalid_argument&)
        { return reject(); }
        catch (const std::length_error&)
        { return reject(); }
        if (!valid_ || input.schema_id != schema_ || input.entity_id != id_ || input.class_name != class_name_ ||
            input.generation != generation_ || input.required_revision == 0 ||
            input.required_revision == std::numeric_limits<std::uint64_t>::max() ||
            input.revision != input.required_revision + 1 || (input.removed && !input.fields.empty()) ||
            (!input.removed && input.fields.empty()))
        {
            return reject();
        }
        if (input.revision <= revision_)
        {
            return scalar_apply_result::duplicate;
        }
        if (input.required_revision != revision_ || !state_)
        {
            return reject();
        }
        if (input.removed)
        {
            state_.reset();
        }
        else
        {
            for (const auto& [index, value] : input.fields)
            {
                const auto old = state_->fields.find(index);
                if (old == state_->fields.end() || old->second.index() != value.index())
                {
                    return reject();
                }
            }
            // Validate the merged text budget before allocating a state copy.
            auto remaining = limits_.max_string_bytes - class_name_.size();
            for (const auto& [index, old] : state_->fields)
            {
                const auto changed = input.fields.find(index);
                const auto& value = changed == input.fields.end() ? old : changed->second;
                if (const auto text = std::get_if<std::string>(&value))
                {
                    if (text->size() > remaining)
                    {
                        return reject();
                    }
                    remaining -= text->size();
                }
            }
            auto replacement = detail::copy_scalar_snapshot(*state_);
            for (const auto& [index, value] : input.fields)
            {
                replacement.fields.at(index) = detail::copy_scalar_value(value);
            }
            auto immutable = std::make_shared<const scalar_snapshot>(detail::direct_value{[&] {
                return scalar_snapshot{replacement.entity_id, replacement.class_name, std::move(replacement.fields)};
            }});
            state_ = std::move(immutable);
        }
        revision_ = input.revision;
        return scalar_apply_result::applied;
    }

    bool valid() const noexcept {
        return valid_;
    }

    std::shared_ptr<const scalar_snapshot> state() const noexcept {
        return state_;
    }

    std::uint64_t revision() const noexcept {
        return revision_;
    }

private:
    scalar_apply_result reject() noexcept {
        state_.reset();
        valid_ = false;
        return scalar_apply_result::rejected;
    }

    std::string schema_;
    guid_t id_;
    std::string class_name_;
    snapshot_limits limits_;
    std::uint64_t generation_{};
    std::uint64_t revision_{};
    std::shared_ptr<const scalar_snapshot> state_;
    bool valid_{false};
};

} // namespace ark::entity_sync
