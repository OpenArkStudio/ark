// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>

#include "../network/doctest.h"

#include "ark/kernel/data_list_impl.hpp"
#include "ark/kernel/entity_impl.hpp"
#include "ark/kernel/entity_snapshot.hpp"
#include "ark/kernel/entity_stream.hpp"
#include "ark/kernel/kernel_module_impl.hpp"

namespace {

using ark::entity_sync::guid_value;
using ark::entity_sync::scalar_snapshot;
using ark::entity_sync::snapshot_limits;

scalar_snapshot capture_scalar_snapshot(
    const ark::entity& source, std::span<const std::uint32_t> selection, const snapshot_limits& limits = {}) {
    ark::kernel_module_impl implementation;
    ark::kernel_module* kernel = &implementation;
    return kernel->capture_scalar_snapshot(source, selection, limits);
}

constexpr std::array<std::uint32_t, 9> all_fields{1, 2, 3, 4, 5, 6, 7, 8, 9};

class snapshot_entity
{
public:
    explicit snapshot_entity(std::string class_name = "SnapshotOwner")
        : schema(std::make_shared<ark::class_meta>(class_name)) {
        const std::array types{ark::data_type::dt_boolean, ark::data_type::dt_int32, ark::data_type::dt_uint32,
            ark::data_type::dt_int64, ark::data_type::dt_uint64, ark::data_type::dt_float, ark::data_type::dt_double,
            ark::data_type::dt_string, ark::data_type::dt_guid};
        for (std::uint32_t index = 1; index <= types.size(); ++index)
        {
            add_field(index, types[index - 1]);
        }
        source = std::make_shared<ark::entity_impl>(
            schema, std::numeric_limits<ark::guid_t>::max(), 0, 1, 0, ark::data_list_impl{});
    }

    void add_field(std::uint32_t index, ark::data_type type) {
        const auto field = schema->create_data_meta("field_" + std::to_string(index), index);
        REQUIRE(field != nullptr);
        field->set_type(type);
    }

    ark::protobuf::protobuf_entity_data materialized() const {
        ark::kernel_module_impl serializer;
        ark::protobuf::protobuf_entity_data result;
        REQUIRE(serializer.entity_node_to_protobuf_data(source, &result));
        return result;
    }

    std::shared_ptr<ark::class_meta> schema;
    std::shared_ptr<ark::entity_impl> source;
};

} // namespace

TEST_CASE("real Entity captures drive an owning scalar baseline delta and reconnect loop") {
    using namespace ark::entity_sync;
    snapshot_entity fixture;
    auto& source = *fixture.source;
    REQUIRE(source.set_u_int32(3, 100));
    REQUIRE(source.set_u_int64(5, 50));
    REQUIRE(source.set_string(8, "Alice"));
    REQUIRE(source.set_bool(1, true));
    const std::array<std::uint32_t, 3> visible{3, 5, 8};
    const auto initial = capture_scalar_snapshot(source, visible);
    CHECK(initial.fields.count(1) == 0);
    scalar_publisher publisher({"player/v1", 1, 1, initial});
    scalar_replica peer("player/v1", initial.entity_id, initial.class_name);
    REQUIRE(peer.apply(publisher.baseline()) == scalar_apply_result::applied);
    const auto retained = peer.state();
    REQUIRE(source.set_u_int32(3, 90));
    REQUIRE(source.set_u_int32(3, 80));
    REQUIRE(source.set_u_int64(5, 30));
    REQUIRE(source.set_string(8, "Bob"));
    const auto latest = capture_scalar_snapshot(source, visible);
    const auto delta = publisher.update(latest);
    REQUIRE(delta != nullptr);
    CHECK(delta->fields.size() == 3);
    REQUIRE(peer.apply(*delta) == scalar_apply_result::applied);
    CHECK(*peer.state() == latest);
    CHECK_FALSE(publisher.update(capture_scalar_snapshot(source, visible)));
    fixture.source.reset();
    CHECK(std::get<std::uint32_t>(retained->fields.at(3)) == 100);
    CHECK(std::get<std::uint64_t>(peer.state()->fields.at(5)) == 30);
    CHECK(std::get<std::string>(retained->fields.at(8)) == "Alice");
    const auto removed = publisher.remove();
    REQUIRE(peer.apply(*removed) == scalar_apply_result::applied);
    CHECK(peer.state() == nullptr);
    scalar_publisher reconnected({"player/v1", 2, 1, latest});
    REQUIRE(peer.apply(reconnected.baseline()) == scalar_apply_result::applied);
    CHECK(*peer.state() == latest);
}

