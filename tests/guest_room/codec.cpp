// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <string>

#include "../network/doctest.h"
#include <nlohmann/json.hpp>

#include "../../examples/guest_room/guest_room_wire.hpp"
#include "ark/net/net_session.hpp"

namespace {
using namespace ark::guest_room;
using json = nlohmann::json;

std::string frame(ark::msg_id_t id, const std::string& body) {
    header head;
    head.set_message_id(id);
    head.set_message_length(static_cast<std::uint32_t>(body.size()));
    std::string result;
    head.serialize_to_string(result);
    return result + body;
}

std::string hex(const std::string& bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string text;
    for (const unsigned char byte : bytes) {
        text.push_back(digits[byte >> 4]);
        text.push_back(digits[byte & 15]);
    }
    return text;
}

std::string unhex(const std::string& text) {
    REQUIRE(text.size() % 2 == 0);
    std::string bytes;
    for (std::size_t index = 0; index < text.size(); index += 2) {
        bytes.push_back(static_cast<char>(std::stoul(text.substr(index, 2), nullptr, 16)));
    }
    return bytes;
}

template<class Message>
void protobuf_roundtrip(const std::string& body) {
    Message parsed;
    REQUIRE(parsed.ParseFromString(body));
    CHECK(parsed.SerializeAsString() == body);
}

ark::entity_sync::nested_baseline initial_player() {
    return {1, 1,
        {player_schema,
            {1,
                {42, "GuestPlayer",
                    {{100, std::string("Alice")}, {101, 120.0}, {102, 100.0}, {103, std::uint32_t{0}},
                        {104, std::uint32_t{100}}, {105, std::uint32_t{100}}, {106, std::uint32_t{100}},
                        {107, std::uint32_t{100}}, {108, std::uint32_t{0}}}},
                {{inventory_index, {"InventoryItem", {}}}},
                {{equipment_index,
                    {{1, {2, {43, "GuestEquipment", {{1, std::uint32_t{1001}}, {2, std::uint32_t{0}}}}, {}, {}}}}}}}}};
}

ark::entity_sync::nested_stream_binding initial_binding() {
    return {player_schema, 42, 1, 1, player_layout({43, 44})};
}

ark::entity_sync::nested_scalar_type scalar_type(const std::string& name) {
    using type = ark::entity_sync::nested_scalar_type;
    if (name.starts_with("bool")) {
        return type::boolean;
    }
    if (name.starts_with("int32")) {
        return type::int32;
    }
    if (name.starts_with("uint32")) {
        return type::uint32;
    }
    if (name.starts_with("int64")) {
        return type::int64;
    }
    if (name.starts_with("uint64")) {
        return type::uint64;
    }
    if (name.starts_with("float")) {
        return type::real32;
    }
    if (name.starts_with("double")) {
        return type::real64;
    }
    if (name.starts_with("guid")) {
        return type::guid;
    }
    if (name.starts_with("bytes")) {
        return type::bytes;
    }
    throw std::invalid_argument("Unknown scalar fixture type");
}

} // namespace

