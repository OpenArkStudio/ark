// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <cstdlib>
#include <new>

#include "../network/doctest.h"

#include "nested_stream_fixture.hpp"

namespace {
std::size_t fail_at{}, allocation_count{};
bool armed{};
} // namespace

void* operator new(std::size_t size) {
    if (armed && ++allocation_count == fail_at)
    {
        throw std::bad_alloc{};
    }
    if (void* memory = std::malloc(size ? size : 1))
    {
        return memory;
    }
    throw std::bad_alloc{};
}

void operator delete(void* memory) noexcept {
    std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept {
    std::free(memory);
}

using namespace ark::entity_sync;

TEST_CASE("Every nested publisher allocation failure preserves its state revision and source for retry") {
    bool completed = false;
    auto source = nested_test::changed();
    const auto structural = source.root.containers.at(201).at(1);
    source.root.containers.at(201).clear();
    source.root.containers.at(202).emplace(7, structural);
    const auto unchanged_source = source;
    for (std::size_t position = 1; position < 2048 && !completed; ++position)
    {
        nested_publisher publisher(nested_test::binding(), nested_test::initial());
        const auto before = publisher.baseline();
        allocation_count = 0;
        fail_at = position;
        armed = true;
        bool failed = false;
        try
        {
            const auto packet = publisher.update(source);
            completed = packet != nullptr;
        }
        catch (const std::bad_alloc&)
        { failed = true; }
        armed = false;
        if (failed)
        {
            CHECK(publisher.baseline() == before);
            CHECK(source == unchanged_source);
            const auto retry = publisher.update(source);
            REQUIRE(retry != nullptr);
            CHECK(retry->required_revision == 1);
            CHECK(retry->revision == 2);
            CHECK(publisher.baseline()->snapshot == source);
        }
    }
    CHECK(completed);
}

TEST_CASE("Every nested replica allocation failure preserves its committed state and permits exactly one retry") {
    bool completed = false;
    nested_publisher publisher(nested_test::binding(), nested_test::initial());
    auto source = nested_test::changed();
    const auto structural = source.root.containers.at(201).at(1);
    source.root.containers.at(201).clear();
    source.root.containers.at(202).emplace(7, structural);
    const auto packet = publisher.update(source);
    for (std::size_t position = 1; position < 2048 && !completed; ++position)
    {
        nested_replica replica(nested_test::binding());
        REQUIRE(replica.apply(nested_test::initial()) == nested_apply_result::applied);
        const auto before = replica.state();
        allocation_count = 0;
        fail_at = position;
        armed = true;
        bool failed = false;
        try
        { completed = replica.apply(*packet) == nested_apply_result::applied; }
        catch (const std::bad_alloc&)
        { failed = true; }
        armed = false;
        if (failed)
        {
            CHECK(replica.valid());
            CHECK(replica.state() == before);
            CHECK(replica.revision() == 1);
            CHECK(replica.apply(*packet) == nested_apply_result::applied);
            CHECK(replica.revision() == 2);
            CHECK(replica.apply(*packet) == nested_apply_result::duplicate);
            CHECK(*replica.state() == source);
        }
    }
    CHECK(completed);
}

TEST_CASE("Every first nested baseline allocation failure preserves an uninstalled receiver") {
    bool completed = false;
    const auto baseline = nested_test::initial();
    for (std::size_t position = 1; position < 2048 && !completed; ++position)
    {
        nested_replica replica(nested_test::binding());
        allocation_count = 0;
        fail_at = position;
        armed = true;
        bool failed = false;
        try
        { completed = replica.apply(baseline) == nested_apply_result::applied; }
        catch (const std::bad_alloc&)
        { failed = true; }
        armed = false;
        if (failed)
        {
            CHECK_FALSE(replica.valid());
            CHECK(replica.state() == nullptr);
            CHECK(replica.revision() == 0);
            CHECK(replica.apply(baseline) == nested_apply_result::applied);
            CHECK(*replica.state() == baseline.snapshot);
        }
    }
    CHECK(completed);
}

TEST_CASE("Every nested tombstone allocation failure leaves publisher active at the old revision") {
    bool completed = false;
    for (std::size_t position = 1; position < 2048 && !completed; ++position)
    {
        nested_publisher publisher(nested_test::binding(), nested_test::initial());
        const auto before = publisher.baseline();
        allocation_count = 0;
        fail_at = position;
        armed = true;
        bool failed = false;
        try
        { completed = publisher.remove() != nullptr; }
        catch (const std::bad_alloc&)
        { failed = true; }
        armed = false;
        if (failed)
        {
            CHECK(publisher.baseline() == before);
            const auto packet = publisher.remove();
            REQUIRE(packet != nullptr);
            CHECK(packet->required_revision == 1);
            CHECK(packet->revision == 2);
        }
    }
    CHECK(completed);
}

TEST_CASE("A nested string patch copy fails without publishing a partial variant and can retry") {
    const std::map<std::uint32_t, scalar_value> before{{7, std::string("old")}};
    const std::map<std::uint32_t, scalar_value> next{{7, std::string(256, 'x')}};
    const auto unchanged = next;
    const std::map<std::uint32_t, scalar_value> sentinel{{99, true}};
    bool completed = false;
    std::size_t failures{};
    for (std::size_t position = 1; position < 16 && !completed; ++position)
    {
        auto output = sentinel;
        nested_detail::patch_budget budget(nested_stream_limits{});
        allocation_count = 0;
        fail_at = position;
        armed = true;
        bool failed = false;
        try
        {
            nested_detail::patch_fields(before, next, output, budget);
            completed = true;
        }
        catch (const std::bad_alloc&)
        { failed = true; }
        armed = false;
        CHECK(next == unchanged);
        if (failed)
        {
            ++failures;
            CHECK(output == sentinel);
            nested_detail::patch_budget retry_budget(nested_stream_limits{});
            nested_detail::patch_fields(before, next, output, retry_budget);
        }
        CHECK(output.at(7) == next.at(7));
        CHECK(output.at(99) == scalar_value{true});
    }
    CHECK(completed);
    CHECK(failures >= 2); // Both the map node and the owned string can fail.
}

TEST_CASE("Every nested publisher construction allocation failure preserves the supplied owning baseline") {
    const auto trusted = nested_test::binding();
    auto baseline = nested_test::initial();
    baseline.snapshot.root.scalars.fields.at(2) = std::string(120, 's');
    baseline.snapshot.root.tables.at(200).rows.at(1).fields.at(2) = std::string(80, 'r');
    baseline.snapshot.root.containers.at(201).at(1).tables.at(200).rows.at(1).fields.at(2) = std::string(80, 'c');
    const auto unchanged = baseline.snapshot;
    bool completed = false;
    for (std::size_t position = 1; position < 2048 && !completed; ++position)
    {
        allocation_count = 0;
        fail_at = position;
        armed = true;
        bool failed = false;
        try
        {
            nested_publisher publisher(trusted, baseline);
            completed = publisher.baseline() != nullptr;
        }
        catch (const std::bad_alloc&)
        { failed = true; }
        armed = false;
        CHECK(baseline.snapshot == unchanged);
        CHECK(baseline.revision == 1);
        if (failed)
        {
            nested_publisher retry(trusted, baseline);
            CHECK(retry.baseline()->snapshot == unchanged);
            CHECK(retry.baseline()->revision == 1);
        }
    }
    CHECK(completed);
}
