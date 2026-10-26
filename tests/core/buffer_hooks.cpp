// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <array>
#include <cstdlib>
#include <new>
#include <string>
#include <utility>

#include "../network/doctest.h"

namespace {

std::array<char*, 8> live{};
unsigned allocations{};
unsigned releases{};
bool return_null{};
bool throw_allocation{};

char* allocate_bytes(size_t size) {
    if (throw_allocation)
    {
        throw_allocation = false;
        throw std::bad_alloc{};
    }
    if (return_null)
    {
        return_null = false;
        return nullptr;
    }
    auto* result = static_cast<char*>(std::malloc(size ? size : 1));
    if (!result)
    {
        throw std::bad_alloc{};
    }
    for (auto& entry : live)
    {
        if (!entry)
        {
            entry = result;
            ++allocations;
            return result;
        }
    }
    std::abort();
}

void release_bytes(char*& value) noexcept {
    for (auto& entry : live)
    {
        if (entry == value)
        {
            entry = nullptr;
            std::free(value);
            value = nullptr;
            ++releases;
            return;
        }
    }
    std::abort();
}

} // namespace

// A separate executable prevents differently expanded inline definitions from mixing.
#define ARK_NEW_ARRAY(element_type, size) allocate_bytes(size)
#define ARK_DELETE_ARRAY(element_type, pointer) release_bytes(pointer)
#include <ark/buffer.hpp>

TEST_CASE("Buffer uses matching custom hooks for growth replacement moves and destruction") {
    const auto initial_allocations = allocations;
    const auto initial_releases = releases;
    {
        ark::buffer source(4);
        REQUIRE(source.write("abc", 3));
        source.remove_data(2);
        REQUIRE(source.write("defg", 4));
        CHECK(allocations == initial_allocations + 2);
        CHECK(releases == initial_releases + 1);
        CHECK(std::string(source.get_data(), source.get_length()) == "cdefg");
        ark::buffer destination(2);
        destination = std::move(source);
        CHECK(releases == initial_releases + 2);
        auto* same = &destination;
        destination = std::move(*same);
        CHECK(releases == initial_releases + 2);
        ark::buffer moved(std::move(destination));
        CHECK(allocations == initial_allocations + 3);
        CHECK(releases == initial_releases + 2);
        REQUIRE(source.write("r", 1));
        CHECK(std::string(moved.get_data(), moved.get_length()) == "cdefg");
    }
    CHECK(allocations - initial_allocations == 4);
    CHECK(releases - initial_releases == 4);
    for (const auto* entry : live)
    {
        CHECK(entry == nullptr);
    }
}

TEST_CASE("Buffer null custom allocation preserves the empty constructor and failed write states") {
    const auto initial_releases = releases;
    return_null = true;
    {
        ark::buffer empty(4);
        CHECK(empty.get_capacity() == 0);
        CHECK(empty.get_length() == 0);
        CHECK(empty.get_data() == nullptr);
        CHECK(empty.write(nullptr, 0));
        return_null = true;
        CHECK_FALSE(empty.write("x", 1));
        CHECK(empty.get_capacity() == 0);
        CHECK(empty.get_length() == 0);
        REQUIRE(empty.write("x", 1));
        CHECK(empty.get_capacity() == 1);
    }
    CHECK(releases == initial_releases + 1);
    {
        ark::buffer value(4);
        REQUIRE(value.write("abc", 3));
        value.remove_data(1);
        const auto* unread = value.get_data();
        return_null = true;
        CHECK_FALSE(value.write("defg", 4));
        CHECK(value.get_capacity() == 4);
        CHECK(value.get_data() == unread);
        CHECK(value.get_length() == 2);
        CHECK(std::string(value.get_data(), value.get_length()) == "bc");
        REQUIRE(value.write("defg", 4));
        CHECK(std::string(value.get_data(), value.get_length()) == "bcdefg");
    }
}

TEST_CASE("Buffer throwing custom growth allocation preserves state and matching release ownership") {
    const auto initial_allocations = allocations;
    const auto initial_releases = releases;
    {
        ark::buffer value(4);
        REQUIRE(value.write("abc", 3));
        value.remove_data(1);
        const auto* unread = value.get_data();
        throw_allocation = true;
        CHECK_THROWS_AS(value.write("defg", 4), std::bad_alloc);
        CHECK_FALSE(throw_allocation);
        CHECK(value.get_capacity() == 4);
        CHECK(value.get_data() == unread);
        CHECK(value.get_length() == 2);
        CHECK(std::string(value.get_data(), value.get_length()) == "bc");
        CHECK(allocations == initial_allocations + 1);
        CHECK(releases == initial_releases);
    }
    CHECK(releases == initial_releases + 1);
}