TEST_CASE("Guest room golden frames match the C sharp shared fixture") {
    std::ifstream input(ARK_GUEST_ROOM_VECTORS);
    REQUIRE(input.good());
    const auto fixtures = json::parse(input);
    CHECK(fixtures["format"] == "ark.guest-room.protobuf");
    std::map<ark::guid_t, ark::entity_sync::nested_stream_binding> bindings;
    for (const auto& vector : fixtures["vectors"]) {
        if (vector["message_id"].get<ark::msg_id_t>() == snapshot) {
            wire::room_baseline room;
            REQUIRE(room.ParseFromString(unhex(vector["body_hex"].get<std::string>())));
            for (const auto& player : room.players()) {
                const auto trusted = decode_binding(player.binding());
                bindings.insert_or_assign(trusted.root_id, trusted);
            }
        }
    }
    for (const auto& vector : fixtures["vectors"]) {
        const auto id = vector["message_id"].get<ark::msg_id_t>();
        const auto body = unhex(vector["body_hex"].get<std::string>());
        const auto bytes = frame(id, body);
        CHECK(hex(bytes) == vector["frame_hex"].get<std::string>());
        header parsed;
        parsed.parse_from_array(bytes.data());
        CHECK(parsed.message_id() == id);
        CHECK(parsed.message_length() == body.size());
        switch (id) {
        case join:
            protobuf_roundtrip<wire::join_request>(body);
            break;
        case accepted:
            protobuf_roundtrip<wire::join_accepted>(body);
            break;
        case move:
            protobuf_roundtrip<wire::move_request>(body);
            break;
        case action:
            protobuf_roundtrip<wire::action_request>(body);
            break;
        case snapshot: {
            protobuf_roundtrip<wire::room_baseline>(body);
            wire::room_baseline room;
            REQUIRE(room.ParseFromString(body));
            REQUIRE(room.players_size() >= 1);
            for (const auto& player : room.players()) {
                const auto trusted = decode_binding(player.binding());
                const auto state = decode(player.state(), trusted);
                validate_player(state.snapshot);
                state_wire::nested_baseline encoded;
                encode(state, encoded, trusted);
                CHECK(encoded.SerializeAsString() == player.state().SerializeAsString());
            }
            break;
        }
        case delta: {
            protobuf_roundtrip<wire::room_delta>(body);
            wire::room_delta room;
            REQUIRE(room.ParseFromString(body));
            REQUIRE(room.changed_size() >= 1);
            for (const auto& change : room.changed()) {
                const auto& trusted = bindings.at(static_cast<ark::guid_t>(change.root().entity_id()));
                state_wire::nested_delta encoded;
                encode(decode(change, trusted), encoded, trusted);
                CHECK(encoded.SerializeAsString() == change.SerializeAsString());
            }
            break;
        }
        case error:
            protobuf_roundtrip<wire::error_response>(body);
            break;
        case ping:
        case pong:
            protobuf_roundtrip<wire::heartbeat>(body);
            break;
        default:
            FAIL("unknown golden message");
        }
    }
    for (const auto& vector : fixtures["scalar_vectors"]) {
        const auto body = unhex(vector["body_hex"].get<std::string>());
        const auto name = vector["name"].get<std::string>();
        state_wire::scalar_field field;
        REQUIRE(field.ParseFromString(body));
        REQUIRE(field.index() == 100);
        CHECK(field.SerializeAsString() == body);
        state_wire::nested_baseline envelope;
        envelope.set_generation(1);
        envelope.set_revision(1);
        envelope.mutable_snapshot()->set_schema_id("scalar-fixture");
        auto& root = *envelope.mutable_snapshot()->mutable_root();
        root.set_entity_id(42);
        root.set_incarnation(1);
        root.set_class_name("Types");
        *root.add_fields() = field;
        const ark::entity_sync::nested_stream_binding trusted{
            "scalar-fixture", 42, 1, 1, {"Types", {{100, scalar_type(name)}}, {}, {}}};
        if (name.starts_with("guid") && (field.guid_value() < std::numeric_limits<ark::guid_t>::min() ||
                                            field.guid_value() > std::numeric_limits<ark::guid_t>::max())) {
            CHECK_THROWS_AS(ark::entity_sync::nested_wire::decode(envelope, trusted), std::invalid_argument);
            continue;
        }
        const auto decoded = ark::entity_sync::nested_wire::decode(envelope, trusted);
        const auto& value = decoded.snapshot.root.scalars.fields.at(100);
        state_wire::nested_baseline encoded;
        ark::entity_sync::nested_wire::encode(decoded, encoded, trusted);
        CHECK(encoded.snapshot().root().fields(0).SerializeAsString() == body);
        if (name.starts_with("bool")) {
            CHECK(std::holds_alternative<bool>(value));
        } else if (name.starts_with("int32")) {
            CHECK(std::holds_alternative<std::int32_t>(value));
        } else if (name.starts_with("uint32")) {
            CHECK(std::holds_alternative<std::uint32_t>(value));
        } else if (name.starts_with("int64")) {
            CHECK(std::holds_alternative<std::int64_t>(value));
        } else if (name.starts_with("uint64")) {
            CHECK(std::holds_alternative<std::uint64_t>(value));
        } else if (name.starts_with("float")) {
            CHECK(std::get<float>(value) == 1.5f);
        } else if (name.starts_with("double")) {
            CHECK(std::get<double>(value) == -2.25);
        } else if (name.starts_with("guid")) {
            CHECK(std::holds_alternative<ark::entity_sync::guid_value>(value));
        } else if (name.starts_with("bytes")) {
            CHECK(std::get<std::string>(value) == std::string("A\0B", 3));
        }
    }
}

