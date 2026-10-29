// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <limits>
#include <memory>
#include <string>

#include "../network/doctest.h"

#include "nested_stream_fixture.hpp"

using namespace ark::entity_sync;
using nested_test::binding;
using nested_test::initial;

TEST_CASE("Nested baselines require independently trusted empty-table column types and fixed bindings") {
    auto baseline = initial();
    baseline.snapshot.root.tables.at(200).rows.clear();
    nested_publisher publisher(binding(), baseline);
    nested_replica replica(binding());
    CHECK(replica.apply(*publisher.baseline()) == nested_apply_result::applied);
    auto wrong = initial();
    SUBCASE("wrong column on first nonempty baseline") {
        wrong.snapshot.root.tables.at(200).rows.at(1).fields.at(1) = std::int32_t{5};
    }
    SUBCASE("wrong scalar on first baseline") {
        wrong.snapshot.root.scalars.fields.at(1) = std::uint32_t{7};
    }
    SUBCASE("wrong row class even when empty") {
        wrong.snapshot.root.tables.at(200) = {"Other", {}};
    }
    SUBCASE("hidden field") {
        wrong.snapshot.root.scalars.fields.emplace(99, true);
    }
    SUBCASE("root GUID") {
        wrong.snapshot.root.scalars.entity_id = 999;
    }
    SUBCASE("root incarnation") {
        ++wrong.snapshot.root.incarnation;
    }
    SUBCASE("schema") {
        wrong.snapshot.schema_id = "other/v1";
    }
    SUBCASE("generation") {
        ++wrong.generation;
    }
    SUBCASE("missing complete field") {
        wrong.snapshot.root.scalars.fields.erase(1);
    }
    nested_replica fresh(binding());
    CHECK(fresh.apply(wrong) == nested_apply_result::rejected);
    CHECK_FALSE(fresh.valid());
    CHECK(fresh.state() == nullptr);
    CHECK_THROWS_AS(nested_publisher(binding(), wrong), std::invalid_argument);
}

TEST_CASE("Nested trusted layouts reject contradictory class indices and missing bindings") {
    auto trusted = binding();
    SUBCASE("missing root") {
        trusted.root_id = 0;
    }
    SUBCASE("missing incarnation") {
        trusted.root_incarnation = 0;
    }
    SUBCASE("missing generation") {
        trusted.generation = 0;
    }
    SUBCASE("missing schema") {
        trusted.schema_id.clear();
    }
    SUBCASE("invalid type") {
        trusted.root.fields.at(1) = static_cast<nested_scalar_type>(100);
    }
    SUBCASE("conflicting column type for the same class") {
        trusted.root.containers.at(201).at(20).tables.at(200).columns.at(1) = nested_scalar_type::int32;
    }
    CHECK_THROWS_AS((nested_replica(trusted)), std::invalid_argument);
}

TEST_CASE("Nested publishers coalesce owning scalar row and child patches without modifying retained values") {
    nested_publisher publisher(binding(), initial());
    nested_replica replica(binding());
    const auto baseline = publisher.baseline();
    REQUIRE(replica.apply(*baseline) == nested_apply_result::applied);
    const auto retained = replica.state();
    const auto next = nested_test::changed();
    const auto packet = publisher.update(next);
    REQUIRE(packet != nullptr);
    CHECK(packet->required_revision == 1);
    CHECK(packet->revision == 2);
    CHECK(packet->root.fields.size() == 1);
    CHECK(packet->root.tables.at(200).size() == 2);
    CHECK_FALSE(packet->root.containers.at(201).replace);
    CHECK(packet->root.containers.at(201).changed.at(1).fields.size() == 1);
    REQUIRE(replica.apply(*packet) == nested_apply_result::applied);
    CHECK(*replica.state() == next);
    CHECK(*retained == baseline->snapshot);
    CHECK_FALSE(publisher.update(next));
    CHECK(publisher.baseline()->revision == 2);
    CHECK(replica.apply(*packet) == nested_apply_result::duplicate);
    CHECK(replica.apply(*baseline) == nested_apply_result::duplicate);
    nested_replica late(binding());
    REQUIRE(late.apply(*publisher.baseline()) == nested_apply_result::applied);
    CHECK(*late.state() == next);
}

