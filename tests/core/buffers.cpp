// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <cstring>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "../network/doctest.h"

#include <ark/buffer.hpp>

static_assert(!std::is_copy_constructible_v<ark::buffer>);
static_assert(!std::is_copy_assignable_v<ark::buffer>);
static_assert(std::is_nothrow_move_constructible_v<ark::buffer>);
static_assert(std::is_nothrow_move_assignable_v<ark::buffer>);

namespace {

std::string contents(ark::buffer& value) {
    return value.get_length() ? std::string(value.get_data(), value.get_length()) : std::string{};
}

} // namespace

TEST_CASE("Buffer retains default explicit and zero capacities with empty writes") {
    ark::buffer defaults;
    CHECK(defaults.get_capacity() == 512 * 1024);
    CHECK(defaults.get_length() == 0);
    ark::buffer configured(7);
    CHECK(configured.get_capacity() == 7);
    CHECK(configured.get_length() == 0);
    CHECK(configured.write(nullptr, 0));
    CHECK(configured.get_length() == 0);
    CHECK_FALSE(configured.write(nullptr, 1));
    CHECK(configured.get_length() == 0);
    ark::buffer zero(0);
    CHECK(zero.get_capacity() == 0);
    CHECK(zero.get_data() == nullptr);
    CHECK(zero.write(nullptr, 0));
    CHECK(zero.get_capacity() == 0);
    REQUIRE(zero.write("abc", 3));
    CHECK(zero.get_capacity() == 3);
    CHECK(contents(zero) == "abc");
}

TEST_CASE("Buffer compacts unread binary bytes without growing") {
    ark::buffer value(8);
    const char bytes[] = {'a', 'b', '\0', 'd', 'e', 'f'};
    REQUIRE(value.write(bytes, sizeof(bytes)));
    const auto* allocation = value.get_data();
    value.remove_data(2);
    REQUIRE(value.get_length() == 4);
    REQUIRE(value.write("XY", 2));
    CHECK(value.get_capacity() == 8);
    value.remove_data(2);
    REQUIRE(value.write("1234", 4));
    CHECK(value.get_data() == allocation);
    CHECK(value.get_capacity() == 8);
    CHECK(contents(value) == "efXY1234");
    value.remove_data(8);
    CHECK(value.get_length() == 0);
    CHECK(value.write("z", 1));
    CHECK(value.get_capacity() == 8);
    CHECK(contents(value) == "z");
}

TEST_CASE("Buffer growth retains unread bytes and existing capacity selection") {
    ark::buffer value(4);
    REQUIRE(value.write("abc", 3));
    value.remove_data(2);
    REQUIRE(value.write("defg", 4));
    CHECK(contents(value) == "cdefg");
    CHECK(value.get_capacity() == 8);
    value.remove_data(5);
    REQUIRE(value.write("123456789", 9));
    CHECK(contents(value) == "123456789");
    CHECK(value.get_capacity() == 16);
    value.remove_data(8);
    REQUIRE(value.write("abcdef", 6));
    CHECK(contents(value) == "9abcdef");
    CHECK(value.get_capacity() == 16);
}

TEST_CASE("Buffer move construction transfers unread state and leaves a reusable source") {
    ark::buffer source(8);
    REQUIRE(source.write("abcdef", 6));
    source.remove_data(2);
    const auto* unread = source.get_data();
    ark::buffer destination(std::move(source));
    CHECK(destination.get_data() == unread);
    CHECK(destination.get_capacity() == 8);
    CHECK(contents(destination) == "cdef");
    CHECK(source.get_capacity() == 0);
    CHECK(source.get_length() == 0);
    CHECK(source.get_data() == nullptr);
    CHECK(source.write(nullptr, 0));
    source.remove_data(std::numeric_limits<size_t>::max());
    CHECK(source.get_length() == 0);
    REQUIRE(source.write("reuse", 5));
    CHECK(contents(source) == "reuse");
    CHECK(contents(destination) == "cdef");
}

TEST_CASE("Buffer move assignment replaces ownership and self move preserves contents") {
    ark::buffer source(12);
    REQUIRE(source.write("source", 6));
    source.remove_data(1);
    const auto* unread = source.get_data();
    ark::buffer destination(4);
    REQUIRE(destination.write("old", 3));
    destination = std::move(source);
    CHECK(destination.get_data() == unread);
    CHECK(destination.get_capacity() == 12);
    CHECK(contents(destination) == "ource");
    CHECK(source.get_capacity() == 0);
    CHECK(source.get_length() == 0);
    CHECK(source.get_data() == nullptr);
    auto* same = &destination;
    destination = std::move(*same);
    CHECK(destination.get_data() == unread);
    CHECK(contents(destination) == "ource");
    REQUIRE(source.write("new", 3));
    CHECK(contents(source) == "new");
    ark::buffer empty(std::move(source));
    source = std::move(destination);
    CHECK(contents(source) == "ource");
    destination = std::move(source);
    CHECK(contents(destination) == "ource");
    CHECK(source.get_length() == 0);
    CHECK(contents(empty) == "new");
}

TEST_CASE("Buffer assignment from an empty moved source releases the previous allocation") {
    ark::buffer survivor(2);
    {
        ark::buffer source(8);
        REQUIRE(source.write("kept", 4));
        survivor = std::move(source);
    }
    CHECK(contents(survivor) == "kept");
    ark::buffer source(std::move(survivor));
    source = std::move(survivor);
    CHECK(source.get_capacity() == 0);
    CHECK(source.get_length() == 0);
    CHECK(source.get_data() == nullptr);
    REQUIRE(source.write("again", 5));
    CHECK(contents(source) == "again");
}

TEST_CASE("Buffer owning storage survives container relocation") {
    std::vector<ark::buffer> values;
    values.emplace_back(4);
    REQUIRE(values.front().write("first", 5));
    const auto* stored = values.front().get_data();
    values.reserve(values.capacity() + 1);
    CHECK(values.front().get_data() == stored);
    CHECK(contents(values.front()) == "first");
}

TEST_CASE("Buffer rejects invalid consumption and overflowing growth without mutating state") {
    ark::buffer value(8);
    REQUIRE(value.write("abc", 3));
    value.remove_data(4);
    CHECK(contents(value) == "abc");
    value.remove_data(1);
    const auto* unread = value.get_data();
    value.remove_data(std::numeric_limits<size_t>::max());
    CHECK(value.get_data() == unread);
    CHECK(contents(value) == "bc");
    CHECK_THROWS_AS(value.write("x", std::numeric_limits<size_t>::max()), std::length_error);
    CHECK(value.get_data() == unread);
    CHECK(contents(value) == "bc");
    CHECK(value.get_capacity() == 8);
    REQUIRE(value.write("de", 2));
    CHECK(contents(value) == "bcde");
}