TEST_CASE("Guest room Net retains split headers bodies and coalesced frames") {
    const auto bytes = frame(join, [] {
        wire::join_request body;
        body.set_name("Alice");
        body.set_room("lobby");
        body.set_protocol_version(protocol_version);
        return body.SerializeAsString();
    }());
    for (std::size_t split = 1; split < bytes.size(); ++split) {
        ark::tcp_client_session session(std::make_shared<header>(), 1, nullptr, 16);
        session.add_buffer(bytes.data(), split);
        REQUIRE(session.parse_buffer_to_msg());
        ark::net_msg* message = nullptr;
        CHECK_FALSE(session.pop_net_msg(message));
        const auto rest = bytes.substr(split) + frame(ping, "\x08\x01");
        session.add_buffer(rest.data(), rest.size());
        REQUIRE(session.parse_buffer_to_msg());
        REQUIRE(session.pop_net_msg(message));
        CHECK(message->get_msg_id() == join);
        CHECK(std::string(message->get_msg_data(), message->get_msg_length()) == bytes.substr(12));
        ark::net_msg::release(message);
        REQUIRE(session.pop_net_msg(message));
        CHECK(message->get_msg_id() == ping);
        ark::net_msg::release(message);
        CHECK_FALSE(session.pop_net_msg(message));
    }
}

TEST_CASE("Guest room invalid headers fail before body allocation and callback") {
    for (const auto kind : {0, 1, 2}) {
        auto bytes = frame(join, "").substr(0, 12);
        if (kind == 0) {
            bytes[0] = 'X';
        } else if (kind == 1) {
            bytes[7] = 1;
        } else {
            bytes[10] = 0x20;
            bytes[11] = 1; // 8193-byte body, with no body present.
        }
        header parsed;
        parsed.parse_from_array(bytes.data());
        CHECK(parsed.message_length() == max_body + 1);
        ark::tcp_client_session session(std::make_shared<header>(), 1, nullptr, 16);
        session.add_buffer(bytes.data(), bytes.size());
        CHECK_FALSE(session.parse_buffer_to_msg());
        ark::net_msg* message = nullptr;
        CHECK_FALSE(session.pop_net_msg(message));
    }
    const auto maximum = frame(join, std::string(max_body, 'x'));
    ark::tcp_client_session session(std::make_shared<header>(), 1, nullptr, 16);
    session.add_buffer(maximum.data(), maximum.size());
    REQUIRE(session.parse_buffer_to_msg());
    ark::net_msg* message = nullptr;
    REQUIRE(session.pop_net_msg(message));
    CHECK(message->get_msg_length() == max_body);
    ark::net_msg::release(message);
}

