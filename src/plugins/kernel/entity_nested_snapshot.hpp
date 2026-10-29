// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "ark/kernel/entity_snapshot.hpp"

namespace ark::entity_sync {

// Explicit authorization input. A table selection covers its rows; container
// children require individual GUID bindings and their own member selections.
struct nested_entity_projection
{
    std::string class_name;
    std::vector<std::uint32_t> fields;
    std::map<std::uint32_t, std::vector<std::uint32_t>> tables;
    std::map<std::uint32_t, std::map<guid_t, nested_entity_projection>> containers;
};

struct nested_projection
{
    std::string schema_id;
    guid_t root_id{};
    nested_entity_projection root;
};

struct nested_row_snapshot
{
    std::uint64_t incarnation{};
    std::map<std::uint32_t, scalar_value> fields;
    bool operator==(const nested_row_snapshot&) const = default;
};

struct nested_table_snapshot
{
    std::string class_name;
    std::map<std::uint32_t, nested_row_snapshot> rows;
    bool operator==(const nested_table_snapshot&) const = default;
};

struct nested_entity_snapshot
{
    std::uint64_t incarnation{};
    scalar_snapshot scalars;
    std::map<std::uint32_t, nested_table_snapshot> tables;
    // Container member index -> slot number -> authorized child state.
    std::map<std::uint32_t, std::map<std::uint32_t, nested_entity_snapshot>> containers;
    bool operator==(const nested_entity_snapshot&) const = default;
};

struct nested_snapshot
{
    std::string schema_id;
    nested_entity_snapshot root;
    bool operator==(const nested_snapshot&) const = default;
};

struct nested_snapshot_limits
{
    // Root depth is zero. Entity count also bounds inspected container entries.
    std::size_t max_depth{8};
    std::size_t max_entities{1024};
    std::size_t max_rows{65536};
    // Scalar values plus selected table/container members. Applied separately to
    // projection input and complete captured state, not only individual leaves.
    std::size_t max_fields{65536};
    // Schema ID, class names and selected byte strings, including embedded NULs.
    std::size_t max_string_bytes{1024 * 1024};
};

} // namespace ark::entity_sync
