// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <atomic>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "../network/doctest.h"

#include "../../src/plugins/kernel/detail/incarnation.hpp"
#include "ark/kernel/data_list_impl.hpp"
#include "ark/kernel/entity_impl.hpp"
#include "ark/kernel/entity_nested_snapshot.hpp"
#include "ark/kernel/kernel_module_impl.hpp"
#include "kernel_snapshot_test_access.hpp"

namespace {

using namespace ark::entity_sync;

class nested_world
{
public:
    nested_world() {
        root_schema->create_data_meta("amount", 1)->set_type(ark::data_type::dt_int32);
        root_schema->create_data_meta("secret", 2)->set_type(ark::data_type::dt_string);
        for (const auto& schema : {row_schema, item_schema})
        {
            schema->create_data_meta("amount", 1)->set_type(ark::data_type::dt_uint32);
            schema->create_data_meta("label", 2)->set_type(ark::data_type::dt_string);
        }
        row_schema->create_data_meta("wide", 3)->set_type(ark::data_type::dt_wstring);
        root_schema->create_table_meta("items", 200)->set_class_meta(row_schema);
        root_schema->create_container_meta("bag", 201, "Item", {});
        item_schema->create_container_meta("bag", 201, "Item", {});
        root = make(root_schema, 10);
    }

    ~nested_world() {
        // Existing parent relationships are strong references. Detach even
        // deliberately malformed/cyclic test graphs before releasing the world.
        for (const auto& entity : entities)
        {
            const auto bag = entity->find_container(201);
            bag->remove(0u);
            while (const auto slot = bag->first())
            {
                bag->remove(slot);
            }
        }
    }

    std::shared_ptr<ark::entity_impl> make(std::shared_ptr<ark::class_meta> schema, ark::guid_t id) {
        auto value = std::make_shared<ark::entity_impl>(schema, id, 0, 1, 0, ark::data_list_impl{});
        entities.push_back(value);
        return value;
    }

    nested_projection selection() const {
        return {"nested/v1", root->get_id(), {"Root", {1}, {{200, {1, 2}}}, {}}};
    }

    nested_snapshot capture(const nested_projection& projection, nested_snapshot_limits limits = {}) {
        return kernel.capture_nested_snapshot(*root, projection, limits);
    }

    std::shared_ptr<ark::class_meta> root_schema{std::make_shared<ark::class_meta>("Root")};
    std::shared_ptr<ark::class_meta> row_schema{std::make_shared<ark::class_meta>("Line")};
    std::shared_ptr<ark::class_meta> item_schema{std::make_shared<ark::class_meta>("Item")};
    std::vector<std::shared_ptr<ark::entity_impl>> entities;
    std::shared_ptr<ark::entity_impl> root;
    ark::kernel_module_impl kernel;
};

std::size_t string_bytes(const nested_entity_snapshot& state) {
    std::size_t total = state.scalars.class_name.size();
    for (const auto& [index, value] : state.scalars.fields)
    {
        if (const auto text = std::get_if<std::string>(&value))
        {
            total += text->size();
        }
    }
    for (const auto& [index, table] : state.tables)
    {
        total += table.class_name.size();
        for (const auto& [number, row] : table.rows)
        {
            for (const auto& [column, value] : row.fields)
            {
                if (const auto text = std::get_if<std::string>(&value))
                {
                    total += text->size();
                }
            }
        }
    }
    for (const auto& [index, container] : state.containers)
    {
        for (const auto& [slot, child] : container)
        {
            total += string_bytes(child);
        }
    }
    return total;
}

} // namespace

TEST_CASE("Nested owning capture preserves row keys and distinct lifetimes across deletion reuse and clear") {
    nested_world world;
    const auto selection = world.selection();
    auto* table = world.root->find_table(200);
    REQUIRE(table != nullptr);
    for (const auto number : {1u, 2u, 3u})
    {
        REQUIRE(table->add_row(number) != nullptr);
    }
    const auto original = world.capture(selection);
    CHECK(original.root.tables.at(200).rows.size() == 3);
    const auto old_lifetime = original.root.tables.at(200).rows.at(2).incarnation;
    REQUIRE(table->remove_row(2));
    const auto deleted = world.capture(selection);
    CHECK(deleted.root.tables.at(200).rows.count(2) == 0);
    CHECK(deleted.root.tables.at(200).rows.at(3) == original.root.tables.at(200).rows.at(3));
    const auto* replacement = table->add_row();
    REQUIRE(replacement != nullptr);
    CHECK(replacement->get_row() == 2);
    const auto replaced = world.capture(selection);
    CHECK(replaced.root.tables.at(200).rows.at(2).fields == original.root.tables.at(200).rows.at(2).fields);
    CHECK(replaced.root.tables.at(200).rows.at(2).incarnation > old_lifetime);
    CHECK(world.capture(selection) == replaced);
    table->clear();
    REQUIRE(table->add_row(2) != nullptr);
    CHECK(world.capture(selection).root.tables.at(200).rows.at(2).incarnation >
          replaced.root.tables.at(200).rows.at(2).incarnation);
    CHECK(original.root.tables.at(200).rows.size() == 3);
}