TEST_CASE("Guest room reconnect owns fresh parser state and reserved IDs cannot enter Net dispatch") {
    const auto bytes = frame(ping, "\x08\x01");
    {
        ark::tcp_client_session stale(std::make_shared<header>(), 1, nullptr, 16);
        stale.add_buffer(bytes.data(), 14);
        REQUIRE(stale.parse_buffer_to_msg());
    }
    ark::tcp_client_session fresh(std::make_shared<header>(), 2, nullptr, 16);
    fresh.add_buffer(bytes.data(), bytes.size());
    REQUIRE(fresh.parse_buffer_to_msg());
    ark::net_msg* message = nullptr;
    REQUIRE(fresh.pop_net_msg(message));
    CHECK(message->get_msg_id() == ping);
    ark::net_msg::release(message);
    for (const auto id : {0, 1, 65535, 60001, 60007, 12345}) {
        const auto unsupported_frame = frame(static_cast<ark::msg_id_t>(id), "{}");
        header request(true);
        request.parse_from_array(unsupported_frame.data());
        CHECK(request.message_id() == unsupported);
    }
    const auto action_frame = frame(action, "\x08\x01\x10\x01");
    header request(true);
    request.parse_from_array(action_frame.data());
    CHECK(request.message_id() == action);
    CHECK(request.max_body_size() == 8192);
    auto old_marker = action_frame;
    old_marker[3] = '1';
    request.parse_from_array(old_marker.data());
    CHECK(request.message_length() > request.max_body_size());
}

TEST_CASE("Guest room validates UTF-8 names independently of protobuf text fields") {
    CHECK(valid_name(std::string(64, 'a')));
    CHECK(valid_name("访客"));
    for (const auto& name : {std::string(), std::string(65, 'a'), std::string("a\nb"), std::string("a\xc2\x85"),
             std::string("\xc0\x80"), std::string("\xed\xa0\x80"), std::string("\xf4\x90\x80\x80"),
             std::string("\xe4\xb8"), std::string("\xff")}) {
        CHECK_FALSE(valid_name(name));
    }
}

TEST_CASE("Generic protobuf alternatives preserve widths signedness zero presence and raw bytes") {
    using namespace ark::entity_sync;
    const nested_baseline original{1, 1,
        {"all-types",
            {1,
                {42, "Types",
                    {{1, false}, {2, std::numeric_limits<std::int32_t>::min()},
                        {3, std::numeric_limits<std::uint32_t>::max()}, {4, std::numeric_limits<std::int64_t>::min()},
                        {5, std::numeric_limits<std::uint64_t>::max()}, {6, 0.0f}, {7, 0.0},
                        {8, std::string("\0\xff", 2)}, {9, guid_value{-42}}}},
                {}, {}}}};
    const nested_stream_binding trusted{"all-types", 42, 1, 1,
        {"Types",
            {{1, nested_scalar_type::boolean}, {2, nested_scalar_type::int32}, {3, nested_scalar_type::uint32},
                {4, nested_scalar_type::int64}, {5, nested_scalar_type::uint64}, {6, nested_scalar_type::real32},
                {7, nested_scalar_type::real64}, {8, nested_scalar_type::bytes}, {9, nested_scalar_type::guid}},
            {}, {}}};
    state_wire::nested_baseline encoded;
    nested_wire::encode(original, encoded, trusted);
    const auto& fields = encoded.snapshot().root().fields();
    REQUIRE(fields.size() == 9);
    CHECK(fields.Get(0).value_case() == state_wire::scalar_field::kBoolValue);
    CHECK(fields.Get(5).value_case() == state_wire::scalar_field::kFloatValue);
    CHECK(fields.Get(6).value_case() == state_wire::scalar_field::kDoubleValue);
    state_wire::nested_baseline parsed;
    REQUIRE(parsed.ParseFromString(encoded.SerializeAsString()));
    CHECK(nested_wire::decode(parsed, trusted).snapshot == original.snapshot);
    auto unknown = encoded.SerializeAsString();
    unknown.append("\xa0\x06\x01", 3); // Unknown field 100 remains forward compatible.
    REQUIRE(parsed.ParseFromString(unknown));
    CHECK(nested_wire::decode(parsed, trusted).snapshot == original.snapshot);
    auto invalid = encoded;
    auto* root = invalid.mutable_snapshot()->mutable_root();
    root->add_fields()->CopyFrom(root->fields(0));
    CHECK_THROWS_AS(nested_wire::decode(invalid, trusted), std::invalid_argument);
    invalid = encoded;
    invalid.mutable_snapshot()->mutable_root()->mutable_fields(0)->clear_value();
    CHECK_THROWS_AS(nested_wire::decode(invalid, trusted), std::invalid_argument);
    invalid = encoded;
    invalid.mutable_snapshot()->mutable_root()->mutable_fields(0)->set_index(0);
    CHECK_THROWS_AS(nested_wire::decode(invalid, trusted), std::invalid_argument);
    invalid = encoded;
    invalid.mutable_snapshot()->mutable_root()->mutable_fields(5)->set_float_value(
        std::numeric_limits<float>::infinity());
    CHECK_THROWS_AS(nested_wire::decode(invalid, trusted), std::invalid_argument);
    invalid = encoded;
    invalid.mutable_snapshot()->mutable_root()->mutable_fields(6)->set_double_value(
        std::numeric_limits<double>::quiet_NaN());
    CHECK_THROWS_AS(nested_wire::decode(invalid, trusted), std::invalid_argument);
    invalid = encoded;
    invalid.mutable_snapshot()->mutable_root()->set_entity_id(0);
    CHECK_THROWS_AS(nested_wire::decode(invalid, trusted), std::invalid_argument);
    invalid = encoded;
    invalid.mutable_snapshot()->mutable_root()->mutable_fields(8)->set_guid_value(
        std::numeric_limits<std::int64_t>::min());
    if constexpr (sizeof(ark::guid_t) < sizeof(std::int64_t)) {
        CHECK_THROWS_AS(nested_wire::decode(invalid, trusted), std::invalid_argument);
    } else {
        CHECK(std::get<guid_value>(nested_wire::decode(invalid, trusted).snapshot.root.scalars.fields.at(9)).value ==
              std::numeric_limits<std::int64_t>::min());
    }
    auto negative_identity = initial_player();
    negative_identity.snapshot.root.scalars.entity_id = -1;
    state_wire::nested_baseline rejected;
    CHECK_THROWS_AS(encode(negative_identity, rejected, initial_binding()), std::invalid_argument);
    negative_identity.snapshot.root.scalars.entity_id = 0;
    CHECK_THROWS_AS(encode(negative_identity, rejected, initial_binding()), std::invalid_argument);
    nested_wire::limits budget;
    budget.stream.snapshot.max_fields = 8;
    CHECK_THROWS_AS(nested_wire::decode(encoded, trusted, budget), std::length_error);
    budget.stream.snapshot.max_fields = 9;
    budget.stream.snapshot.max_string_bytes = 5;
    CHECK_THROWS_AS(nested_wire::decode(encoded, trusted, budget), std::length_error);
    CHECK_FALSE(parsed.ParseFromString(std::string(1, static_cast<char>(0x80))));
}

