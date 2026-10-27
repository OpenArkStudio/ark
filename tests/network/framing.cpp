// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.

#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "doctest.h"

#include "ark/net/net_session.hpp"
#include "body_limit_header.hpp"

namespace {

std::string make_frame(ark::msg_id_t id, const std::string& body) {
    ark::s_msg_header header;
    header.set_message_id(id);
    header.set_message_length(static_cast<uint32_t>(body.size()));
    std::string bytes;
    header.serialize_to_string(bytes);
    return bytes + body;
}
} // namespace

TEST_CASE("Framing preserves split headers, binary payloads and coalesced messages") {
    ark::tcp_client_session session(std::make_shared<ark::s_msg_header>(), 1);
    const std::string payload("a\0b", 3);
    const auto first = make_frame(7, payload);
    session.add_buffer(first.data(), 2);
    CHECK(session.parse_buffer_to_msg());
    ark::net_msg* message = nullptr;
    CHECK_FALSE(session.pop_net_msg(message));
    session.add_buffer(first.data() + 2, first.size() - 2);
    session.add_buffer(make_frame(8, ""));
    CHECK(session.parse_buffer_to_msg());
    REQUIRE(session.pop_net_msg(message));
    CHECK(message->get_msg_id() == 7);
    CHECK(std::string(message->get_msg_data(), message->get_msg_length()) == payload);
    ark::net_msg::release(message);
    REQUIRE(session.pop_net_msg(message));
    CHECK(message->get_msg_id() == 8);
    CHECK(message->get_msg_length() == 0);
    ark::net_msg::release(message);
    CHECK_FALSE(session.pop_net_msg(message));
    CHECK(session.get_buffer_len() == 0);
}

TEST_CASE("Framing rejects oversized input without preempting disconnect bookkeeping") {
    ark::tcp_client_session session(std::make_shared<ark::s_msg_header>(), 1);
    ark::s_msg_header header;
    header.set_message_length(ark::ark_msg_max_length + 1);
    std::string bytes;
    header.serialize_to_string(bytes);
    session.add_buffer(bytes);
    CHECK_FALSE(session.parse_buffer_to_msg());
    CHECK_FALSE(session.need_remove());
    ark::net_msg* message = nullptr;
    CHECK_FALSE(session.pop_net_msg(message));
}

TEST_CASE("Receive buffers preserve unread bytes across compaction and growth") {
    ark::buffer buffer(4);
    CHECK(buffer.write("abc", 3));
    buffer.remove_data(2);
    CHECK(buffer.write("defg", 4));
    CHECK(buffer.get_length() == 5);
    CHECK(std::string(buffer.get_data(), buffer.get_length()) == "cdefg");
    buffer.remove_data(5);
    CHECK(buffer.get_length() == 0);
    CHECK(buffer.write("123456789", 9));
    CHECK(std::string(buffer.get_data(), buffer.get_length()) == "123456789");
    buffer.remove_data(8);
    CHECK(buffer.write("abcdef", 6));
    CHECK(std::string(buffer.get_data(), buffer.get_length()) == "9abcdef");
}

TEST_CASE("Framing retains a partial body after complete frames are consumed") {
    ark::tcp_client_session session(std::make_shared<ark::s_msg_header>(), 1);
    const auto first = make_frame(7, "first");
    const std::string payload(ark::ark_msg_max_length, 'x');
    const auto second = make_frame(8, payload);
    session.add_buffer(first + second.substr(0, second.size() - 3));
    CHECK(session.parse_buffer_to_msg());
    ark::net_msg* message{};
    REQUIRE(session.pop_net_msg(message));
    CHECK(message->get_msg_id() == 7);
    ark::net_msg::release(message);
    CHECK_FALSE(session.pop_net_msg(message));
    for (std::size_t i = second.size() - 3; i < second.size(); ++i)
    {
        session.add_buffer(second.data() + i, 1);
        CHECK(session.parse_buffer_to_msg());
    }
    REQUIRE(session.pop_net_msg(message));
    CHECK(message->get_msg_id() == 8);
    CHECK(std::string(message->get_msg_data(), message->get_msg_length()) == payload);
    ark::net_msg::release(message);
    CHECK(session.get_buffer_len() == 0);
    session.add_buffer(make_frame(9, "next"));
    CHECK(session.parse_buffer_to_msg());
    REQUIRE(session.pop_net_msg(message));
    CHECK(message->get_msg_id() == 9);
    ark::net_msg::release(message);
    CHECK_FALSE(session.pop_net_msg(message));
}

