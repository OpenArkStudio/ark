// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <memory>
#include <string>

#include "doctest.h"

#include <ark/net/net_event.hpp>
#include <ark/net/net_msg.hpp>

namespace {

void release_message(ark::net_msg* value) {
    ark::net_msg::release(value);
}

using message_owner = std::unique_ptr<ark::net_msg, decltype(&release_message)>;

message_owner make_message(const std::string& body) {
    message_owner value(ark::net_msg::alloc_msg(new ark::ss_msg_header), release_message);
    REQUIRE(value != nullptr);
    value->copy_data(body.data(), static_cast<uint32_t>(body.size()));
    return value;
}

std::string body(const ark::net_msg& value) {
    return value.get_msg_length() == 0 ? std::string{} : std::string(value.get_msg_data(), value.get_msg_length());
}

} // namespace

TEST_CASE("Message body copies grow, shrink and preserve binary input") {
    auto value = make_message("a");
    const std::string input("long\0body", 9);
    value->copy_data(input.data(), static_cast<uint32_t>(input.size()));
    CHECK(body(*value) == input);
    CHECK(input == std::string("long\0body", 9));
    value->copy_data("x", 1);
    CHECK(body(*value) == "x");
    value->copy_data(input.data(), static_cast<uint32_t>(input.size()));
    CHECK(body(*value) == input);
    value->copy_data(nullptr, 0);
    CHECK(value->get_msg_length() == 0);
    CHECK_THROWS_AS(value->copy_data(nullptr, 1), std::invalid_argument);
    CHECK(value->get_msg_length() == 0);
}

TEST_CASE("Message body copies support overlapping borrowed storage") {
    auto value = make_message("abcdef");
    auto* data = value->get_msg_data();
    value->copy_data(data + 2, 4);
    CHECK(body(*value) == "cdef");
    CHECK(value->get_msg_data() == data);
    value->copy_data(value->get_msg_data(), 4);
    CHECK(body(*value) == "cdef");
}

TEST_CASE("Message copies retain independent headers, bodies and native wire bytes") {
    const std::string input("a\0b", 3);
    auto source = make_message(input);
    source->set_msg_id(7);
    source->set_actor_id(42);
    source->set_src_bus(3);
    source->set_dst_bus(4);
    source->get_head().set_compressed(true);
    std::string original_header;
    source->get_head().serialize_to_string(original_header);
    auto destination = make_message("");
    destination->copy_from(source.get());
    CHECK(body(*destination) == input);
    CHECK(destination->get_msg_data() != source->get_msg_data());
    CHECK(&destination->get_head() != &source->get_head());
    CHECK(destination->get_actor_id() == 42);
    CHECK(destination->get_src_bus() == 3);
    CHECK(destination->get_dst_bus() == 4);
    CHECK(destination->get_compress() == 1);
    std::string cloned_header;
    destination->get_head().serialize_to_string(cloned_header);
    CHECK(cloned_header == original_header);
    source->set_msg_id(8);
    source->get_msg_data()[0] = 'z';
    CHECK(destination->get_msg_id() == 7);
    CHECK(body(*destination) == input);
    auto* head = &destination->get_head();
    auto* data = destination->get_msg_data();
    destination->copy_from(destination.get());
    CHECK(&destination->get_head() == head);
    CHECK(destination->get_msg_data() == data);
    CHECK(body(*destination) == input);
    CHECK_THROWS_AS(destination->copy_from(nullptr), std::invalid_argument);
    CHECK(body(*destination) == input);
    source->copy_data(nullptr, 0);
    destination->copy_from(source.get());
    CHECK(destination->get_msg_length() == 0);
    CHECK(destination->get_msg_data() == nullptr);
    CHECK(destination->get_msg_id() == 8);
}

TEST_CASE("Message length growth retains allocated bytes and tolerates mutable header lengths") {
    auto value = make_message("abc");
    value->set_msg_length(16);
    CHECK(value->get_msg_length() == 16);
    CHECK(std::string(value->get_msg_data(), 3) == "abc");
    value->get_msg_data()[15] = 'z';
    value->set_msg_length(2);
    value->copy_data("123456789", 9);
    CHECK(body(*value) == "123456789");
    value->get_head().set_message_length(100);
    auto destination = make_message("unchanged");
    CHECK_THROWS_AS(destination->copy_from(value.get()), std::length_error);
    CHECK(body(*destination) == "unchanged");
    value->copy_data("restored", 8);
    CHECK(body(*value) == "restored");
}

TEST_CASE("Message and event factories retain null release postconditions") {
    CHECK(ark::net_msg::alloc_msg(nullptr) == nullptr);
    auto* message = ark::net_msg::alloc_msg(new ark::s_msg_header);
    REQUIRE(message != nullptr);
    CHECK(message->get_msg_data() == nullptr);
    ark::net_msg::release(message);
    CHECK(message == nullptr);
    ark::net_msg::release(message);
    auto* event = ark::net_event::alloc_event();
    REQUIRE(event != nullptr);
    CHECK(event->get_type() == ark::net_event_type::none);
    ark::net_event::release(event);
    CHECK(event == nullptr);
    ark::net_event::release(event);
}

TEST_CASE("Native header factories preserve CS and SS serialization sizes") {
    ark::s_msg_header cs;
    ark::ss_msg_header ss;
    CHECK(cs.header_length() == sizeof(ark::msg_id_t) + sizeof(uint32_t) + sizeof(uint8_t));
    CHECK(ss.header_length() == cs.header_length() + sizeof(ark::guid_t) + 2 * sizeof(ark::bus_id_t));
    for (ark::msg_header* header : {static_cast<ark::msg_header*>(&cs), static_cast<ark::msg_header*>(&ss)})
    {
        header->set_message_id(3);
        header->set_message_length(5);
        std::unique_ptr<ark::msg_header> created(header->create());
        std::unique_ptr<ark::msg_header> cloned(header->clone());
        REQUIRE(created != nullptr);
        REQUIRE(cloned != nullptr);
        CHECK(created->message_length() == 0);
        std::string expected;
        std::string actual;
        header->serialize_to_string(expected);
        cloned->serialize_to_string(actual);
        CHECK(actual == expected);
    }
}