TEST_CASE("Public nested delta codec keeps revision gaps and tombstones detectable") {
    using namespace ark::entity_sync;
    const auto initial = initial_player();
    const auto trusted = initial_binding();
    validate_player(initial.snapshot);
    nested_publisher publisher(trusted, initial, player_limits().stream);
    nested_replica replica(trusted, player_limits().stream);
    REQUIRE(replica.apply(initial) == nested_apply_result::applied);
    CHECK_FALSE(publisher.update(initial.snapshot));
    auto next = initial.snapshot;
    next.root.scalars.fields.at(104) = std::uint32_t{85};
    next.root.scalars.fields.at(108) = std::uint32_t{1};
    const auto change = publisher.update(next);
    REQUIRE(change);
    CHECK(change->root.fields.size() == 2);
    state_wire::nested_delta encoded;
    encode(*change, encoded, trusted);
    const auto decoded = decode(encoded, trusted);
    CHECK(replica.apply(decoded) == nested_apply_result::applied);
    CHECK(replica.apply(decoded) == nested_apply_result::duplicate);
    CHECK(replica.revision() == 2);
    auto gap = encoded;
    gap.set_required_revision(3);
    gap.set_revision(4);
    CHECK(replica.apply(decode(gap, trusted)) == nested_apply_result::rejected);
    nested_replica recovered(trusted, player_limits().stream);
    REQUIRE(recovered.apply(*publisher.baseline()) == nested_apply_result::applied);
    const auto removal = publisher.remove();
    encode(*removal, encoded, trusted);
    CHECK(recovered.apply(decode(encoded, trusted)) == nested_apply_result::applied);
    CHECK_FALSE(recovered.state());
    auto resurrection = *change;
    resurrection.required_revision = 3;
    resurrection.revision = 4;
    CHECK(recovered.apply(resurrection) == nested_apply_result::rejected);
    encoded.mutable_root()->add_fields()->set_uint32_value(1);
    CHECK_THROWS_AS(decode(encoded, trusted), std::invalid_argument);
    auto wrong = initial;
    wrong.snapshot.root.scalars.fields.at(104) = std::uint32_t{101};
    CHECK_THROWS_AS(validate_player(wrong.snapshot), std::invalid_argument);
    for (const auto current : {104u, 105u}) {
        wrong = initial;
        wrong.snapshot.root.scalars.fields.at(current) = std::uint32_t{0};
        wrong.snapshot.root.scalars.fields.at(current + 2) = std::uint32_t{0};
        CHECK_THROWS_AS(validate_player(wrong.snapshot), std::invalid_argument);
        wrong.snapshot.root.scalars.fields.at(current + 2) = std::uint32_t{1};
        CHECK_NOTHROW(validate_player(wrong.snapshot));
    }
    wrong = initial;
    wrong.snapshot.root.scalars.fields.at(103) = std::int32_t{0};
    CHECK_THROWS_AS(validate_player(wrong.snapshot), std::invalid_argument);
    wrong = initial;
    wrong.snapshot.root.scalars.fields.emplace(99, std::uint32_t{1});
    CHECK_THROWS_AS(validate_player(wrong.snapshot), std::invalid_argument);
    wrong = initial;
    wrong.snapshot.schema_id = "other";
    CHECK_THROWS_AS(validate_player(wrong.snapshot), std::invalid_argument);
    auto exhausted_baseline = initial;
    exhausted_baseline.revision = std::numeric_limits<std::uint64_t>::max();
    nested_publisher exhausted(trusted, exhausted_baseline, player_limits().stream);
    CHECK_THROWS_AS(exhausted.update(next), std::overflow_error);
}

