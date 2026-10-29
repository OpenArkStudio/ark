// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <unordered_set>

#include "../network/doctest.h"

namespace {

std::unordered_set<void*> live;
bool fail_next{};
std::size_t successful_before_failure{};
std::size_t allocations{};
std::size_t releases{};

void* hook_allocate(std::size_t size) {
    if (fail_next && successful_before_failure)
    {
        --successful_before_failure;
    }
    else if (fail_next)
    {
        fail_next = false;
        return nullptr;
    }
    auto* pointer = std::malloc(size ? size : 1);
    if (!pointer)
    {
        throw std::bad_alloc{};
    }
    live.insert(pointer);
    ++allocations;
    return pointer;
}

void hook_free(void*& pointer) {
    if (pointer)
    {
        if (live.erase(pointer) != 1)
        {
            std::abort();
        }
        ++releases;
        std::free(pointer);
        pointer = nullptr;
    }
}

} // namespace

// This separate executable never links default-expanded data_impl definitions.
#define ARK_NEW_ARRAY_RET(element_type, size) return hook_allocate(size)
#define ARK_DELETE_ARRAY(element_type, pointer) hook_free(pointer)
#include <ark/kernel/data_impl.hpp>
#include <ark/kernel/data_list_impl.hpp>

TEST_CASE("Data values retain matching custom array allocation hooks through failed and successful replacement") {
    REQUIRE(live.empty());
    const auto initial_allocations = allocations;
    const auto initial_releases = releases;
    {
        ark::data_impl value(ark::data_type::dt_string, "original allocated value");
        const auto* original = value.get_string();
        fail_next = true;
        CHECK_THROWS_AS(value.set_string("replacement allocated value"), std::bad_alloc);
        CHECK_FALSE(fail_next);
        CHECK(value.get_string() == original);
        CHECK(std::string(value.get_string()) == "original allocated value");
        CHECK(live.size() == 1);
        value.set_string(value.get_string() + 1);
        CHECK(std::string(value.get_string()) == "riginal allocated value");
        CHECK(live.size() == 1);
        const char payload[] = "user\0bytes";
        fail_next = true;
        CHECK_THROWS_AS(value.set_user_data(payload, sizeof(payload)), std::bad_alloc);
        CHECK_FALSE(fail_next);
        CHECK(std::string(value.get_string()) == "riginal allocated value");
        value.set_user_data(payload, sizeof(payload));
        CHECK(live.size() == 1);
        ark::data_impl copy(value);
        CHECK(live.size() == 2);
        CHECK(copy.get_raw_user_data() != value.get_raw_user_data());
        std::size_t size{};
        const auto* copied = copy.get_user_data(size);
        CHECK(size == sizeof(payload));
        CHECK(std::memcmp(copied, payload, sizeof(payload)) == 0);
        value.set_string("abc");
        CHECK(live.size() == 1);
        CHECK(std::string(value.get_string()) == "abc");
    }
    CHECK(live.empty());
    CHECK(allocations - initial_allocations == releases - initial_releases);
}

TEST_CASE("DataList retains matching array hooks across failed buffer and index growth") {
    REQUIRE(live.empty());
    const auto initial_allocations = allocations;
    const auto initial_releases = releases;
    {
        ark::base_data_list<2, 16> values;
        REQUIRE(values.add_string("1234567"));
        const char payload[] = "binary\0payload";
        fail_next = true;
        CHECK_THROWS_AS(values.add_user_data(payload, sizeof(payload)), std::bad_alloc);
        CHECK_FALSE(fail_next);
        CHECK(values.get_count() == 1);
        CHECK(std::string(values.string(0)) == "1234567");
        CHECK(live.empty());
        REQUIRE(values.add_user_data(payload, sizeof(payload)));
        CHECK(live.size() == 1);
        fail_next = true;
        CHECK_THROWS_AS(values.add_int(42), std::bad_alloc);
        CHECK_FALSE(fail_next);
        CHECK(values.get_count() == 2);
        std::size_t size{};
        const auto* result = values.user_data(1, size);
        CHECK(size == sizeof(payload));
        CHECK(std::memcmp(result, payload, size) == 0);
        REQUIRE(values.add_int(42));
        CHECK(values.get_int(2) == 42);
        CHECK(live.size() == 2);
    }
    CHECK(live.empty());
    CHECK(allocations - initial_allocations == releases - initial_releases);
}

TEST_CASE("DataList array hooks release partial copies and preserve self assignment") {
    REQUIRE(live.empty());
    const auto initial_allocations = allocations;
    const auto initial_releases = releases;
    {
        using list = ark::base_data_list<1, 16>;
        list source;
        REQUIRE(source.add_int(17));
        REQUIRE(source.add_string("a copied value longer than the inline buffer"));
        REQUIRE(source.add_int(18));
        const auto original = live;
        for (bool through_interface : {false, true})
        {
            for (std::size_t position : {1u, 2u})
            {
                fail_next = true;
                successful_before_failure = position;
                const auto copy = [&] {
                    if (through_interface)
                    {
                        const ark::data_list& interface = source;
                        list destination(interface);
                    }
                    else
                    {
                        list destination(source);
                    }
                };
                CHECK_THROWS_AS(copy(), std::bad_alloc);
                CHECK_FALSE(fail_next);
                fail_next = false;
                successful_before_failure = 0;
                CHECK(live == original);
                // Recover only a red-regression constructor's unpublished buffers.
                for (auto it = live.begin(); it != live.end();)
                {
                    void* pointer = *it++;
                    if (!original.contains(pointer))
                    {
                        hook_free(pointer);
                    }
                }
            }
        }
        const auto* same = &source;
        CHECK_NOTHROW(source = *same);
        CHECK(source.get_count() == 3);
        CHECK(live == original);
        fail_next = true;
        successful_before_failure = 1;
        CHECK_THROWS_AS(source = *same, std::bad_alloc);
        CHECK_FALSE(fail_next);
        fail_next = false;
        successful_before_failure = 0;
        CHECK(source.get_count() == 3);
        CHECK(live == original);
    }
    CHECK(live.empty());
    CHECK(allocations - initial_allocations == releases - initial_releases);
}
