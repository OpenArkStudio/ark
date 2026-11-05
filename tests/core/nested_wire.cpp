// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <limits>
#include <string>

#include "../network/doctest.h"

#include "ark/kernel/nested_wire.hpp"
#include "nested_stream_fixture.hpp"

namespace {
using namespace ark::entity_sync;
namespace wire = nested_wire;
namespace proto = ark::protobuf::entity_sync;

proto::nested_baseline encoded_initial() {
    proto::nested_baseline output;
    wire::encode(nested_test::initial(), output, nested_test::binding());
    return output;
}

proto::nested_delta encoded_change() {
    nested_publisher publisher(nested_test::binding(), nested_test::initial());
    proto::nested_delta output;
    wire::encode(*publisher.update(nested_test::changed()), output, nested_test::binding());
    return output;
}
} // namespace

TEST_CASE("Nested wire preserves every scalar width zero presence and binary bytes") {
    auto baseline = nested_test::initial();
    auto trusted = nested_test::binding();
    baseline.snapshot.root.scalars.fields = {{1, false}, {2, std::int32_t{0}}, {3, std::uint32_t{0}},
        {4, std::numeric_limits<std::int64_t>::min()}, {5, std::numeric_limits<std::uint64_t>::max()}, {6, 0.0f},
        {7, 0.0}, {8, std::string("\xff\0\x80", 3)}, {9, guid_value{-1}}};
    trusted.root.fields = {{1, nested_scalar_type::boolean}, {2, nested_scalar_type::int32},
        {3, nested_scalar_type::uint32}, {4, nested_scalar_type::int64}, {5, nested_scalar_type::uint64},
        {6, nested_scalar_type::real32}, {7, nested_scalar_type::real64}, {8, nested_scalar_type::bytes},
        {9, nested_scalar_type::guid}};
    proto::nested_baseline message;
    wire::encode(baseline, message, trusted);
    REQUIRE(message.snapshot().root().fields_size() == 9);
    CHECK(message.snapshot().root().fields(0).value_case() == proto::scalar_field::kBoolValue);
    CHECK(message.snapshot().root().fields(1).value_case() == proto::scalar_field::kInt32Value);
    CHECK(message.snapshot().root().fields(2).value_case() == proto::scalar_field::kUint32Value);
    CHECK(message.snapshot().root().fields(5).value_case() == proto::scalar_field::kFloatValue);
    CHECK(message.snapshot().root().fields(6).value_case() == proto::scalar_field::kDoubleValue);
    CHECK(wire::decode(message, trusted).snapshot == baseline.snapshot);
    const auto bytes = wire::encode(baseline, trusted);
    CHECK(wire::decode_baseline(bytes, trusted).snapshot == baseline.snapshot);
    nested_publisher publisher(trusted, baseline);
    auto next = baseline.snapshot;
    next.root.scalars.fields.at(1) = true;
    next.root.scalars.fields.at(8) = std::string("\0\xfe", 2);
    nested_replica receiver(trusted);
    REQUIRE(receiver.apply(wire::decode_baseline(bytes, trusted)) == nested_apply_result::applied);
    const auto delta = wire::decode_delta(wire::encode(*publisher.update(next), trusted), trusted);
    REQUIRE(receiver.apply(delta) == nested_apply_result::applied);
    CHECK(*receiver.state() == next);
    CHECK(receiver.apply(delta) == nested_apply_result::duplicate);
}

