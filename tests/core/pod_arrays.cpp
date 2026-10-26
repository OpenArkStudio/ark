// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <new>
#include <type_traits>

#include "../network/doctest.h"

#include <ark/array_pod.hpp>

namespace {

enum class failure_mode
{
    none,
    null_result,
    exception
};

struct controlled_allocator
{
    static inline failure_mode failure{};
    static inline size_t allocations{};
    static inline size_t releases{};
    static inline size_t live{};
    static inline size_t last_bytes{};

    void* alloc(size_t bytes) {
        last_bytes = bytes;
        const auto mode = failure;
        failure = failure_mode::none;
        if (mode == failure_mode::null_result)
        {
            return nullptr;
        }
        if (mode == failure_mode::exception)
        {
            throw std::bad_alloc{};
        }
        auto* result = std::malloc(bytes);
        if (!result)
        {
            throw std::bad_alloc{};
        }
        ++allocations;
        ++live;
        return result;
    }

    void free(void* value, size_t) {
        std::free(value);
        ++releases;
        --live;
    }

    void swap(controlled_allocator&) {
    }
};

using controlled_array = ark::pod_array<uint64_t, 1, controlled_allocator>;
static_assert(std::is_void_v<decltype(std::declval<controlled_array&>().push_back(1))>);
static_assert(std::is_void_v<decltype(std::declval<controlled_array&>().reserve(1))>);
static_assert(std::is_void_v<decltype(std::declval<controlled_array&>().resize(1))>);

} // namespace

TEST_CASE("POD array scalar aliases survive heap growth and insertion shifts") {
    ark::pod_array<int, 1> values;
    values.push_back(11);
    values.push_back(values[0]); // Inline storage survives its first growth too.
    values[1] = 22;
    values.push_back(values[0]);
    REQUIRE(values.size() == 3);
    CHECK(values[2] == 11);
    values.push_back(values[1]); // No growth.
    CHECK(values[3] == 22);
    values.resize(6, values[1]);
    CHECK(values[4] == 22);
    CHECK(values[5] == 22);

    ark::pod_array<int, 1> growing;
    growing.push_back(10);
    growing.push_back(20);
    growing.insert(0, growing[1]);
    REQUIRE(growing.size() == 3);
    CHECK(growing[0] == 20);
    CHECK(growing[1] == 10);
    CHECK(growing[2] == 20);

    growing.reserve(8);
    growing.insert(0, growing[2]);
    REQUIRE(growing.size() == 4);
    CHECK(growing[0] == 20);
    CHECK(growing[1] == 20);
    CHECK(growing[2] == 10);
    CHECK(growing[3] == 20);
    growing.insert(growing.size(), growing[2]);
    CHECK(growing.back() == 10);
}

TEST_CASE("POD array pointer aliases retain borrowed pointees") {
    int first = 10;
    int second = 20;
    {
        ark::pod_array<int*, 1> values;
        values.push_back(&first);
        values.push_back(&second);
        values.push_back(values[0]);
        CHECK(values.back() == &first);
        values.resize(5, values[1]);
        CHECK(values[3] == &second);
        CHECK(values[4] == &second);
        values.insert(0, values[2]);
        CHECK(values[0] == &first);
        values.clear();
        CHECK(values.empty());
    }
    CHECK(first == 10);
    CHECK(second == 20);
}

TEST_CASE("POD array impossible byte counts fail before allocation and preserve state") {
    REQUIRE(controlled_allocator::live == 0);
    const auto initial_allocations = controlled_allocator::allocations;
    {
        controlled_array values;
        values.push_back(42);
        const auto* original = values.data();
        const auto impossible = std::numeric_limits<size_t>::max() / sizeof(uint64_t) + 1;
        CHECK_THROWS_AS(values.reserve(impossible), std::length_error);
        CHECK_THROWS_AS(values.resize(impossible), std::length_error);
        CHECK_THROWS_AS(values.resize(impossible, values[0]), std::length_error);
        CHECK(controlled_allocator::allocations == initial_allocations);
        CHECK(values.data() == original);
        REQUIRE(values.size() == 1);
        CHECK(values[0] == 42);
        values.push_back(values[0]);
        CHECK(values.back() == 42);
    }
    CHECK(controlled_allocator::live == 0);
}

TEST_CASE("POD array null and throwing allocations retain storage size and values") {
    REQUIRE(controlled_allocator::live == 0);
    const auto initial_allocations = controlled_allocator::allocations;
    const auto initial_releases = controlled_allocator::releases;
    for (const auto failure : {failure_mode::null_result, failure_mode::exception})
    {
        for (int operation = 0; operation != 7; ++operation)
        {
            INFO("failure=", static_cast<int>(failure), " operation=", operation);
            controlled_array values;
            values.push_back(11);
            values.push_back(22);
            controlled_array destination;
            destination.push_back(90);
            destination.push_back(91);
            const auto* original = values.data();
            const auto* destination_original = destination.data();
            const auto releases = controlled_allocator::releases;
            controlled_allocator::failure = failure;
            const auto fail_operation = [&] {
                switch (operation)
                {
                case 0:
                    values.push_back(values[0]);
                    break;
                case 1:
                    values.reserve(5);
                    break;
                case 2:
                    values.resize(5);
                    break;
                case 3:
                    values.resize(5, values[0]);
                    break;
                case 4:
                    values.insert(0, values[1]);
                    break;
                case 5:
                {
                    controlled_array copy(values);
                    break;
                }
                case 6:
                    destination = values;
                    break;
                }
            };
            CHECK_THROWS_AS(fail_operation(), std::bad_alloc);
            CHECK(controlled_allocator::failure == failure_mode::none);
            CHECK(controlled_allocator::releases == releases);
            CHECK(values.data() == original);
            REQUIRE(values.size() == 2);
            CHECK(values[0] == 11);
            CHECK(values[1] == 22);
            CHECK(destination.data() == destination_original);
            REQUIRE(destination.size() == 2);
            CHECK(destination[0] == 90);
            CHECK(destination[1] == 91);
            values.push_back(values[0]);
            CHECK(values.back() == 11);
        }
    }
    CHECK(controlled_allocator::live == 0);
    CHECK(controlled_allocator::allocations - initial_allocations == controlled_allocator::releases - initial_releases);
}

TEST_CASE("POD array copying assignment swap and empty reuse keep independent storage") {
    REQUIRE(controlled_allocator::live == 0);
    {
        controlled_array empty;
        empty.reserve(0);
        empty.resize(0);
        controlled_array empty_copy(empty);
        CHECK(empty_copy.empty());
        controlled_array values;
        values.push_back(1);
        values.push_back(2);
        values.push_back(3);
        controlled_array copy(values);
        REQUIRE(copy.size() == 3);
        CHECK(copy.data() != values.data());
        copy[0] = 9;
        CHECK(values[0] == 1);
        empty = copy;
        CHECK(empty[0] == 9);
        auto* self = &empty;
        empty = *self;
        CHECK(empty[2] == 3);
        controlled_array small;
        small.push_back(7);
        small.swap(empty);
        CHECK(small.size() == 3);
        CHECK(empty.size() == 1);
        CHECK(empty[0] == 7);
        small.pop_back();
        CHECK(small.back() == 2);
        small.clear();
        small.push_back(8);
        CHECK(small.size() == 1);
        CHECK(small[0] == 8);
    }
    CHECK(controlled_allocator::live == 0);
}