TEST_CASE("scalar snapshots preserve native types boundaries and independent ownership") {
    snapshot_entity fixture;
    auto& source = *fixture.source;
    REQUIRE(source.set_bool(1, true));
    REQUIRE(source.set_int32(2, std::numeric_limits<std::int32_t>::min()));
    REQUIRE(source.set_u_int32(3, std::numeric_limits<std::uint32_t>::max()));
    REQUIRE(source.set_int64(4, std::numeric_limits<std::int64_t>::min()));
    REQUIRE(source.set_u_int64(5, std::numeric_limits<std::uint64_t>::max()));
    REQUIRE(source.set_float(6, std::numeric_limits<float>::lowest()));
    REQUIRE(source.set_double(7, std::numeric_limits<double>::max()));
    const std::string binary("a\0b\xc3\xa9", 5);
    REQUIRE(source.set_string(8, binary));
    REQUIRE(source.set_guid(9, std::numeric_limits<ark::guid_t>::min()));
    const auto snapshot = capture_scalar_snapshot(source, all_fields);
    CHECK(snapshot.entity_id == std::numeric_limits<ark::guid_t>::max());
    CHECK(snapshot.class_name == "SnapshotOwner");
    REQUIRE(snapshot.fields.size() == 9);
    CHECK(std::get<bool>(snapshot.fields.at(1)));
    CHECK(std::get<std::int32_t>(snapshot.fields.at(2)) == std::numeric_limits<std::int32_t>::min());
    CHECK(std::get<std::uint32_t>(snapshot.fields.at(3)) == std::numeric_limits<std::uint32_t>::max());
    CHECK(std::get<std::int64_t>(snapshot.fields.at(4)) == std::numeric_limits<std::int64_t>::min());
    CHECK(std::get<std::uint64_t>(snapshot.fields.at(5)) == std::numeric_limits<std::uint64_t>::max());
    CHECK(std::get<float>(snapshot.fields.at(6)) == std::numeric_limits<float>::lowest());
    CHECK(std::get<double>(snapshot.fields.at(7)) == std::numeric_limits<double>::max());
    CHECK(std::get<std::string>(snapshot.fields.at(8)) == binary);
    CHECK(std::get<guid_value>(snapshot.fields.at(9)).value == std::numeric_limits<ark::guid_t>::min());

    REQUIRE(source.set_int32(2, std::numeric_limits<std::int32_t>::max()));
    REQUIRE(source.set_int64(4, std::numeric_limits<std::int64_t>::max()));
    REQUIRE(source.set_u_int64("field_5", 42));
    REQUIRE(source.set_guid("field_9", std::numeric_limits<ark::guid_t>::max()));
    REQUIRE(source.set_string(8, "changed"));
    REQUIRE(source.set_float(6, std::numeric_limits<float>::max()));
    REQUIRE(source.set_double(7, std::numeric_limits<double>::lowest()));
    const auto changed = capture_scalar_snapshot(source, all_fields);
    CHECK(std::get<std::int32_t>(changed.fields.at(2)) == std::numeric_limits<std::int32_t>::max());
    CHECK(std::get<std::int64_t>(changed.fields.at(4)) == std::numeric_limits<std::int64_t>::max());
    CHECK(std::get<std::uint64_t>(changed.fields.at(5)) == 42);
    CHECK(std::get<std::uint64_t>(snapshot.fields.at(5)) == std::numeric_limits<std::uint64_t>::max());
    CHECK(std::get<guid_value>(changed.fields.at(9)).value == std::numeric_limits<ark::guid_t>::max());
    CHECK(std::get<guid_value>(snapshot.fields.at(9)).value == std::numeric_limits<ark::guid_t>::min());
    CHECK(std::get<float>(changed.fields.at(6)) == std::numeric_limits<float>::max());
    CHECK(std::get<double>(changed.fields.at(7)) == std::numeric_limits<double>::lowest());
    CHECK_FALSE(snapshot == changed);
    const auto retained = snapshot;
    const std::weak_ptr<ark::entity_impl> retired = fixture.source;
    fixture.source.reset();
    fixture.schema.reset();
    CHECK(retired.expired());
    CHECK(snapshot == retained);
    CHECK(std::get<std::string>(snapshot.fields.at(8)) == binary);
}