TEST_CASE("Nested row reuse same values clear and stale incarnations preserve ordered lifetimes") {
    nested_publisher publisher(binding(), initial());
    nested_replica replica(binding());
    REQUIRE(replica.apply(*publisher.baseline()) == nested_apply_result::applied);
    auto next = initial().snapshot;
    auto& rows = next.root.tables.at(200).rows;
    rows.at(1).incarnation = 2;
    auto packet = publisher.update(next);
    REQUIRE(packet != nullptr);
    CHECK(packet->root.tables.at(200).at(1).required_incarnation == 1);
    CHECK(packet->root.tables.at(200).at(1).incarnation == 2);
    CHECK(packet->root.tables.at(200).at(1).fields == rows.at(1).fields);
    REQUIRE(replica.apply(*packet) == nested_apply_result::applied);
    rows.clear();
    packet = publisher.update(next);
    REQUIRE(replica.apply(*packet) == nested_apply_result::applied);
    CHECK(replica.state()->root.tables.at(200).rows.empty());
    rows.emplace(1, nested_row_snapshot{3, {{1, std::uint32_t{5}}, {2, std::string("owned-row")}}});
    packet = publisher.update(next);
    REQUIRE(replica.apply(*packet) == nested_apply_result::applied);
    CHECK(replica.state()->root.tables.at(200).rows.at(1).incarnation == 3);
    const auto retained = publisher.baseline();
    rows.clear();
    packet = publisher.update(next);
    REQUIRE(replica.apply(*packet) == nested_apply_result::applied);
    auto stale = *packet;
    stale.required_revision = packet->revision;
    stale.revision = packet->revision + 1;
    stale.root.tables.at(200).at(1) = {0, 2, retained->snapshot.root.tables.at(200).rows.at(1).fields};
    CHECK(replica.apply(stale) == nested_apply_result::rejected);
    CHECK(replica.state() == nullptr);
    rows.emplace(1, nested_row_snapshot{2, retained->snapshot.root.tables.at(200).rows.at(1).fields});
    CHECK_THROWS_AS(publisher.update(next), std::invalid_argument);
    CHECK(publisher.baseline()->revision == packet->revision);
}

TEST_CASE("Nested relationship-only moves detach destroy and reappearance commit one complete root") {
    nested_publisher publisher(binding(), initial());
    nested_replica replica(binding());
    REQUIRE(replica.apply(*publisher.baseline()) == nested_apply_result::applied);
    auto next = initial().snapshot;
    auto moving = next.root.containers.at(201).at(1);
    next.root.containers.at(201).clear();
    next.root.containers.at(202).emplace(7, moving);
    auto packet = publisher.update(next);
    REQUIRE(packet != nullptr);
    CHECK(packet->root.fields.empty());
    CHECK(packet->root.tables.empty());
    CHECK(packet->root.containers.size() == 2);
    CHECK(packet->root.containers.at(201).replace);
    REQUIRE(replica.apply(*packet) == nested_apply_result::applied);
    CHECK(*replica.state() == next);
    next.root.containers.at(202).clear();
    packet = publisher.update(next);
    REQUIRE(replica.apply(*packet) == nested_apply_result::applied);
    next.root.containers.at(201).emplace(3, moving);
    packet = publisher.update(next);
    REQUIRE(replica.apply(*packet) == nested_apply_result::applied);
    CHECK(replica.state()->root.containers.at(201).at(3).incarnation == moving.incarnation);
    next.root.containers.at(201).at(3).incarnation = moving.incarnation + 1;
    packet = publisher.update(next);
    REQUIRE(replica.apply(*packet) == nested_apply_result::applied);
    next.root.containers.at(201).clear();
    packet = publisher.update(next);
    REQUIRE(replica.apply(*packet) == nested_apply_result::applied);
    next.root.containers.at(201).emplace(3, moving);
    CHECK_THROWS_AS(publisher.update(next), std::invalid_argument);
    CHECK(replica.state()->root.containers.at(201).empty());
}

