// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <fstream>
#include <limits>
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
    for (const unsigned char byte : bytes)
    {
        text.push_back(digits[byte >> 4]);
        text.push_back(digits[byte & 15]);
    }
    return text;
}

std::string unhex(const std::string& text) {
    REQUIRE(text.size() % 2 == 0);
    std::string bytes;
    for (std::size_t index = 0; index < text.size(); index += 2)
    {
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

} // namespace

TEST_CASE("Guest room golden frames match the C sharp shared fixture") {
    std::ifstream input(ARK_GUEST_ROOM_VECTORS);
    REQUIRE(input.good());
    const auto fixtures = json::parse(input);
    CHECK(fixtures["format"] == "ark.guest-room.protobuf-v1");
    for (const auto& vector : fixtures["vectors"])
    {
        const auto id = vector["message_id"].get<ark::msg_id_t>();
        const auto body = unhex(vector["body_hex"].get<std::string>());
        const auto bytes = frame(id, body);
        CHECK(hex(bytes) == vector["frame_hex"].get<std::string>());
        header parsed;
        parsed.parse_from_array(bytes.data());
        CHECK(parsed.message_id() == id);
        CHECK(parsed.message_length() == body.size());
        switch (id)
        {
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
        case snapshot:
        {
            protobuf_roundtrip<wire::room_baseline>(body);
            wire::room_baseline room;
            REQUIRE(room.ParseFromString(body));
            REQUIRE(room.players_size() == 1);
            const auto state = decode(room.players(0).state(), player_limits);
            validate_player(state);
            wire::scalar_baseline encoded;
            encode(state, encoded);
            CHECK(encoded.SerializeAsString() == room.players(0).state().SerializeAsString());
            break;
        }
        case delta:
        {
            protobuf_roundtrip<wire::room_delta>(body);
            wire::room_delta room;
            REQUIRE(room.ParseFromString(body));
            REQUIRE(room.changed_size() == 1);
            wire::scalar_delta encoded;
            encode(decode(room.changed(0), player_limits), encoded);
            CHECK(encoded.SerializeAsString() == room.changed(0).SerializeAsString());
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
    for (const auto& vector : fixtures["scalar_vectors"])
    {
        const auto body = unhex(vector["body_hex"].get<std::string>());
        const auto name = vector["name"].get<std::string>();
        wire::scalar_field field;
        REQUIRE(field.ParseFromString(body));
        REQUIRE(field.index() == 100);
        CHECK(field.SerializeAsString() == body);
        if (name.starts_with("guid") && (field.guid_value() < std::numeric_limits<ark::guid_t>::min() ||
                                            field.guid_value() > std::numeric_limits<ark::guid_t>::max()))
        {
            CHECK_THROWS_AS(decode_field(field), std::invalid_argument);
            continue;
        }
        const auto value = decode_field(field);
        wire::scalar_field encoded;
        encode_field(100, value, encoded);
        CHECK(encoded.SerializeAsString() == body);
        if (name.starts_with("bool"))
        {
            CHECK(std::holds_alternative<bool>(value));
        }
        else if (name.starts_with("int32"))
        {
            CHECK(std::holds_alternative<std::int32_t>(value));
        }
        else if (name.starts_with("uint32"))
        {
            CHECK(std::holds_alternative<std::uint32_t>(value));
        }
        else if (name.starts_with("int64"))
        {
            CHECK(std::holds_alternative<std::int64_t>(value));
        }
        else if (name.starts_with("uint64"))
        {
            CHECK(std::holds_alternative<std::uint64_t>(value));
        }
        else if (name.starts_with("float"))
        {
            CHECK(std::get<float>(value) == 1.5f);
        }
        else if (name.starts_with("double"))
        {
            CHECK(std::get<double>(value) == -2.25);
        }
        else if (name.starts_with("guid"))
        {
            CHECK(std::holds_alternative<ark::entity_sync::guid_value>(value));
        }
        else if (name.starts_with("bytes"))
        {
            CHECK(std::get<std::string>(value) == std::string("A\0B", 3));
        }
    }
}

TEST_CASE("Guest room Net retains split headers bodies and coalesced frames") {
    const auto bytes = frame(join, [] {
        wire::join_request body;
        body.set_name("Alice");
        body.set_room("lobby");
        body.set_protocol_version(1);
        return body.SerializeAsString();
    }());
    for (std::size_t split = 1; split < bytes.size(); ++split)
    {
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
    for (const auto kind : {0, 1, 2})
    {
        auto bytes = frame(join, "").substr(0, 12);
        if (kind == 0)
        {
            bytes[0] = 'X';
        }
        else if (kind == 1)
        {
            bytes[7] = 1;
        }
        else
        {
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
    for (const auto id : {0, 1, 65535, 60001, 60007, 12345})
    {
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
    for (const auto& name :
        {std::string(), std::string(65, 'a'), std::string("a\nb"), std::string("a\xc2\x85"), std::string("\xc0\x80"),
            std::string("\xed\xa0\x80"), std::string("\xf4\x90\x80\x80"), std::string("\xe4\xb8"), std::string("\xff")})
    {
        CHECK_FALSE(valid_name(name));
    }
}

TEST_CASE("Scalar protobuf alternatives preserve widths signedness zero presence and raw bytes") {
    using namespace ark::entity_sync;
    const scalar_baseline original{"all-types", 1, 1,
        {42, "Types",
            {{1, false}, {2, std::numeric_limits<std::int32_t>::min()}, {3, std::numeric_limits<std::uint32_t>::max()},
                {4, std::numeric_limits<std::int64_t>::min()}, {5, std::numeric_limits<std::uint64_t>::max()},
                {6, 0.0f}, {7, 0.0}, {8, std::string("\0\xff", 2)}, {9, guid_value{-42}}}}};
    wire::scalar_baseline encoded;
    encode(original, encoded);
    REQUIRE(encoded.fields_size() == 9);
    CHECK(encoded.fields(0).value_case() == wire::scalar_field::kBoolValue);
    CHECK(encoded.fields(5).value_case() == wire::scalar_field::kFloatValue);
    CHECK(encoded.fields(6).value_case() == wire::scalar_field::kDoubleValue);
    wire::scalar_baseline parsed;
    REQUIRE(parsed.ParseFromString(encoded.SerializeAsString()));
    CHECK(decode(parsed).snapshot == original.snapshot);
    auto unknown = encoded.SerializeAsString();
    unknown.append("\xa0\x06\x01", 3); // Unknown field 100 is forward compatible.
    REQUIRE(parsed.ParseFromString(unknown));
    CHECK(decode(parsed).snapshot == original.snapshot);
    auto invalid = encoded;
    invalid.add_fields()->CopyFrom(invalid.fields(0));
    CHECK_THROWS_AS(decode(invalid), std::invalid_argument);
    invalid = encoded;
    invalid.mutable_fields(0)->clear_value();
    CHECK_THROWS_AS(decode(invalid), std::invalid_argument);
    invalid = encoded;
    invalid.mutable_fields(0)->set_index(0);
    CHECK_THROWS_AS(decode(invalid), std::invalid_argument);
    invalid = encoded;
    invalid.mutable_fields(5)->set_float_value(std::numeric_limits<float>::infinity());
    CHECK_THROWS_AS(decode(invalid), std::invalid_argument);
    invalid = encoded;
    invalid.mutable_fields(6)->set_double_value(std::numeric_limits<double>::quiet_NaN());
    CHECK_THROWS_AS(decode(invalid), std::invalid_argument);
    invalid = encoded;
    invalid.set_entity_id(std::numeric_limits<std::uint64_t>::max());
    CHECK_THROWS_AS(decode(invalid), std::invalid_argument);
    invalid = encoded;
    invalid.mutable_fields(8)->set_guid_value(std::numeric_limits<std::int64_t>::min());
    if constexpr (sizeof(ark::guid_t) < sizeof(std::int64_t))
    {
        CHECK_THROWS_AS(decode(invalid), std::invalid_argument);
    }
    else
    {
        CHECK(std::get<guid_value>(decode(invalid).snapshot.fields.at(9)).value ==
              std::numeric_limits<std::int64_t>::min());
    }
    auto negative_identity = original;
    negative_identity.snapshot.entity_id = -1;
    wire::scalar_baseline rejected;
    CHECK_THROWS_AS(encode(negative_identity, rejected), std::invalid_argument);
    negative_identity.snapshot.entity_id = 0;
    CHECK_THROWS_AS(encode(negative_identity, rejected), std::invalid_argument);
    CHECK_THROWS_AS(decode(encoded, {8, 100}), std::length_error);
    CHECK_THROWS_AS(decode(encoded, {9, 5}), std::length_error);
    CHECK_FALSE(parsed.ParseFromString(std::string(1, static_cast<char>(0x80))));
}

TEST_CASE("Public scalar delta codec keeps revision gaps and tombstones detectable") {
    using namespace ark::entity_sync;
    scalar_baseline initial{player_schema, 1, 1,
        {42, "GuestPlayer",
            {{100, std::string("Alice")}, {101, 120.0}, {102, 100.0}, {103, std::uint32_t{0}},
                {104, std::uint32_t{100}}, {105, std::uint32_t{100}}, {106, std::uint32_t{100}},
                {107, std::uint32_t{100}}, {108, std::uint32_t{0}}}}};
    validate_player(initial);
    scalar_publisher publisher(initial, player_limits);
    scalar_replica replica(player_schema, 42, "GuestPlayer", player_limits);
    REQUIRE(replica.apply(initial) == scalar_apply_result::applied);
    CHECK_FALSE(publisher.update(initial.snapshot));
    auto next = initial.snapshot;
    next.fields.at(104) = std::uint32_t{85};
    next.fields.at(108) = std::uint32_t{1};
    const auto change = publisher.update(next);
    REQUIRE(change);
    CHECK(change->fields.size() == 2);
    wire::scalar_delta encoded;
    encode(*change, encoded);
    const auto decoded = decode(encoded, player_limits);
    CHECK(replica.apply(decoded) == scalar_apply_result::applied);
    CHECK(replica.apply(decoded) == scalar_apply_result::duplicate);
    CHECK(replica.revision() == 2);
    auto gap = encoded;
    gap.set_required_revision(3);
    gap.set_revision(4);
    CHECK(replica.apply(decode(gap)) == scalar_apply_result::rejected);
    scalar_replica recovered(player_schema, 42, "GuestPlayer", player_limits);
    REQUIRE(recovered.apply(publisher.baseline()) == scalar_apply_result::applied);
    const auto removal = publisher.remove();
    encode(*removal, encoded);
    CHECK(recovered.apply(decode(encoded)) == scalar_apply_result::applied);
    CHECK_FALSE(recovered.state());
    auto resurrection = *change;
    resurrection.required_revision = 3;
    resurrection.revision = 4;
    CHECK(recovered.apply(resurrection) == scalar_apply_result::rejected);
    encoded.add_fields()->set_uint32_value(1);
    CHECK_THROWS_AS(decode(encoded), std::invalid_argument);
    auto wrong = initial;
    wrong.snapshot.fields.at(104) = std::uint32_t{101};
    CHECK_THROWS_AS(validate_player(wrong), std::invalid_argument);
    for (const auto current : {104u, 105u})
    {
        wrong = initial;
        wrong.snapshot.fields.at(current) = std::uint32_t{0};
        wrong.snapshot.fields.at(current + 2) = std::uint32_t{0};
        CHECK_THROWS_AS(validate_player(wrong), std::invalid_argument);
        wrong.snapshot.fields.at(current + 2) = std::uint32_t{1};
        CHECK_NOTHROW(validate_player(wrong));
    }
    wrong = initial;
    wrong.snapshot.fields.at(103) = std::int32_t{0};
    CHECK_THROWS_AS(validate_player(wrong), std::invalid_argument);
    wrong = initial;
    wrong.snapshot.fields.emplace(99, std::uint32_t{1});
    CHECK_THROWS_AS(validate_player(wrong), std::invalid_argument);
    wrong = initial;
    wrong.schema_id = "other";
    CHECK_THROWS_AS(validate_player(wrong), std::invalid_argument);
    scalar_publisher exhausted({player_schema, 1, std::numeric_limits<std::uint64_t>::max(), initial.snapshot});
    CHECK_THROWS_AS(exhausted.update(next), std::overflow_error);
}

TEST_CASE("Guest room largest protobuf roster stays inside framing budget") {
    wire::room_baseline roster;
    roster.set_room("lobby");
    for (std::uint64_t index = 1; index <= 16; ++index)
    {
        auto& player = *roster.add_players();
        player.set_player_id("18446744073709551615");
        ark::entity_sync::scalar_baseline state{player_schema, 1, std::numeric_limits<std::uint64_t>::max(),
            {static_cast<ark::guid_t>(index), "GuestPlayer",
                {{100, std::string(64, '"')}, {101, 960.0}, {102, 540.0}, {103, std::uint32_t{4294967295u}},
                    {104, std::uint32_t{4294967295u}}, {105, std::uint32_t{4294967295u}},
                    {106, std::uint32_t{4294967295u}}, {107, std::uint32_t{4294967295u}},
                    {108, std::uint32_t{4294967295u}}}}};
        encode(state, *player.mutable_state());
    }
    CHECK(roster.ByteSizeLong() <= max_body);
}