TEST_CASE("Guest room largest protobuf roster stays inside framing budget") {
    using namespace ark::entity_sync;
    wire::room_baseline roster;
    roster.set_room("lobby");
    for (ark::guid_t index = 1; index <= 16; ++index) {
        auto& player = *roster.add_players();
        player.set_player_id("18446744073709551615");
        auto state = initial_player();
        auto& root = state.snapshot.root;
        root.scalars.entity_id = std::numeric_limits<ark::guid_t>::max() - 3 * index;
        root.incarnation = std::numeric_limits<std::uint64_t>::max();
        state.generation = std::numeric_limits<std::uint64_t>::max();
        state.revision = std::numeric_limits<std::uint64_t>::max();
        root.scalars.fields.at(100) = std::string(64, '"');
        root.scalars.fields.at(101) = 960.0;
        root.scalars.fields.at(102) = 540.0;
        for (std::uint32_t field = 103; field <= 108; ++field) {
            root.scalars.fields.at(field) = std::numeric_limits<std::uint32_t>::max();
        }
        root.tables.at(inventory_index)
            .rows.emplace(1, nested_row_snapshot{std::numeric_limits<std::uint64_t>::max(),
                                 {{1, std::uint32_t{1}}, {2, std::uint32_t{99}}}});
        auto& child = root.containers.at(equipment_index).at(1);
        child.scalars.entity_id = root.scalars.entity_id + 1;
        child.incarnation = std::numeric_limits<std::uint64_t>::max();
        child.scalars.fields.at(1) = std::uint32_t{1002};
        child.scalars.fields.at(2) = std::uint32_t{100};
        const nested_stream_binding trusted{player_schema, root.scalars.entity_id, root.incarnation, state.generation,
            player_layout({child.scalars.entity_id, root.scalars.entity_id + 2})};
        encode_binding(trusted, *player.mutable_binding());
        encode(state, *player.mutable_state(), trusted);
    }
    CHECK(roster.ByteSizeLong() <= max_body);
}