TEST_CASE("Nested malformed identities types operations gaps and fresh baselines invalidate without partial state") {
    nested_publisher publisher(binding(), initial());
    nested_replica replica(binding());
    REQUIRE(replica.apply(*publisher.baseline()) == nested_apply_result::applied);
    const auto retained = replica.state();
    auto packet = *publisher.update(nested_test::changed());
    SUBCASE("gap") {
        ++packet.required_revision;
        ++packet.revision;
    }
    SUBCASE("wrong schema") {
        packet.schema_id = "wrong";
    }
    SUBCASE("wrong generation") {
        ++packet.generation;
    }
    SUBCASE("wrong root") {
        ++packet.root.entity_id;
    }
    SUBCASE("wrong root incarnation") {
        ++packet.root.incarnation;
    }
    SUBCASE("wrong scalar type") {
        packet.root.fields.at(2) = true;
    }
    SUBCASE("wrong row type") {
        packet.root.tables.at(200).at(2).fields.at(1) = std::int32_t{12};
    }
    SUBCASE("wrong existing row identity") {
        packet.root.tables.at(200).at(1).required_incarnation = 99;
        packet.root.tables.at(200).at(1).incarnation = 99;
    }
    SUBCASE("hidden child") {
        packet.root.containers.at(201).changed.at(1).entity_id = 99;
    }
    SUBCASE("late invalid operation after valid scalar") {
        packet.root.containers.at(201).changed.at(1).incarnation = 99;
    }
    SUBCASE("malformed tombstone") {
        packet.removed = true;
    }
    CHECK(replica.apply(packet) == nested_apply_result::rejected);
    CHECK_FALSE(replica.valid());
    CHECK(replica.state() == nullptr);
    CHECK(*retained == initial().snapshot);
    CHECK(replica.apply(*publisher.baseline()) == nested_apply_result::rejected);
}

TEST_CASE("Nested root tombstones revision exhaustion and fresh-generation rejection cannot revive state") {
    nested_publisher publisher(binding(), initial());
    nested_replica replica(binding());
    const auto baseline = publisher.baseline();
    REQUIRE(replica.apply(*baseline) == nested_apply_result::applied);
    const auto removed = publisher.remove();
    REQUIRE(replica.apply(*removed) == nested_apply_result::applied);
    CHECK(replica.valid());
    CHECK(replica.state() == nullptr);
    CHECK(replica.apply(*baseline) == nested_apply_result::duplicate);
    CHECK(replica.apply(*removed) == nested_apply_result::duplicate);
    CHECK_THROWS_AS(publisher.update(initial().snapshot), std::logic_error);
    CHECK_THROWS_AS(publisher.baseline(), std::logic_error);
    auto exhausted = initial();
    exhausted.revision = std::numeric_limits<std::uint64_t>::max();
    nested_publisher end(binding(), exhausted);
    CHECK_FALSE(end.update(exhausted.snapshot));
    CHECK_THROWS_AS(end.update(nested_test::changed()), std::overflow_error);
    CHECK_THROWS_AS(end.remove(), std::overflow_error);
    CHECK(end.baseline()->revision == exhausted.revision);
    nested_replica current(binding());
    REQUIRE(current.apply(*baseline) == nested_apply_result::applied);
    auto reset = *baseline;
    ++reset.generation;
    CHECK(current.apply(reset) == nested_apply_result::rejected);
}

