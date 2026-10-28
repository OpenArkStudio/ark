// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <cstdint>
#include <limits>
#include <string>

#include "../network/doctest.h"

#include "ark/kernel/entity_stream.hpp"

namespace {

using namespace ark::entity_sync;

scalar_baseline initial() {
    return {"v1", 7, 1,
        {17, "Player",
            {{1, std::uint32_t{100}}, {2, std::uint32_t{50}}, {3, std::string{"Alice"}},
                {4, std::numeric_limits<std::uint64_t>::max()}, {5, guid_value{42}}}}};
}

scalar_replica client(snapshot_limits limits = {}) {
    return {"v1", 17, "Player", limits};
}

} // namespace

TEST_CASE("Scalar baseline and deltas own values and converge after an explicit flush") {
    auto source = initial();
    scalar_publisher publisher(source);
    auto receiver = client();
    const auto baseline = publisher.baseline();
    source.snapshot.fields.at(3) = std::string{"Changed outside publisher"};
    CHECK(std::get<std::string>(baseline.snapshot.fields.at(3)) == "Alice");
    REQUIRE(receiver.apply(baseline) == scalar_apply_result::applied);
    const auto retained = receiver.state();
    auto next = baseline.snapshot;
    next.fields.at(1) = std::uint32_t{90};
    next.fields.at(1) = std::uint32_t{80};
    const auto delta = publisher.update(next);
    REQUIRE(delta != nullptr);
    CHECK(delta->fields.size() == 1);
    CHECK(delta->required_revision == 1);
    CHECK(delta->revision == 2);
    CHECK(std::get<std::uint32_t>(delta->fields.at(1)) == 80);
    CHECK(std::get<std::uint32_t>(baseline.snapshot.fields.at(1)) == 100);
    REQUIRE(receiver.apply(*delta) == scalar_apply_result::applied);
    REQUIRE(receiver.state() != nullptr);
    CHECK(*receiver.state() == next);
    CHECK(std::get<std::uint32_t>(retained->fields.at(1)) == 100);
    CHECK_FALSE(publisher.update(next));
    CHECK(publisher.baseline().revision == 2);
    CHECK(receiver.apply(*delta) == scalar_apply_result::duplicate);
    CHECK(receiver.apply(baseline) == scalar_apply_result::duplicate);
    CHECK(*receiver.state() == next);
}

TEST_CASE("Scalar receiver state can outlive the receiver and later updates") {
    std::shared_ptr<const scalar_snapshot> retained;
    std::shared_ptr<const scalar_delta> packet;
    {
        scalar_publisher publisher(initial());
        auto receiver = client();
        REQUIRE(receiver.apply(publisher.baseline()) == scalar_apply_result::applied);
        retained = receiver.state();
        auto next = publisher.baseline().snapshot;
        next.fields.at(3) = std::string{"Later name"};
        packet = publisher.update(next);
        REQUIRE(packet != nullptr);
        REQUIRE(receiver.apply(*packet) == scalar_apply_result::applied);
    }
    CHECK(std::get<std::string>(retained->fields.at(3)) == "Alice");
    CHECK(std::get<std::string>(packet->fields.at(3)) == "Later name");
}

TEST_CASE("Scalar frames preserve every supported value alternative and full integer widths") {
    auto base = initial();
    base.snapshot.fields = {{1, false}, {2, std::numeric_limits<std::int32_t>::min()},
        {3, std::numeric_limits<std::uint32_t>::max()}, {4, std::numeric_limits<std::int64_t>::min()},
        {5, std::numeric_limits<std::uint64_t>::max()}, {6, std::numeric_limits<float>::max()},
        {7, std::numeric_limits<double>::max()}, {8, std::string{"a\0b", 3}},
        {9, guid_value{std::numeric_limits<ark::guid_t>::max()}}};
    scalar_publisher publisher(base);
    auto receiver = client();
    REQUIRE(receiver.apply(publisher.baseline()) == scalar_apply_result::applied);
    CHECK(*receiver.state() == base.snapshot);
    auto next = base.snapshot;
    next.fields.at(1) = true;
    next.fields.at(2) = std::numeric_limits<std::int32_t>::max();
    next.fields.at(3) = std::uint32_t{0};
    next.fields.at(4) = std::numeric_limits<std::int64_t>::max();
    next.fields.at(5) = std::uint64_t{0};
    next.fields.at(6) = -std::numeric_limits<float>::max();
    next.fields.at(7) = -std::numeric_limits<double>::max();
    next.fields.at(8) = std::string{"new\0value", 9};
    next.fields.at(9) = guid_value{std::numeric_limits<ark::guid_t>::min()};
    const auto delta = publisher.update(next);
    REQUIRE(delta);
    CHECK(delta->fields.size() == 9);
    REQUIRE(receiver.apply(*delta) == scalar_apply_result::applied);
    CHECK(*receiver.state() == next);
}

