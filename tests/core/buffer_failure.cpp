// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <cstdlib>
#include <new>
#include <string>

#include "../network/doctest.h"

#include <ark/buffer.hpp>

namespace {

bool fail_next_array{};

} // namespace

// Fail one ordinary array allocation deterministically, without exhausting memory.
void* operator new[](std::size_t size) {
    if (fail_next_array)
    {
        fail_next_array = false;
        throw std::bad_alloc{};
    }
    if (auto* result = std::malloc(size ? size : 1))
    {
        return result;
    }
    throw std::bad_alloc{};
}

void operator delete[](void* value) noexcept {
    std::free(value);
}

void operator delete[](void* value, std::size_t) noexcept {
    std::free(value);
}

TEST_CASE("Buffer default construction allocation propagates bad_alloc") {
    fail_next_array = true;
    bool caught{};
    try
    { ark::buffer candidate(8); }
    catch (const std::bad_alloc&)
    { caught = true; }
    CHECK_FALSE(fail_next_array);
    CHECK(caught);
}

TEST_CASE("Buffer default growth allocation propagates bad_alloc and retains unread state") {
    ark::buffer value(4);
    REQUIRE(value.write("abc", 3));
    value.remove_data(1);
    const auto* unread = value.get_data();
    fail_next_array = true;
    bool caught{};
    try
    { value.write("defg", 4); }
    catch (const std::bad_alloc&)
    { caught = true; }
    CHECK_FALSE(fail_next_array);
    CHECK(caught);
    CHECK(value.get_capacity() == 4);
    CHECK(value.get_data() == unread);
    CHECK(value.get_length() == 2);
    CHECK(std::string(value.get_data(), value.get_length()) == "bc");
    REQUIRE(value.write("defg", 4));
    CHECK(std::string(value.get_data(), value.get_length()) == "bcdefg");
}