TEST_CASE("Nested wire row lifetimes relationship replacements and tombstones compose with the replica") {
    const auto trusted = nested_test::binding();
    nested_publisher publisher(trusted, nested_test::initial());
    nested_replica receiver(trusted);
    REQUIRE(receiver.apply(wire::decode_baseline(wire::encode(*publisher.baseline(), trusted), trusted)) ==
            nested_apply_result::applied);
    auto next = nested_test::changed();
    auto packet = wire::decode_delta(wire::encode(*publisher.update(next), trusted), trusted);
    REQUIRE(receiver.apply(packet) == nested_apply_result::applied);
    CHECK(*receiver.state() == next);
    next.root.tables.at(200).rows.at(1).incarnation = 3;
    packet = wire::decode_delta(wire::encode(*publisher.update(next), trusted), trusted);
    CHECK(packet.root.tables.at(200).at(1).required_incarnation == 1);
    CHECK(packet.root.tables.at(200).at(1).incarnation == 3);
    CHECK(packet.root.tables.at(200).at(1).fields.size() == 2);
    REQUIRE(receiver.apply(packet) == nested_apply_result::applied);
    next.root.tables.at(200).rows.clear();
    packet = wire::decode_delta(wire::encode(*publisher.update(next), trusted), trusted);
    CHECK(packet.root.tables.at(200).at(1).incarnation == 0);
    REQUIRE(receiver.apply(packet) == nested_apply_result::applied);
    const auto child = next.root.containers.at(201).at(1);
    next.root.containers.at(201).clear();
    next.root.containers.at(202).emplace(7, child);
    proto::nested_delta message;
    wire::encode(*publisher.update(next), message, trusted);
    REQUIRE(message.root().containers_size() == 2);
    CHECK(message.root().containers(0).has_replacement());
    CHECK(message.root().containers(0).replacement().slots_size() == 0);
    CHECK(message.root().containers(1).has_replacement());
    REQUIRE(receiver.apply(wire::decode(message, trusted)) == nested_apply_result::applied);
    CHECK(*receiver.state() == next);
    const auto removed = wire::decode_delta(wire::encode(*publisher.remove(), trusted), trusted);
    REQUIRE(receiver.apply(removed) == nested_apply_result::applied);
    CHECK(receiver.valid());
    CHECK(receiver.state() == nullptr);
    CHECK(receiver.apply(removed) == nested_apply_result::duplicate);
}

TEST_CASE("Nested wire rejects duplicate baseline indices without collapsing records") {
    auto message = encoded_initial();
    auto& root = *message.mutable_snapshot()->mutable_root();
    SUBCASE("scalar") {
        *root.add_fields() = root.fields(0);
    }
    SUBCASE("table") {
        *root.add_tables() = root.tables(0);
    }
    SUBCASE("row") {
        auto& table = *root.mutable_tables(0);
        *table.add_rows() = table.rows(0);
    }
    SUBCASE("container") {
        *root.add_containers() = root.containers(0);
    }
    SUBCASE("slot") {
        auto& container = *root.mutable_containers(0);
        *container.add_slots() = container.slots(0);
    }
    CHECK_THROWS_AS(wire::decode(message, nested_test::binding()), std::invalid_argument);
    CHECK_THROWS_AS(wire::decode_baseline(message.SerializeAsString(), nested_test::binding()), std::invalid_argument);
}

TEST_CASE("Nested wire rejects duplicate delta indices and missing container alternatives") {
    auto message = encoded_change();
    auto& root = *message.mutable_root();
    SUBCASE("scalar") {
        *root.add_fields() = root.fields(0);
    }
    SUBCASE("table") {
        *root.add_tables() = root.tables(0);
    }
    SUBCASE("row") {
        auto& table = *root.mutable_tables(0);
        *table.add_rows() = table.rows(0);
    }
    SUBCASE("container") {
        *root.add_containers() = root.containers(0);
    }
    SUBCASE("changed slot") {
        auto& patch = *root.mutable_containers(0)->mutable_patch();
        *patch.add_changed() = patch.changed(0);
    }
    SUBCASE("replacement slot") {
        auto* slots = root.mutable_containers(0)->mutable_replacement();
        auto* first = slots->add_slots();
        first->set_slot(1);
        *first->mutable_state() = encoded_initial().snapshot().root().containers(0).slots(0).state();
        *slots->add_slots() = slots->slots(0);
    }
    SUBCASE("missing alternative") {
        root.mutable_containers(0)->clear_update();
    }
    CHECK_THROWS_AS(wire::decode(message, nested_test::binding()), std::invalid_argument);
}