TEST_CASE("Discarding a partial frame also discards its decoded header") {
    ark::tcp_client_session session(std::make_shared<ark::s_msg_header>(), 1);
    const auto discarded = make_frame(7, std::string(100, 'x'));
    session.add_buffer(discarded.substr(0, discarded.size() - 5));
    CHECK(session.parse_buffer_to_msg());
    const auto remaining = session.get_buffer_len();
    CHECK(session.remove_buffer(remaining) == 0);
    session.add_buffer(make_frame(8, "next"));
    CHECK(session.parse_buffer_to_msg());
    ark::net_msg* message{};
    REQUIRE(session.pop_net_msg(message));
    CHECK(message->get_msg_id() == 8);
    CHECK(std::string(message->get_msg_data(), message->get_msg_length()) == "next");
    ark::net_msg::release(message);
    CHECK(session.get_buffer_len() == 0);
}

TEST_CASE("Receive buffer capacity is configurable independently of the message limit") {
    auto header = std::make_shared<ark::s_msg_header>();
    ark::tcp_client_session defaults(header, 1);
    CHECK(defaults.get_buffer_capacity() == 4096);
    for (size_t capacity : {size_t{64}, size_t{64 * 1024}, size_t{512 * 1024}})
    {
        ark::tcp_client_session session(header, 2, nullptr, capacity);
        CHECK(session.get_buffer_capacity() == capacity);
        const auto frame = make_frame(7, std::string(ark::ark_msg_max_length, 'x'));
        session.add_buffer(frame);
        REQUIRE(session.parse_buffer_to_msg());
        ark::net_msg* message{};
        REQUIRE(session.pop_net_msg(message));
        CHECK(message->get_msg_length() == ark::ark_msg_max_length);
        ark::net_msg::release(message);
        CHECK(session.get_buffer_capacity() >= capacity);
    }
}

TEST_CASE("Framing uses local header body limits and retains native CS and SS bounds") {
    for (const auto header :
        std::vector<std::shared_ptr<ark::msg_header>>{std::make_shared<ark::s_msg_header>(),
            std::make_shared<ark::ss_msg_header>(), std::make_shared<ark::test::large_body_header<ark::s_msg_header>>(),
            std::make_shared<ark::test::large_body_header<ark::ss_msg_header>>()})
    {
        const auto limit = header->max_body_size();
        CHECK((limit == 4096 || limit == 8192));
        std::unique_ptr<ark::msg_header> created(header->create());
        std::unique_ptr<ark::msg_header> cloned(header->clone());
        CHECK(created->max_body_size() == limit);
        CHECK(cloned->max_body_size() == limit);
        for (const auto size : {limit, limit + 1})
        {
            INFO("limit=", limit, ", length=", size);
            header->set_message_id(7);
            header->set_message_length(size);
            std::string bytes;
            header->serialize_to_string(bytes);
            if (size == limit)
            {
                bytes.append(size, 'x');
            }
            ark::tcp_client_session session(header, 1, nullptr, 16);
            session.add_buffer(bytes);
            CHECK(session.parse_buffer_to_msg() == (size == limit));
            ark::net_msg* message = nullptr;
            if (size == limit)
            {
                REQUIRE(session.pop_net_msg(message));
                CHECK(message->get_msg_length() == size);
                ark::net_msg::release(message);
            }
            CHECK_FALSE(session.pop_net_msg(message));
        }
        CHECK_FALSE(header->accepts_body_size(std::numeric_limits<std::size_t>::max()));
    }
}

TEST_CASE("A decoded wire header cannot raise the configured receive limit") {
    class wire_limit_header final : public ark::s_msg_header
    {
    public:
        std::uint32_t max_body_size() const override {
            return limit;
        }

        void parse_from_array(const void* source) override {
            ark::s_msg_header::parse_from_array(source);
            limit = std::numeric_limits<std::uint32_t>::max();
        }

        ark::msg_header* create() const override {
            return new wire_limit_header;
        }

        std::uint32_t limit{4096};
    };

    ark::s_msg_header source;
    source.set_message_length(4097);
    std::string bytes;
    source.serialize_to_string(bytes);
    ark::tcp_client_session session(std::make_shared<wire_limit_header>(), 1);
    session.add_buffer(bytes);
    CHECK_FALSE(session.parse_buffer_to_msg());
    ark::net_msg* message = nullptr;
    CHECK_FALSE(session.pop_net_msg(message));
}
