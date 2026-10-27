// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <fstream>
#include <memory>
#include <string>

#include "../network/doctest.h"

#include "../../examples/guest_room/guest_room_protocol.hpp"
#include "ark/net/net_session.hpp"

namespace {
using namespace ark::guest_room;

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
} // namespace

TEST_CASE("Guest room golden frames match the C sharp shared fixture") {
    std::ifstream input(ARK_GUEST_ROOM_VECTORS);
    REQUIRE(input.good());
    const auto fixtures = json::parse(input);
    for (const auto& vector : fixtures["vectors"])
    {
        const auto id = vector["message_id"].get<ark::msg_id_t>();
        const auto body = vector["body"].get<std::string>();
        const auto bytes = frame(id, body);
        CHECK(hex(bytes) == vector["frame_hex"].get<std::string>());
        header parsed;
        parsed.parse_from_array(bytes.data());
        CHECK(parsed.message_id() == id);
        CHECK(parsed.message_length() == body.size());
    }
}

TEST_CASE("Guest room Net retains split headers bodies and coalesced frames") {
    const auto bytes = frame(join, R"({"name":"Alice","room":"lobby"})");
    for (std::size_t split = 1; split < bytes.size(); ++split)
    {
        ark::tcp_client_session session(std::make_shared<header>(), 1, nullptr, 16);
        session.add_buffer(bytes.data(), split);
        REQUIRE(session.parse_buffer_to_msg());
        ark::net_msg* message = nullptr;
        CHECK_FALSE(session.pop_net_msg(message));
        const auto rest = bytes.substr(split) + frame(ping, R"({"nonce":1})");
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
            bytes[10] = 0x10;
            bytes[11] = 1; // 4097-byte body, with no body present.
        }
        header parsed;
        parsed.parse_from_array(bytes.data());
        CHECK(parsed.message_length() == 4097);
        ark::tcp_client_session session(std::make_shared<header>(), 1, nullptr, 16);
        session.add_buffer(bytes.data(), bytes.size());
        CHECK_FALSE(session.parse_buffer_to_msg());
        ark::net_msg* message = nullptr;
        CHECK_FALSE(session.pop_net_msg(message));
    }
    const auto maximum = frame(join, std::string(4096, 'x'));
    ark::tcp_client_session session(std::make_shared<header>(), 1, nullptr, 16);
    session.add_buffer(maximum.data(), maximum.size());
    REQUIRE(session.parse_buffer_to_msg());
    ark::net_msg* message = nullptr;
    REQUIRE(session.pop_net_msg(message));
    CHECK(message->get_msg_length() == 4096);
    ark::net_msg::release(message);
}

TEST_CASE("Guest room reconnect owns fresh parser state and reserved IDs cannot enter Net dispatch") {
    const auto bytes = frame(ping, R"({"nonce":1})");
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
    for (const auto id : {0, 1, 65535, 60001, 12345})
    {
        const auto unsupported_frame = frame(static_cast<ark::msg_id_t>(id), "{}");
        header request(true);
        request.parse_from_array(unsupported_frame.data());
        CHECK(request.message_id() == unsupported);
    }
}

TEST_CASE("Guest room unsigned values and encoded name bounds are strict") {
    std::uint32_t value = 0;
    for (const auto text : {"0", "4294967295"})
    {
        CHECK(uint32_value(json::parse(text), value));
    }
    for (const auto text : {"-1", "4294967296", "1.0", "true", "null", "\"1\""})
    {
        CHECK_FALSE(uint32_value(json::parse(text), value));
    }
    CHECK(valid_name(std::string(64, 'a')));
    CHECK(valid_name("访客"));
    CHECK_FALSE(valid_name(""));
    CHECK_FALSE(valid_name(std::string(65, 'a')));
    CHECK_FALSE(valid_name("a\nb"));
    CHECK_FALSE(valid_name("a\xc2\x85"));
}

TEST_CASE("Guest room worst encoded roster fits the framing budget") {
    json players = json::array();
    for (int count = 0; count < 16; ++count)
    {
        players.push_back({{"id", "18446744073709551615"}, {"name", std::string(64, '"')},
            {"x", 2.2250738585072014e-308}, {"y", 2.2250738585072014e-308}, {"seq", 4294967295u}});
    }
    const json roster{{"room", "lobby"}, {"players", players}};
    CHECK(roster.dump().size() <= max_body);
}