TEST_CASE("Nested capture reflects committed row state when add delete or clear callbacks throw") {
    nested_world world;
    auto* table = world.root->find_table(200);
    REQUIRE(table->add_row(1) != nullptr);
    std::size_t fail_operation = 0;
    REQUIRE(world.root_schema->get_class_call_back_manager()->add_table_call_back(
        200,
        [&](const ark::guid_t&, const ark::table_event_data& event, const ark::data&, const ark::data&) {
            if (event.op_type == fail_operation)
            {
                throw std::runtime_error("table callback");
            }
            return 0;
        },
        0));
    const auto before = world.capture(world.selection());
    SUBCASE("insert has committed before its callback throws") {
        fail_operation = static_cast<std::size_t>(ark::table_op_type::table_add);
        CHECK_THROWS_AS(table->add_row(2), std::runtime_error);
        const auto captured = world.capture(world.selection());
        CHECK(captured.root.tables.at(200).rows.size() == 2);
        CHECK(captured.root.tables.at(200).rows.at(2).incarnation > before.root.tables.at(200).rows.at(1).incarnation);
    }
    SUBCASE("delete callback throws before erasing the row") {
        fail_operation = static_cast<std::size_t>(ark::table_op_type::table_delete);
        CHECK_THROWS_AS(table->remove_row(1), std::runtime_error);
        CHECK(world.capture(world.selection()) == before);
    }
    SUBCASE("clear callback throws before erasing rows") {
        fail_operation = static_cast<std::size_t>(ark::table_op_type::table_clear);
        CHECK_THROWS_AS(table->clear(), std::runtime_error);
        CHECK(world.capture(world.selection()) == before);
    }
}

TEST_CASE("Nested row and Entity incarnation counters reject exhaustion without wrapping") {
    std::atomic<std::uint64_t> counter{std::numeric_limits<std::uint64_t>::max() - 1};
    CHECK(ark::detail::next_incarnation(counter) == std::numeric_limits<std::uint64_t>::max());
    CHECK_THROWS_AS(ark::detail::next_incarnation(counter), std::overflow_error);
    CHECK(counter == std::numeric_limits<std::uint64_t>::max());
    nested_world world;
    auto* table = dynamic_cast<ark::table_impl*>(world.root->find_table(200));
    REQUIRE(table != nullptr);
    ark::kernel_nested_snapshot_test_access::row_counter(*table, std::numeric_limits<std::uint64_t>::max() - 1);
    REQUIRE(table->add_row(1) != nullptr);
    const auto before = world.capture(world.selection());
    CHECK(before.root.tables.at(200).rows.at(1).incarnation == std::numeric_limits<std::uint64_t>::max());
    CHECK_THROWS_AS(table->add_row(2), std::overflow_error);
    CHECK(world.capture(world.selection()) == before);
    table->clear();
    CHECK_THROWS_AS(table->add_row(1), std::overflow_error);
    CHECK(table->get_row_count() == 0);
}

