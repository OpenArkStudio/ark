// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

#include "ark/kernel/nested_stream.hpp"

namespace nested_test {
using namespace ark::entity_sync;

inline nested_entity_layout item_layout() {
    return {"Item", {{1, nested_scalar_type::uint32}},
        {{200, {"Line", {{1, nested_scalar_type::uint32}, {2, nested_scalar_type::bytes}}}}}, {}};
}

inline nested_stream_binding binding() {
    return {"nested/v1", 10, 1, 1,
        {"Root", {{1, nested_scalar_type::int32}, {2, nested_scalar_type::bytes}},
            {{200, {"Line", {{1, nested_scalar_type::uint32}, {2, nested_scalar_type::bytes}}}}},
            {{201, {{20, item_layout()}, {21, item_layout()}}}, {202, {{20, item_layout()}, {21, item_layout()}}}}}};
}

inline nested_entity_snapshot child(ark::guid_t id = 20, std::uint64_t incarnation = 2) {
    return {incarnation, {id, "Item", {{1, std::uint32_t{7}}}},
        {{200, {"Line", {{1, {1, {{1, std::uint32_t{3}}, {2, std::string("owned-child")}}}}}}}}, {}};
}

inline nested_baseline initial() {
    return {1, 1,
        {"nested/v1", {1, {10, "Root", {{1, std::int32_t{-7}}, {2, std::string("owned-root")}}},
                          {{200, {"Line", {{1, {1, {{1, std::uint32_t{5}}, {2, std::string("owned-row")}}}}}}}},
                          {{201, {{1, child()}}}, {202, {}}}}}};
}

inline nested_snapshot changed() {
    auto next = initial().snapshot;
    next.root.scalars.fields.at(2) = std::string(120, 'x');
    auto& rows = next.root.tables.at(200).rows;
    rows.at(1).fields.at(1) = std::uint32_t{9};
    rows.emplace(2, nested_row_snapshot{2, {{1, std::uint32_t{12}}, {2, std::string(80, 'r')}}});
    next.root.containers.at(201).at(1).scalars.fields.at(1) = std::uint32_t{8};
    return next;
}
} // namespace nested_test