TEST_CASE("scalar snapshots preserve finite floating subnormals and signed zero") {
    snapshot_entity fixture;
    REQUIRE(fixture.source->set_float(6, std::numeric_limits<float>::denorm_min()));
    REQUIRE(fixture.source->set_double(7, std::numeric_limits<double>::denorm_min()));
    const std::array<std::uint32_t, 2> selection{6, 7};
    const auto tiny = capture_scalar_snapshot(*fixture.source, selection);
    CHECK(std::get<float>(tiny.fields.at(6)) == std::numeric_limits<float>::denorm_min());
    CHECK(std::get<double>(tiny.fields.at(7)) == std::numeric_limits<double>::denorm_min());
    REQUIRE(fixture.source->set_float(6, -0.0F));
    REQUIRE(fixture.source->set_double(7, -0.0));
    const auto zero = capture_scalar_snapshot(*fixture.source, selection);
    CHECK(std::signbit(std::get<float>(zero.fields.at(6))));
    CHECK(std::signbit(std::get<double>(zero.fields.at(7))));
}

TEST_CASE("scalar snapshots capture declared lazy fields as typed zeros without creating nodes") {
    snapshot_entity fixture;
    REQUIRE(fixture.materialized().ByteSizeLong() == 0);
    const auto snapshot = capture_scalar_snapshot(*fixture.source, all_fields);
    REQUIRE(snapshot.fields.size() == all_fields.size());
    CHECK_FALSE(std::get<bool>(snapshot.fields.at(1)));
    CHECK(std::get<std::int32_t>(snapshot.fields.at(2)) == 0);
    CHECK(std::get<std::uint32_t>(snapshot.fields.at(3)) == 0);
    CHECK(std::get<std::int64_t>(snapshot.fields.at(4)) == 0);
    CHECK(std::get<std::uint64_t>(snapshot.fields.at(5)) == 0);
    CHECK(std::get<float>(snapshot.fields.at(6)) == 0.0F);
    CHECK(std::get<double>(snapshot.fields.at(7)) == 0.0);
    CHECK(std::get<std::string>(snapshot.fields.at(8)).empty());
    CHECK(std::get<guid_value>(snapshot.fields.at(9)).value == ark::guid_t{});
    CHECK(fixture.materialized().ByteSizeLong() == 0);
}

TEST_CASE("scalar snapshot selection is explicit and never includes unselected fields") {
    snapshot_entity fixture;
    REQUIRE(fixture.source->set_bool(1, true));
    REQUIRE(fixture.source->set_float(6, std::numeric_limits<float>::infinity()));
    REQUIRE(fixture.source->set_string(8, std::string(4096, 'x')));
    fixture.add_field(10, ark::data_type::dt_pointer);
    const std::array<std::uint32_t, 1> selection{1};
    const auto selected = capture_scalar_snapshot(*fixture.source, selection, {1, 13});
    REQUIRE(selected.fields.size() == 1);
    CHECK(std::get<bool>(selected.fields.at(1)));
    const auto empty = capture_scalar_snapshot(*fixture.source, {}, {0, 13});
    CHECK(empty.fields.empty());
    CHECK(empty.class_name == fixture.source->get_class_name());
}