TEST_CASE("Nested limits cover complete reconstructed state patch work and hidden lifetime metadata") {
    auto next = nested_test::changed();
    nested_stream_limits limits;
    SUBCASE("reconstructed text budget") {
        limits.snapshot.max_string_bytes = 100;
    }
    SUBCASE("patch values") {
        limits.max_patch_values = 1;
    }
    SUBCASE("patch operations") {
        limits.max_patch_operations = 1;
    }
    SUBCASE("patch text") {
        limits.max_patch_string_bytes = 20;
    }
    SUBCASE("complete row budget") {
        limits.snapshot.max_rows = 2;
    }
    SUBCASE("lifetime metadata") {
        limits.max_lifetime_entries = 6;
    }
    nested_publisher publisher(binding(), initial(), limits);
    const auto old = publisher.baseline();
    CHECK_THROWS_AS(publisher.update(next), std::length_error);
    CHECK(publisher.baseline() == old);
    nested_publisher unrestricted(binding(), initial());
    const auto packet = unrestricted.update(next);
    nested_replica replica(binding(), limits);
    REQUIRE(replica.apply(initial()) == nested_apply_result::applied);
    const auto retained = replica.state();
    CHECK(replica.apply(*packet) == nested_apply_result::rejected);
    CHECK(replica.state() == nullptr);
    CHECK(*retained == initial().snapshot);
}

TEST_CASE("Nested retained packets validate after publisher and source lifetimes end") {
    std::shared_ptr<const nested_baseline> baseline;
    std::shared_ptr<const nested_delta> packet;
    {
        auto source = nested_test::changed();
        nested_publisher publisher(binding(), initial());
        baseline = publisher.baseline();
        packet = publisher.update(source);
        source.root.scalars.fields.clear();
    }
    nested_replica replica(binding());
    REQUIRE(replica.apply(*baseline) == nested_apply_result::applied);
    REQUIRE(replica.apply(*packet) == nested_apply_result::applied);
    CHECK(*replica.state() == nested_test::changed());
}

TEST_CASE("Nested recursive child changes and slot swaps keep one atomic authorized root") {
    auto trusted = binding();
    auto child_layout = nested_test::item_layout();
    child_layout.containers = {{201, {{30, nested_test::item_layout()}}}};
    trusted.root.containers.at(201).at(20) = child_layout;
    auto baseline = initial();
    auto& child = baseline.snapshot.root.containers.at(201).at(1);
    child.containers = {{201, {{7, nested_test::child(30, 3)}}}};
    nested_publisher publisher(trusted, baseline);
    nested_replica replica(trusted);
    REQUIRE(replica.apply(*publisher.baseline()) == nested_apply_result::applied);
    auto next = baseline.snapshot;
    auto& descendants = next.root.containers.at(201).at(1).containers.at(201);
    descendants.at(7).scalars.fields.at(1) = std::uint32_t{99};
    auto packet = publisher.update(next);
    REQUIRE(packet != nullptr);
    REQUIRE(replica.apply(*packet) == nested_apply_result::applied);
    CHECK(*replica.state() == next);
    auto descendant = descendants.at(7);
    descendants.clear();
    descendants.emplace(8, descendant);
    packet = publisher.update(next);
    REQUIRE(packet != nullptr);
    REQUIRE(replica.apply(*packet) == nested_apply_result::applied);
    CHECK(*replica.state() == next);
    auto malformed = *packet;
    ++malformed.required_revision;
    ++malformed.revision;
    auto& slots = malformed.root.containers.at(201).changed.at(1).containers.at(201).slots;
    slots.at(8).scalars.fields.emplace(99, std::string("hidden"));
    const auto retained = replica.state();
    CHECK(replica.apply(malformed) == nested_apply_result::rejected);
    CHECK(replica.state() == nullptr);
    CHECK(*retained == next);
}

TEST_CASE("Nested fixed recipient projections suppress hidden changes and refuse permission expansion") {
    auto trusted = binding();
    auto baseline = initial();
    trusted.root.fields.erase(2);
    baseline.snapshot.root.scalars.fields.erase(2);
    trusted.root.containers.at(201).clear();
    baseline.snapshot.root.containers.at(201).clear();
    nested_publisher restricted(trusted, baseline);
    nested_replica receiver(trusted);
    REQUIRE(receiver.apply(*restricted.baseline()) == nested_apply_result::applied);
    auto visible = baseline.snapshot;
    CHECK_FALSE(restricted.update(visible));
    auto unauthorized = visible;
    unauthorized.root.scalars.fields.emplace(2, std::string("not selected"));
    CHECK_THROWS_AS(restricted.update(unauthorized), std::invalid_argument);
    unauthorized = visible;
    unauthorized.root.containers.at(201).emplace(1, nested_test::child());
    CHECK_THROWS_AS(restricted.update(unauthorized), std::invalid_argument);
    const auto retained = restricted.baseline();
    nested_publisher full(binding(), initial());
    const auto private_change = full.update(nested_test::changed());
    REQUIRE(private_change != nullptr);
    CHECK(receiver.apply(*private_change) == nested_apply_result::rejected);
    CHECK(retained->snapshot == baseline.snapshot);
    CHECK(retained->revision == baseline.revision);
    CHECK(restricted.baseline()->revision == 1);
}