TEST_CASE("Nested wire rejects malformed envelopes values and independently untrusted schemas") {
    auto message = encoded_initial();
    auto& root = *message.mutable_snapshot()->mutable_root();
    SUBCASE("missing scalar alternative") {
        root.mutable_fields(0)->clear_value();
    }
    SUBCASE("wrong scalar type") {
        root.mutable_fields(0)->set_uint32_value(0);
    }
    SUBCASE("nonfinite") {
        root.mutable_fields(0)->set_double_value(std::numeric_limits<double>::infinity());
    }
    SUBCASE("zero index") {
        root.mutable_fields(0)->set_index(0);
    }
    SUBCASE("zero identity") {
        root.set_entity_id(0);
    }
    SUBCASE("zero slot") {
        root.mutable_containers(0)->mutable_slots(0)->set_slot(0);
    }
    SUBCASE("missing child state") {
        root.mutable_containers(0)->mutable_slots(0)->clear_state();
    }
    SUBCASE("missing root") {
        message.mutable_snapshot()->clear_root();
    }
    SUBCASE("missing snapshot") {
        message.clear_snapshot();
    }
    SUBCASE("schema mismatch") {
        message.mutable_snapshot()->set_schema_id("other");
    }
    SUBCASE("wrong class on empty table") {
        root.mutable_tables(0)->clear_rows();
        root.mutable_tables(0)->set_class_name("Other");
    }
    CHECK_THROWS_AS(wire::decode(message, nested_test::binding()), std::invalid_argument);
    auto trusted = nested_test::binding();
    trusted.root.tables.at(200).columns.at(1) = nested_scalar_type::int32;
    CHECK_THROWS_AS(wire::decode(encoded_initial(), trusted), std::invalid_argument);
}

TEST_CASE("Nested wire retains unknown-field compatibility within byte and recursion budgets") {
    auto message = encoded_initial();
    message.GetReflection()->MutableUnknownFields(&message)->AddVarint(999, 1);
    CHECK(wire::decode(message, nested_test::binding()).snapshot == nested_test::initial().snapshot);
    auto* group = message.GetReflection()->MutableUnknownFields(&message)->AddGroup(998);
    for (int depth = 0; depth < 10; ++depth)
    {
        group = group->AddGroup(998);
    }
    wire::limits budget;
    budget.max_recursion_depth = 8;
    CHECK_THROWS_AS(wire::decode(message, nested_test::binding(), budget), std::length_error);
}

TEST_CASE("Nested wire validates before replacing caller output and bounds bytes and Protobuf nesting") {
    const auto baseline = nested_test::initial();
    const auto trusted = nested_test::binding();
    const auto original = encoded_initial();
    proto::nested_baseline output = original;
    auto invalid = baseline;
    invalid.snapshot.root.scalars.fields.at(1) = true;
    CHECK_THROWS_AS(wire::encode(invalid, output, trusted), std::invalid_argument);
    CHECK(output.SerializeAsString() == original.SerializeAsString());
    auto budget = wire::limits{};
    budget.max_message_bytes = original.ByteSizeLong() - 1;
    CHECK_THROWS_AS(wire::encode(baseline, output, trusted, budget), std::length_error);
    CHECK(output.SerializeAsString() == original.SerializeAsString());
    CHECK_THROWS_AS(wire::decode(original, trusted, budget), std::length_error);
    CHECK_THROWS_AS(wire::decode_baseline(original.SerializeAsString(), trusted, budget), std::length_error);
    budget.max_message_bytes = original.ByteSizeLong();
    CHECK(wire::decode(original, trusted, budget).snapshot == baseline.snapshot);
    budget.max_recursion_depth = 1;
    CHECK_THROWS_AS(wire::encode(baseline, output, trusted, budget), std::length_error);
    CHECK(output.SerializeAsString() == original.SerializeAsString());
    CHECK_THROWS_AS(wire::decode(original, trusted, budget), std::length_error);
    CHECK_THROWS_AS(wire::decode_baseline(original.SerializeAsString(), trusted, budget), std::invalid_argument);
    CHECK_THROWS_AS(wire::decode_baseline(std::string("\xff", 1), trusted), std::invalid_argument);
}

