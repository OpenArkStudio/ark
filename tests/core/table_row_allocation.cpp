// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <vector>

#include "../network/doctest.h"

#include "ark/kernel/class_meta.hpp"
#include "ark/kernel/table_impl.hpp"

#ifdef ARK_TABLE_ROW_FAILURE_TEST
#ifndef NDEBUG
#error The allocation failure regression must compile the production table with assertions disabled.
#endif

namespace {

bool fail_next_nothrow{};
unsigned failed_allocations{};

} // namespace

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
    { return ::operator new(size); }
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
        : table(table_metadata(), [this](ark::mask_type, ark::node*, const ark::table_event_data& event,
                                      const ark::data&, const ark::data&) {
            events.push_back(event);
            return 0;
        }) {
    }

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
    const auto initial_failures = failed_allocations;
    fail_next_nothrow = true;
    auto* failed = fixture.table.add_row(requested);
    CHECK_FALSE(fail_next_nothrow);
    CHECK(failed_allocations == initial_failures + 1);
    CHECK(failed == nullptr);
    CHECK(fixture.table.get_row_count() == 1);
    CHECK(fixture.table.find_row(expected) == nullptr);
    CHECK(fixture.table.find_row(1) == existing);
    CHECK(existing->get_int32(1) == 17);
    CHECK(fixture.events.empty());

    auto* retried = fixture.table.add_row(requested);
    REQUIRE(retried != nullptr);
    CHECK(retried->get_row() == expected);
    CHECK(fixture.table.get_row_count() == 2);
    REQUIRE(fixture.events.size() == 1);
    CHECK(fixture.events.front().row == expected);
    CHECK(fixture.events.front().op_type == static_cast<std::size_t>(ark::table_op_type::table_add));
}
#endif