TEST_CASE("Nested capture binds authorization and resolves scalar columns within each local class") {
    nested_world world;
    REQUIRE(world.root->set_int32(1, -7));
    REQUIRE(world.root->set_string(2, "hidden"));
    auto* row = world.root->find_table(200)->add_row(3);
    REQUIRE(row != nullptr);
    REQUIRE(row->set_u_int32(1, 42));
    const auto child = world.make(world.item_schema, 20);
    REQUIRE(child->set_u_int32(1, 99));
    REQUIRE(child->set_string(2, "child-secret"));
    const auto hidden = world.make(world.item_schema, 21);
    const auto bag = world.root->find_container(201);
    REQUIRE(bag->place(4, child));
    REQUIRE(bag->place(5, hidden));
    auto selection = world.selection();
    selection.root.containers = {{201, {{20, {"Item", {1}, {}, {}}}}}};
    const auto state = world.capture(selection);
    CHECK(std::get<std::int32_t>(state.root.scalars.fields.at(1)) == -7);
    CHECK(state.root.scalars.fields.count(2) == 0);
    CHECK(std::get<std::uint32_t>(state.root.tables.at(200).rows.at(3).fields.at(1)) == 42);
    CHECK(std::get<std::string>(state.root.tables.at(200).rows.at(3).fields.at(2)).empty());
    const auto& children = state.root.containers.at(201);
    CHECK(children.size() == 1);
    CHECK(std::get<std::uint32_t>(children.at(4).scalars.fields.at(1)) == 99);
    CHECK(children.at(4).scalars.fields.count(2) == 0);
    selection.root.containers.at(201).clear();
    CHECK(world.capture(selection).root.containers.at(201).empty());
    CHECK_THROWS_AS(world.capture({}), std::invalid_argument);
    selection.root_id = 999;
    CHECK_THROWS_AS(world.capture(selection), std::invalid_argument);
}

TEST_CASE("Nested capture owns binary values and distinguishes Entity GUID and slot reuse") {
    nested_snapshot retained;
    {
        nested_world world;
        auto selection = world.selection();
        selection.root.fields = {2};
        REQUIRE(world.root->set_string(2, std::string("a\0b", 3)));
        auto* row = world.root->find_table(200)->add_row(1);
        REQUIRE(row->set_string(2, std::string("c\0d", 3)));
        const auto child = world.make(world.item_schema, 20);
        const auto bag = world.root->find_container(201);
        REQUIRE(bag->place(1, child));
        selection.root.containers = {{201, {{20, {"Item", {1}, {}, {}}}}}};
        retained = world.capture(selection);
        REQUIRE(bag->remove(1u));
        const auto replacement = world.make(world.item_schema, 20);
        REQUIRE(bag->place(1, replacement));
        const auto next = world.capture(selection);
        CHECK(next.root.containers.at(201).at(1).scalars == retained.root.containers.at(201).at(1).scalars);
        CHECK(next.root.containers.at(201).at(1).incarnation != retained.root.containers.at(201).at(1).incarnation);
        const auto replacement_root = world.make(world.root_schema, 10);
        CHECK(world.kernel.capture_nested_snapshot(*replacement_root, selection).root.incarnation !=
              retained.root.incarnation);
        REQUIRE(world.root->set_string(2, "changed"));
        world.root->find_table(200)->clear();
    }
    CHECK(std::get<std::string>(retained.root.scalars.fields.at(2)) == std::string("a\0b", 3));
    CHECK(std::get<std::string>(retained.root.tables.at(200).rows.at(1).fields.at(2)) == std::string("c\0d", 3));
    CHECK(retained.root.containers.at(201).at(1).incarnation != 0);
}

TEST_CASE("Nested capture recursively owns explicitly authorized container descendants") {
    nested_world world;
    const auto child = world.make(world.item_schema, 20);
    const auto grandchild = world.make(world.item_schema, 30);
    REQUIRE(grandchild->set_u_int32(1, 123));
    REQUIRE(world.root->find_container(201)->place(1, child));
    REQUIRE(child->find_container(201)->place(7, grandchild));
    auto selection = world.selection();
    selection.root.containers = {{201, {{20, {"Item", {1}, {}, {{201, {{30, {"Item", {1}, {}, {}}}}}}}}}}};
    nested_snapshot_limits limits;
    limits.max_depth = 2;
    limits.max_entities = 3;
    const auto snapshot = world.capture(selection, limits);
    const auto& leaf = snapshot.root.containers.at(201).at(1).containers.at(201).at(7);
    CHECK(leaf.scalars.entity_id == 30);
    CHECK(std::get<std::uint32_t>(leaf.scalars.fields.at(1)) == 123);
    REQUIRE(grandchild->set_u_int32(1, 456));
    CHECK(std::get<std::uint32_t>(leaf.scalars.fields.at(1)) == 123);
    limits.max_depth = 1;
    CHECK_THROWS_AS(world.capture(selection, limits), std::length_error);
}

