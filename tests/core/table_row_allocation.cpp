// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <new>
#include <stdexcept>
#include <vector>

#include "../network/doctest.h"

#include "ark/kernel/class_meta.hpp"
#include "ark/kernel/row.hpp"
#include "ark/kernel/table_impl.hpp"
#include "kernel_snapshot_test_access.hpp"

#ifdef ARK_TABLE_ROW_FAILURE_TEST
#ifndef NDEBUG
#error The allocation failure regression must compile the production table with assertions disabled.
#endif

namespace {

bool fail_next_nothrow{};
unsigned failed_allocations{};
ark::table_impl* observed_table{};
std::uint32_t initial_row_count{};
std::size_t allocation_calls{}, insertion_allocation{}, throw_allocation{};
void* candidate_row{};
unsigned candidate_releases{};

void observe_allocations(ark::table_impl& table, std::size_t throw_at = 0) {
    initial_row_count = table.get_row_count();
    allocation_calls = insertion_allocation = 0;
    throw_allocation = throw_at;
    observed_table = &table;
}

void stop_observing_allocations() {
    observed_table = nullptr;
    throw_allocation = 0;
}

} // namespace

void* operator new(std::size_t size) {
    if (observed_table)
    {
        ++allocation_calls;
        if (observed_table->get_row_count() == initial_row_count)
        {
            insertion_allocation = allocation_calls;
        }
        if (allocation_calls == throw_allocation)
        {
            throw std::bad_alloc{};
        }
    }
    if (auto* value = std::malloc(size ? size : 1))
    {
        return value;
    }
    throw std::bad_alloc{};
}

void operator delete(void* value) noexcept {
    if (value && value == candidate_row)
    {
        candidate_row = nullptr;
        ++candidate_releases;
    }
    std::free(value);
}

void operator delete(void* value, std::size_t) noexcept {
    ::operator delete(value);
}

// Keep the production ARK_NEW hook unchanged. Successful allocations retain
// ordinary new/delete pairing; only the armed nothrow allocation returns null.
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    if (fail_next_nothrow)
    {
        fail_next_nothrow = false;
        ++failed_allocations;
        return nullptr;
    }
    try
    {
        auto* value = ::operator new(size);
        if (observed_table)
        {
            candidate_row = value;
        }
        return value;
    }
    catch (...)
    { return nullptr; }
}

void operator delete(void* value, const std::nothrow_t&) noexcept {
    ::operator delete(value);
}
#endif

namespace {

std::shared_ptr<ark::table_meta> table_metadata() {
    auto row_schema = std::make_shared<ark::class_meta>("Item");
    row_schema->create_data_meta("count", 1)->set_type(ark::data_type::dt_int32);
    auto metadata = std::make_shared<ark::table_meta>("inventory", 200);
    metadata->set_class_meta(row_schema);
    return metadata;
}

class table_fixture
{
public:
    table_fixture()
        : table(metadata, [this](ark::mask_type, ark::node*, const ark::table_event_data& event, const ark::data&,
                              const ark::data&) {
            events.push_back(event);
            return 0;
        }) {
    }

    std::shared_ptr<ark::table_meta> metadata{table_metadata()};
    std::vector<ark::table_event_data> events;
    ark::table_impl table;
};

} // namespace

TEST_CASE("Kernel table row insertion preserves automatic and explicit indices and success events") {
    table_fixture fixture;
    auto* first = fixture.table.add_row();
    REQUIRE(first != nullptr);
    CHECK(first->get_row() == 1);
    auto* second = fixture.table.add_row();
    REQUIRE(second != nullptr);
    CHECK(second->get_row() == 2);
    auto* explicit_row = fixture.table.add_row(7);
    REQUIRE(explicit_row != nullptr);
    CHECK(explicit_row->get_row() == 7);
    auto* next = fixture.table.add_row();
    REQUIRE(next != nullptr);
    CHECK(next->get_row() == 3);
    CHECK(fixture.table.get_row_count() == 4);
    REQUIRE(fixture.events.size() == 4);
    const unsigned indices[]{1, 2, 7, 3};
    for (unsigned i = 0; i < 4; ++i)
    {
        CHECK(fixture.events[i].row == indices[i]);
        CHECK(fixture.events[i].op_type == static_cast<std::size_t>(ark::table_op_type::table_add));
    }
}

TEST_CASE("Kernel table callback exceptions preserve already published rows") {
    ark::table_impl table(table_metadata(),
        [](ark::mask_type, ark::node*, const ark::table_event_data& event, const ark::data&, const ark::data&) {
            if (event.op_type == static_cast<std::size_t>(ark::table_op_type::table_add))
            {
                throw std::runtime_error("after publication");
            }
            return 0;
        });
    CHECK_THROWS_AS(table.add_row(7), std::runtime_error);
    CHECK(table.get_row_count() == 1);
    REQUIRE(table.find_row(7) != nullptr);
    CHECK(table.find_row(7)->get_row() == 7);
    CHECK(table.remove_row(7));
    CHECK(table.get_row_count() == 0);
}