TEST_CASE("Nested schema and state reject repeated identities nonfinite values and incarnation transplant") {
    auto baseline = initial();
    SUBCASE("duplicate child GUID") {
        baseline.snapshot.root.containers.at(202).emplace(9, nested_test::child());
    }
    SUBCASE("duplicate row incarnation") {
        baseline.snapshot.root.tables.at(200).rows.emplace(2, baseline.snapshot.root.tables.at(200).rows.at(1));
    }
    SUBCASE("zero child slot") {
        auto child = baseline.snapshot.root.containers.at(201).at(1);
        baseline.snapshot.root.containers.at(201).clear();
        baseline.snapshot.root.containers.at(201).emplace(0, std::move(child));
    }
    SUBCASE("root GUID repeated as a child") {
        baseline.snapshot.root.containers.at(201).at(1).scalars.entity_id = baseline.snapshot.root.scalars.entity_id;
    }
    SUBCASE("nonfinite selected value") {
        auto trusted = binding();
        trusted.root.fields.at(1) = nested_scalar_type::real64;
        baseline.snapshot.root.scalars.fields.at(1) = std::numeric_limits<double>::infinity();
        nested_replica replica(trusted);
        CHECK(replica.apply(baseline) == nested_apply_result::rejected);
        return;
    }
    nested_replica replica(binding());
    CHECK(replica.apply(baseline) == nested_apply_result::rejected);
    CHECK_THROWS_AS(nested_publisher(binding(), baseline), std::invalid_argument);
}

TEST_CASE("Nested owning copies preserve every scalar alternative across baseline delta and source mutation") {
    auto baseline = initial();
    auto trusted = binding();
    baseline.snapshot.root.scalars.fields = {{1, true}, {2, std::int32_t{-2}}, {3, std::uint32_t{3}},
        {4, std::int64_t{-4}}, {5, std::uint64_t{5}}, {6, 1.25f}, {7, 2.5}, {8, std::string(256, 's')},
        {9, guid_value{99}}};
    trusted.root.fields.clear();
    for (const auto& [index, value] : baseline.snapshot.root.scalars.fields)
    {
        trusted.root.fields.emplace(index, static_cast<nested_scalar_type>(value.index()));
    }
    nested_publisher publisher(trusted, baseline);
    nested_replica replica(trusted);
    REQUIRE(replica.apply(*publisher.baseline()) == nested_apply_result::applied);
    CHECK(*replica.state() == baseline.snapshot);
    auto next = baseline.snapshot;
    next.root.scalars.fields.at(1) = false;
    next.root.scalars.fields.at(8) = std::string(300, 'n');
    const auto expected = next;
    const auto packet = publisher.update(next);
    REQUIRE(packet != nullptr);
    REQUIRE(replica.apply(*packet) == nested_apply_result::applied);
    for (const auto& [index, value] : expected.root.scalars.fields)
    {
        CHECK(replica.state()->root.scalars.fields.at(index).index() == value.index());
        CHECK(replica.state()->root.scalars.fields.at(index) == value);
    }
    std::get<std::string>(next.root.scalars.fields.at(8)).at(0) = 'x';
    std::get<std::string>(baseline.snapshot.root.scalars.fields.at(8)).at(0) = 'x';
    CHECK(publisher.baseline()->snapshot == expected);
    CHECK(*replica.state() == expected);
    CHECK(std::get<std::string>(packet->root.fields.at(8)) == std::string(300, 'n'));
}
