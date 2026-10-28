// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <cstdlib>
#include <memory>
#include <new>
#include <string>

#include "doctest.h"

#include <ark/net/net_event.hpp>
#include <ark/net/net_msg.hpp>
#include <ark/net/net_session.hpp>

namespace {

bool fail_next_array{};
bool fail_next_object{};
int live_headers{};

class test_header final : public ark::s_msg_header
{
public:
    test_header() {
        ++live_headers;
    }

    test_header(const test_header& other)
        : ark::s_msg_header(other)
        , fail_clone(other.fail_clone)
        , throw_clone(other.throw_clone) {
        ++live_headers;
    }

    ~test_header() override {
        --live_headers;
    }

    ark::msg_header* create() const override {
        return fail_create ? nullptr : new test_header;
    }

    ark::msg_header* clone() const override {
        if (throw_clone)
        {
            throw std::bad_alloc{};
        }
        return fail_clone ? nullptr : new (std::nothrow) test_header(*this);
    }

    bool fail_clone{};
    bool throw_clone{};
    bool fail_create{};
};

void release_message(ark::net_msg* value) {
    ark::net_msg::release(value);
}

using message_owner = std::unique_ptr<ark::net_msg, decltype(&release_message)>;

message_owner make_message(const std::string& body) {
    auto* header = new test_header;
    header->set_message_id(7);
    message_owner value(ark::net_msg::alloc_msg(header), release_message);
    REQUIRE(value != nullptr);
    value->copy_data(body.data(), static_cast<uint32_t>(body.size()));
    return value;
}

} // namespace

// Each hook fails exactly one small request; ordinary doctest allocations are unaffected.
void* operator new[](std::size_t size) {
    if (fail_next_array)
    {
        fail_next_array = false;
        throw std::bad_alloc{};
    }
    if (auto* value = std::malloc(size ? size : 1))
    {
        return value;
    }
    throw std::bad_alloc{};
}

void operator delete[](void* value) noexcept {
    std::free(value);
}

void operator delete[](void* value, std::size_t) noexcept {
    std::free(value);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    if (fail_next_object)
    {
        fail_next_object = false;
        return nullptr;
    }
    try
    { return ::operator new(size); }
    catch (...)
    { return nullptr; }
}

void operator delete(void* value, const std::nothrow_t&) noexcept {
    ::operator delete(value);
}

TEST_CASE("Message factory consumes its header on object and body allocation failure") {
    CHECK(live_headers == 0);
    auto* header = new test_header;
    fail_next_object = true;
    auto* value = ark::net_msg::alloc_msg(header);
    CHECK_FALSE(fail_next_object);
    CHECK(value == nullptr);
    CHECK(live_headers == 0);
    header = new test_header;
    header->set_message_length(8);
    fail_next_array = true;
    value = ark::net_msg::alloc_msg(header);
    CHECK_FALSE(fail_next_array);
    CHECK(value == nullptr);
    CHECK(live_headers == 0);
}

TEST_CASE("Message growth failure retains header, body and borrowed storage") {
    auto value = make_message("old");
    auto* head = &value->get_head();
    auto* data = value->get_msg_data();
    fail_next_array = true;
    CHECK_THROWS_AS(value->copy_data("replacement", 11), std::bad_alloc);
    CHECK_FALSE(fail_next_array);
    CHECK(&value->get_head() == head);
    CHECK(value->get_msg_data() == data);
    CHECK(value->get_msg_length() == 3);
    CHECK(std::string(data, 3) == "old");
    fail_next_array = true;
    CHECK_THROWS_AS(value->set_msg_length(16), std::bad_alloc);
    CHECK_FALSE(fail_next_array);
    CHECK(value->get_msg_data() == data);
    CHECK(value->get_msg_length() == 3);
    value->copy_data("replacement", 11);
    CHECK(std::string(value->get_msg_data(), value->get_msg_length()) == "replacement");
}

TEST_CASE("Message clone and body allocation failures leave both messages intact") {
    auto source = make_message("source");
    auto destination = make_message("old");
    auto* head = &destination->get_head();
    auto* data = destination->get_msg_data();
    auto& source_head = static_cast<test_header&>(source->get_head());
    source_head.fail_clone = true;
    CHECK_THROWS_AS(destination->copy_from(source.get()), std::bad_alloc);
    source_head.fail_clone = false;
    source_head.throw_clone = true;
    CHECK_THROWS_AS(destination->copy_from(source.get()), std::bad_alloc);
    source_head.throw_clone = false;
    fail_next_array = true;
    CHECK_THROWS_AS(destination->copy_from(source.get()), std::bad_alloc);
    CHECK_FALSE(fail_next_array);
    CHECK(live_headers == 2);
    CHECK(&destination->get_head() == head);
    CHECK(destination->get_msg_data() == data);
    CHECK(destination->get_msg_length() == 3);
    CHECK(std::string(data, 3) == "old");
    CHECK(std::string(source->get_msg_data(), source->get_msg_length()) == "source");
    destination->copy_from(source.get());
    CHECK(live_headers == 2);
    CHECK(std::string(destination->get_msg_data(), destination->get_msg_length()) == "source");
}

TEST_CASE("Event and native header factories return null on nothrow allocation failure") {
    fail_next_object = true;
    auto* event = ark::net_event::alloc_event();
    CHECK(event == nullptr);
    CHECK_FALSE(fail_next_object);
    ark::s_msg_header cs;
    ark::ss_msg_header ss;
    for (ark::msg_header* header : {static_cast<ark::msg_header*>(&cs), static_cast<ark::msg_header*>(&ss)})
    {
        fail_next_object = true;
        auto* created = header->create();
        CHECK(created == nullptr);
        CHECK_FALSE(fail_next_object);
        fail_next_object = true;
        auto* cloned = header->clone();
        CHECK(cloned == nullptr);
        CHECK_FALSE(fail_next_object);
    }
}

TEST_CASE("Receive parsing rejects failed header, message and body allocations without queueing") {
    for (int failure = 0; failure != 3; ++failure)
    {
        INFO("failure=", failure);
        auto prototype = std::make_shared<test_header>();
        ark::tcp_client_session session(prototype, 1);
        ark::s_msg_header header;
        header.set_message_length(4);
        std::string frame;
        header.serialize_to_string(frame);
        frame += "body";
        session.add_buffer(frame);
        prototype->fail_create = failure == 0;
        fail_next_object = failure == 1;
        fail_next_array = failure == 2;
        const bool parsed = session.parse_buffer_to_msg();
        CHECK_FALSE(parsed);
        CHECK_FALSE(fail_next_object);
        CHECK_FALSE(fail_next_array);
        CHECK(live_headers == 1);
        CHECK(session.get_buffer_len() == frame.size());
        ark::net_msg* message = nullptr;
        CHECK_FALSE(session.pop_net_msg(message));
        prototype->fail_create = false;
        REQUIRE(session.parse_buffer_to_msg());
        REQUIRE(session.pop_net_msg(message));
        CHECK(std::string(message->get_msg_data(), message->get_msg_length()) == "body");
        ark::net_msg::release(message);
        CHECK(live_headers == 1);
    }
    CHECK(live_headers == 0);
}