#ifdef ARK_TABLE_ROW_FAILURE_TEST
TEST_CASE("Kernel table null row allocation preserves records indices and success events for retry") {
    std::uint32_t requested = 0;
    std::uint32_t expected = 2;
    SUBCASE("automatic index") {
    }
    SUBCASE("explicit index") {
        requested = expected = 7;
    }

    table_fixture fixture;
    auto* existing = fixture.table.add_row();
    REQUIRE(existing != nullptr);
    existing->set_int32(1, 17);
    fixture.events.clear();
    const auto initial_incarnation = ark::kernel_nested_snapshot_test_access::row_counter(fixture.table);
    const auto initial_failures = failed_allocations;
    fail_next_nothrow = true;
    auto* failed = fixture.table.add_row(requested);
    CHECK_FALSE(fail_next_nothrow);
    CHECK(failed_allocations == initial_failures + 1);
    CHECK(failed == nullptr);
    CHECK(ark::kernel_nested_snapshot_test_access::row_counter(fixture.table) == initial_incarnation);
    CHECK(fixture.table.get_row_count() == 1);
    CHECK(fixture.table.find_row(expected) == nullptr);
    CHECK(fixture.table.find_row(1) == existing);
    CHECK(existing->get_int32(1) == 17);
    CHECK(fixture.events.empty());

    auto* retried = fixture.table.add_row(requested);
    REQUIRE(retried != nullptr);
    CHECK(retried->get_row() == expected);
    CHECK(ark::kernel_nested_snapshot_test_access::row_counter(fixture.table) == initial_incarnation + 1);
    CHECK(fixture.table.get_row_count() == 2);
    REQUIRE(fixture.events.size() == 1);
    CHECK(fixture.events.front().row == expected);
    CHECK(fixture.events.front().op_type == static_cast<std::size_t>(ark::table_op_type::table_add));
}

TEST_CASE("Kernel table insertion exceptions release only the unpublished row and permit retry") {
    std::uint32_t requested = 0;
    std::uint32_t expected = 2;
    SUBCASE("automatic index") {
    }
    SUBCASE("explicit index") {
        requested = expected = 7;
    }

    std::size_t insertion_site{};
    {
        table_fixture control;
        REQUIRE(control.table.add_row() != nullptr);
        control.events.reserve(4);
        // The final allocation before the table grows is the owning map node.
        // Allocations from success callbacks occur after publication and are excluded.
        observe_allocations(control.table);
        auto* added = control.table.add_row(requested);
        stop_observing_allocations();
        REQUIRE(added != nullptr);
        insertion_site = insertion_allocation;
        REQUIRE(insertion_site > 1);
    }
    REQUIRE(candidate_row == nullptr);

    table_fixture fixture;
    auto* existing = fixture.table.add_row();
    REQUIRE(existing != nullptr);
    existing->set_int32(1, 17);
    fixture.events.reserve(4);
    fixture.events.clear();
    const std::weak_ptr<ark::class_meta> schema = fixture.metadata->get_class_meta();
    const auto schema_owners = schema.use_count();
    const auto initial_incarnation = ark::kernel_nested_snapshot_test_access::row_counter(fixture.table);
    const auto initial_releases = candidate_releases;
    bool threw = false;
    observe_allocations(fixture.table, insertion_site);
    try
    { static_cast<void>(fixture.table.add_row(requested)); }
    catch (const std::bad_alloc&)
    { threw = true; }
    stop_observing_allocations();
    CHECK(threw);
    CHECK(ark::kernel_nested_snapshot_test_access::row_counter(fixture.table) == initial_incarnation);
    CHECK(allocation_calls == insertion_site);
    CHECK(fixture.table.get_row_count() == 1);
    CHECK(fixture.table.find_row(expected) == nullptr);
    CHECK(fixture.table.find_row(1) == existing);
    CHECK(existing->get_int32(1) == 17);
    CHECK(fixture.events.empty());
    CHECK(schema.use_count() == schema_owners);
    CHECK(candidate_releases == initial_releases + 1);
    CHECK(candidate_row == nullptr);
    // Clean up a leaked candidate when checking an unfixed implementation.
    // An already published row remains owned by the table.
    if (candidate_row && fixture.table.find_row(expected) == nullptr)
    {
        delete static_cast<ark::row_impl*>(candidate_row);
    }

    auto* retried = fixture.table.add_row(requested);
    REQUIRE(retried != nullptr);
    CHECK(retried->get_row() == expected);
    CHECK(ark::kernel_nested_snapshot_test_access::row_counter(fixture.table) == initial_incarnation + 1);
    CHECK(fixture.table.get_row_count() == 2);
    REQUIRE(fixture.events.size() == 1);
    CHECK(fixture.events.front().row == expected);
    CHECK(fixture.events.front().op_type == static_cast<std::size_t>(ark::table_op_type::table_add));
}
#endif