TEST_CASE("Scalar gaps invalidate state and require a newer generation baseline") {
    scalar_publisher publisher(initial());
    auto receiver = client();
    REQUIRE(receiver.apply(publisher.baseline()) == scalar_apply_result::applied);
    auto next = publisher.baseline().snapshot;
    next.fields.at(1) = std::uint32_t{90};
    const auto missed = publisher.update(next);
    REQUIRE(missed);
    next.fields.at(2) = std::uint32_t{40};
    const auto later = publisher.update(next);
    REQUIRE(later);
    CHECK(receiver.apply(*later) == scalar_apply_result::rejected);
    CHECK_FALSE(receiver.valid());
    CHECK(receiver.state() == nullptr);
    CHECK(receiver.apply(*missed) == scalar_apply_result::rejected);
    auto reset = publisher.baseline();
    CHECK(receiver.apply(reset) == scalar_apply_result::rejected);
    ++reset.generation;
    REQUIRE(receiver.apply(reset) == scalar_apply_result::applied);
    CHECK(*receiver.state() == next);
    CHECK(receiver.apply(*later) == scalar_apply_result::rejected);
    CHECK(receiver.state() == nullptr);
}

TEST_CASE("Scalar deletion cannot be undone by duplicate updates or baselines") {
    scalar_publisher publisher(initial());
    const auto baseline = publisher.baseline();
    auto receiver = client();
    REQUIRE(receiver.apply(baseline) == scalar_apply_result::applied);
    const auto removed = publisher.remove();
    REQUIRE(receiver.apply(*removed) == scalar_apply_result::applied);
    CHECK(receiver.valid());
    CHECK(receiver.state() == nullptr);
    CHECK(receiver.apply(*removed) == scalar_apply_result::duplicate);
    CHECK(receiver.apply(baseline) == scalar_apply_result::duplicate);
    CHECK(receiver.state() == nullptr);
    CHECK_THROWS_AS(publisher.baseline(), std::logic_error);
    CHECK_THROWS_AS(publisher.update(baseline.snapshot), std::logic_error);
    CHECK_THROWS_AS(publisher.remove(), std::logic_error);
    auto revived = baseline;
    ++revived.generation;
    REQUIRE(receiver.apply(revived) == scalar_apply_result::applied);
    CHECK(*receiver.state() == baseline.snapshot);
}

TEST_CASE("Scalar publisher rejects identity projection and type changes without advancing") {
    const auto base = initial();
    scalar_publisher publisher(base);
    auto invalid = base.snapshot;
    SUBCASE("identity") {
        invalid.entity_id = 18;
    }
    SUBCASE("class") {
        invalid.class_name = "Other";
    }
    SUBCASE("missing field") {
        invalid.fields.erase(1);
    }
    SUBCASE("replaced field") {
        invalid.fields.erase(5);
        invalid.fields.emplace(99, std::uint32_t{1});
    }
    SUBCASE("changed type") {
        invalid.fields.at(1) = std::int32_t{100};
    }
    SUBCASE("non finite") {
        invalid.fields.at(1) = std::numeric_limits<float>::infinity();
    }
    CHECK_THROWS_AS(publisher.update(invalid), std::invalid_argument);
    CHECK(publisher.baseline().snapshot == base.snapshot);
    CHECK(publisher.baseline().revision == base.revision);
}