TEST_CASE("scalar snapshot rejects invalid selections and unsupported declared types without mutation") {
    snapshot_entity fixture;
    const auto before = capture_scalar_snapshot(*fixture.source, all_fields);
    const std::array<std::uint32_t, 1> zero{0};
    const std::array<std::uint32_t, 1> unknown{999};
    const std::array<std::uint32_t, 2> duplicate{1, 1};
    CHECK_THROWS_AS(capture_scalar_snapshot(*fixture.source, zero), std::invalid_argument);
    CHECK_THROWS_AS(capture_scalar_snapshot(*fixture.source, unknown), std::invalid_argument);
    CHECK_THROWS_AS(capture_scalar_snapshot(*fixture.source, duplicate), std::invalid_argument);
    const std::array unsupported{ark::data_type::dt_empty, ark::data_type::dt_wstring, ark::data_type::dt_vector3d,
        ark::data_type::dt_array, ark::data_type::dt_table, ark::data_type::dt_pointer, ark::data_type::dt_userdata,
        static_cast<ark::data_type>(999)};
    std::uint32_t index = 20;
    for (const auto type : unsupported)
    {
        fixture.add_field(index, type);
        const std::array selection{index++};
        CHECK_THROWS_AS(capture_scalar_snapshot(*fixture.source, selection), std::invalid_argument);
    }
    CHECK(capture_scalar_snapshot(*fixture.source, all_fields) == before);
    CHECK(fixture.materialized().ByteSizeLong() == 0);
}

TEST_CASE("scalar snapshot rejects nonfinite selected floats while preserving source values") {
    snapshot_entity fixture;
    const std::array<std::uint32_t, 1> floats{6};
    const std::array<std::uint32_t, 1> doubles{7};
    for (const auto value : {std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
             std::numeric_limits<float>::quiet_NaN()})
    {
        static_cast<void>(fixture.source->set_float(6, value));
        CHECK_THROWS_AS(capture_scalar_snapshot(*fixture.source, floats), std::invalid_argument);
        const auto after = fixture.source->get_float(6);
        CHECK((std::isnan(value) ? std::isnan(after) : after == value));
    }
    for (const auto value : {std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
             std::numeric_limits<double>::quiet_NaN()})
    {
        static_cast<void>(fixture.source->set_double(7, value));
        CHECK_THROWS_AS(capture_scalar_snapshot(*fixture.source, doubles), std::invalid_argument);
        const auto after = fixture.source->get_double(7);
        CHECK((std::isnan(value) ? std::isnan(after) : after == value));
    }
}

TEST_CASE("scalar snapshot limits count class and binary string bytes before publishing any result") {
    snapshot_entity fixture("ABC");
    const std::string binary("x\0y", 3);
    REQUIRE(fixture.source->set_string(8, binary));
    fixture.add_field(10, ark::data_type::dt_string);
    REQUIRE(fixture.source->set_string(10, "z"));
    const std::array<std::uint32_t, 2> strings{8, 10};
    const auto before = capture_scalar_snapshot(*fixture.source, strings, {2, 7});
    auto destination = before;
    CHECK_THROWS_AS(capture_scalar_snapshot(*fixture.source, strings, {1, 7}), std::length_error);
    CHECK_THROWS_AS(destination = capture_scalar_snapshot(*fixture.source, strings, {2, 6}), std::length_error);
    CHECK_THROWS_AS(capture_scalar_snapshot(*fixture.source, {}, {0, 2}), std::length_error);
    CHECK(capture_scalar_snapshot(*fixture.source, strings, {2, 7}) == before);
    CHECK(destination == before);
    CHECK(capture_scalar_snapshot(*fixture.source, {}, {0, 3}).fields.empty());

    snapshot_entity lazy("");
    CHECK(capture_scalar_snapshot(*lazy.source, {}, {0, 0}).fields.empty());
    CHECK(
        capture_scalar_snapshot(*lazy.source, all_fields, {all_fields.size(), std::numeric_limits<std::size_t>::max()})
            .fields.size() == all_fields.size());
    CHECK(lazy.materialized().ByteSizeLong() == 0);
}