TEST_CASE("Nested capture rejects invalid selections unsupported columns and non-finite row values") {
    nested_world world;
    auto selection = world.selection();
    SUBCASE("unknown root scalar") {
        selection.root.fields = {77};
    }
    SUBCASE("duplicate scalar") {
        selection.root.fields = {1, 1};
    }
    SUBCASE("zero scalar") {
        selection.root.fields = {0};
    }
    SUBCASE("wrong root class") {
        selection.root.class_name = "Item";
    }
    SUBCASE("unknown table") {
        selection.root.tables = {{999, {1}}};
    }
    SUBCASE("column is local to row class") {
        selection.root.tables = {{200, {99}}};
    }
    SUBCASE("duplicate column") {
        selection.root.tables = {{200, {1, 1}}};
    }
    SUBCASE("wide string even on an empty table") {
        selection.root.tables = {{200, {3}}};
    }
    SUBCASE("unknown container") {
        selection.root.containers = {{999, {}}};
    }
    SUBCASE("wrong child class even when absent") {
        selection.root.containers = {{201, {{20, {"Root", {}, {}, {}}}}}};
    }
    SUBCASE("non-finite row scalar") {
        world.row_schema->create_data_meta("position", 4)->set_type(ark::data_type::dt_double);
        const auto row = world.root->find_table(200)->add_row(1);
        REQUIRE(row->set_double(4, std::numeric_limits<double>::infinity()));
        selection.root.tables = {{200, {4}}};
    }
    CHECK_THROWS_AS(world.capture(selection), std::invalid_argument);
}

TEST_CASE("Nested capture bounds aggregate state and leaves the previous value intact on failure") {
    nested_world world;
    auto selection = world.selection();
    selection.root.fields = {1, 2};
    REQUIRE(world.root->set_string(2, "root-text"));
    for (const auto number : {1u, 2u})
    {
        const auto row = world.root->find_table(200)->add_row(number);
        REQUIRE(row->set_string(2, "row-text"));
    }
    const auto child = world.make(world.item_schema, 20);
    REQUIRE(child->set_string(2, "child-text"));
    REQUIRE(world.root->find_container(201)->place(1, child));
    selection.root.containers = {{201, {{20, {"Item", {1, 2}, {}, {}}}}}};
    const auto before = world.capture(selection);
    auto destination = before;
    nested_snapshot_limits limits;
    limits.max_entities = 2;
    limits.max_rows = 2;
    limits.max_fields = 10;
    limits.max_string_bytes = before.schema_id.size() + string_bytes(before.root);
    CHECK(world.capture(selection, limits) == before);
    SUBCASE("rows") {
        limits.max_rows = 1;
    }
    SUBCASE("fields across root rows and child") {
        limits.max_fields = 9;
    }
    SUBCASE("strings across root rows and child") {
        --limits.max_string_bytes;
    }
    SUBCASE("entities including authorization input") {
        limits.max_entities = 1;
    }
    SUBCASE("inspected hidden children") {
        REQUIRE(world.root->find_container(201)->place(2, world.make(world.item_schema, 21)));
        REQUIRE(world.root->find_container(201)->place(3, world.make(world.item_schema, 22)));
    }
    SUBCASE("depth including authorization input") {
        limits.max_depth = 0;
    }
    CHECK_THROWS(destination = world.capture(selection, limits));
    CHECK(destination == before);
    CHECK(world.capture(selection) == before);
}

TEST_CASE("Nested capture rejects repeated references duplicate GUIDs invalid slots and cycles") {
    nested_world world;
    auto selection = world.selection();
    const auto child = world.make(world.item_schema, 20);
    const auto bag = world.root->find_container(201);
    selection.root.containers = {{201, {{20, {"Item", {1}, {}, {}}}}}};
    SUBCASE("same child in two slots") {
        REQUIRE(bag->place(1, child));
        REQUIRE(bag->place(2, child));
    }
    SUBCASE("two different children with the same GUID") {
        REQUIRE(bag->place(1, child));
        REQUIRE(bag->place(2, world.make(world.item_schema, 20)));
    }
    SUBCASE("zero slot") {
        REQUIRE(bag->place(0, child));
    }
    SUBCASE("stale parent relationship") {
        REQUIRE(bag->place(1, child));
        REQUIRE(child->set_parent_container(nullptr));
    }
    SUBCASE("self cycle") {
        const auto own_bag = child->find_container(201);
        REQUIRE(own_bag->place(1, child));
        nested_projection self{"nested/v1", 20, {"Item", {}, {}, {{201, {{20, {"Item", {}, {}, {}}}}}}}};
        CHECK_THROWS_AS(world.kernel.capture_nested_snapshot(*child, self), std::invalid_argument);
        return;
    }
    CHECK_THROWS_AS(world.capture(selection), std::invalid_argument);
}