TEST_CASE("Scalar receiver rejects malformed patches and exposes no partially applied state") {
    const auto base = initial();
    scalar_publisher publisher(base);
    auto next = base.snapshot;
    next.fields.at(1) = std::uint32_t{1};
    const auto valid = publisher.update(next);
    REQUIRE(valid);
    auto invalid = *valid;
    SUBCASE("unknown field after valid field") {
        invalid.fields.emplace(99, std::uint32_t{1});
    }
    SUBCASE("wrong type") {
        invalid.fields.at(1) = std::int32_t{1};
    }
    SUBCASE("schema") {
        invalid.schema_id = "wrong";
    }
    SUBCASE("entity") {
        invalid.entity_id = 18;
    }
    SUBCASE("generation") {
        ++invalid.generation;
    }
    SUBCASE("counter") {
        invalid.revision += 1;
    }
    SUBCASE("empty update") {
        invalid.fields.clear();
    }
    SUBCASE("removal with data") {
        invalid.removed = true;
    }
    auto receiver = client();
    REQUIRE(receiver.apply(base) == scalar_apply_result::applied);
    CHECK(receiver.apply(invalid) == scalar_apply_result::rejected);
    CHECK_FALSE(receiver.valid());
    CHECK(receiver.state() == nullptr);
    CHECK(receiver.revision() == base.revision);
}

TEST_CASE("Scalar reconstructed state obeys the total string budget before copying") {
    scalar_baseline base{"v1", 1, 1, {17, "Player", {{1, std::string{"12345"}}, {2, std::string{"x"}}}}};
    const snapshot_limits limits{2, 12};
    auto receiver = client(limits);
    REQUIRE(receiver.apply(base) == scalar_apply_result::applied);
    const scalar_delta delta{"v1", 1, 1, 2, 17, "Player", {{2, std::string{"abcde"}}}, false};
    CHECK(receiver.apply(delta) == scalar_apply_result::rejected);
    CHECK(receiver.state() == nullptr);
    scalar_publisher publisher(base, limits);
    auto next = base.snapshot;
    next.fields.at(2) = std::string{"abcde"};
    CHECK_THROWS_AS(publisher.update(next), std::length_error);
    CHECK(publisher.baseline().snapshot == base.snapshot);
}

TEST_CASE("Scalar baseline validation and new generations replace the full visible selection") {
    const auto base = initial();
    auto receiver = client();
    auto invalid = base;
    SUBCASE("schema") {
        invalid.schema_id.clear();
    }
    SUBCASE("zero generation") {
        invalid.generation = 0;
    }
    SUBCASE("zero revision") {
        invalid.revision = 0;
    }
    SUBCASE("zero field") {
        invalid.snapshot.fields.emplace(0, false);
    }
    SUBCASE("wrong entity") {
        invalid.snapshot.entity_id = 19;
    }
    SUBCASE("wrong class") {
        invalid.snapshot.class_name = "Other";
    }
    CHECK(receiver.apply(invalid) == scalar_apply_result::rejected);
    REQUIRE(receiver.apply(base) == scalar_apply_result::applied);
    auto selected = base;
    ++selected.generation;
    selected.snapshot.fields.erase(3);
    REQUIRE(receiver.apply(selected) == scalar_apply_result::applied);
    CHECK(receiver.state()->fields.count(3) == 0);
    CHECK(receiver.apply(base) == scalar_apply_result::rejected);
}

TEST_CASE("Scalar revision exhaustion is explicit and unchanged state remains readable") {
    auto base = initial();
    base.revision = std::numeric_limits<std::uint64_t>::max();
    scalar_publisher publisher(base);
    CHECK_FALSE(publisher.update(base.snapshot));
    auto next = base.snapshot;
    next.fields.at(1) = std::uint32_t{1};
    CHECK_THROWS_AS(publisher.update(next), std::overflow_error);
    CHECK_THROWS_AS(publisher.remove(), std::overflow_error);
    CHECK(publisher.baseline().snapshot == base.snapshot);
    CHECK(publisher.baseline().revision == base.revision);
}