TEST_CASE("Nested wire applies complete state patch and lifetime budgets separately") {
    const auto trusted = nested_test::binding();
    const auto initial = encoded_initial();
    const auto change = encoded_change();
    wire::limits budget;
    SUBCASE("complete rows") {
        budget.stream.snapshot.max_rows = 1;
        CHECK_THROWS_AS(wire::decode(initial, trusted, budget), std::length_error);
    }
    SUBCASE("complete fields") {
        budget.stream.snapshot.max_fields = 1;
        CHECK_THROWS_AS(wire::decode(initial, trusted, budget), std::length_error);
    }
    SUBCASE("complete text") {
        budget.stream.snapshot.max_string_bytes = 1;
        CHECK_THROWS_AS(wire::decode(initial, trusted, budget), std::length_error);
    }
    SUBCASE("lifetime history") {
        budget.stream.max_lifetime_entries = 1;
        CHECK_THROWS_AS(wire::decode(initial, trusted, budget), std::length_error);
    }
    SUBCASE("patch operations") {
        budget.stream.max_patch_operations = 1;
        CHECK_THROWS_AS(wire::decode(change, trusted, budget), std::length_error);
    }
    SUBCASE("patch values") {
        budget.stream.max_patch_values = 1;
        CHECK_THROWS_AS(wire::decode(change, trusted, budget), std::length_error);
    }
    SUBCASE("patch text") {
        budget.stream.max_patch_string_bytes = 1;
        CHECK_THROWS_AS(wire::decode(change, trusted, budget), std::length_error);
    }
}

TEST_CASE("Nested wire stages publications on the destination Protobuf arena") {
    google::protobuf::Arena arena;
    auto* output = google::protobuf::Arena::Create<proto::nested_baseline>(&arena);
    output->set_revision(99);
    const auto before = output->SerializeAsString();
    auto invalid = nested_test::initial();
    invalid.revision = 0;
    CHECK_THROWS_AS(wire::encode(invalid, *output, nested_test::binding()), std::invalid_argument);
    CHECK(output->SerializeAsString() == before);
    wire::encode(nested_test::initial(), *output, nested_test::binding());
    CHECK(wire::decode(*output, nested_test::binding()).snapshot == nested_test::initial().snapshot);
    CHECK(output->GetArena() == &arena);
}

TEST_CASE("Decoded nested packets retain atomic rejection and terminal recovery semantics") {
    const auto trusted = nested_test::binding();
    nested_replica receiver(trusted);
    REQUIRE(receiver.apply(wire::decode(encoded_initial(), trusted)) == nested_apply_result::applied);
    const auto retained = receiver.state();
    auto message = encoded_change();
    SUBCASE("revision gap") {
        message.set_required_revision(2);
        message.set_revision(3);
    }
    SUBCASE("late stale child incarnation") {
        message.mutable_root()
            ->mutable_containers(0)
            ->mutable_patch()
            ->mutable_changed(0)
            ->mutable_change()
            ->set_incarnation(99);
    }
    SUBCASE("stale required row incarnation") {
        auto* row = message.mutable_root()->mutable_tables(0)->mutable_rows(0);
        row->set_required_incarnation(99);
        row->set_incarnation(99);
    }
    const auto decoded = wire::decode(message, trusted);
    CHECK(receiver.apply(decoded) == nested_apply_result::rejected);
    CHECK(receiver.state() == nullptr);
    CHECK_FALSE(receiver.valid());
    CHECK(*retained == nested_test::initial().snapshot);
    CHECK(receiver.apply(wire::decode(encoded_initial(), trusted)) == nested_apply_result::rejected);
}

TEST_CASE("Nested wire GUIDs preserve signed identity and reject native narrowing") {
    auto trusted = nested_test::binding();
    auto baseline = nested_test::initial();
    trusted.root_id = -10;
    baseline.snapshot.root.scalars.entity_id = -10;
    CHECK(wire::decode_baseline(wire::encode(baseline, trusted), trusted).snapshot == baseline.snapshot);
#if defined(ARK_USE_GUID_32)
    auto message = encoded_initial();
    message.mutable_snapshot()->mutable_root()->set_entity_id(std::numeric_limits<std::int64_t>::max());
    CHECK_THROWS_AS(wire::decode(message, nested_test::binding()), std::invalid_argument);
#endif
}
