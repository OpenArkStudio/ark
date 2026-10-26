// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <cstdlib>
#include <new>
#include <string>

#include "../network/doctest.h"

#include <ark/array_pod.hpp>
#include <ark/container/array_map.hpp>

namespace {

size_t fail_array_call{};
bool failure_consumed{};

void* allocate_array(size_t bytes) {
    if (fail_array_call && --fail_array_call == 0)
    {
        failure_consumed = true;
        throw std::bad_alloc{};
    }
    if (auto* result = std::malloc(bytes ? bytes : 1))
    {
        return result;
    }
    throw std::bad_alloc{};
}

void fail_array(size_t ordinal) {
    failure_consumed = false;
    fail_array_call = ordinal;
}

} // namespace

// Real default allocators keep their production definitions in this separate binary.
void* operator new[](std::size_t bytes) {
    return allocate_array(bytes);
}

void* operator new[](std::size_t bytes, const std::nothrow_t&) noexcept {
    try
    { return allocate_array(bytes); }
    catch (...)
    { return nullptr; }
}

void operator delete[](void* value) noexcept {
    std::free(value);
}

void operator delete[](void* value, std::size_t) noexcept {
    std::free(value);
}

void operator delete[](void* value, const std::nothrow_t&) noexcept {
    std::free(value);
}

TEST_CASE("POD array default allocation failure throws without replacing live storage") {
    ark::pod_array<int, 1> values;
    values.push_back(1);
    values.push_back(2);
    const auto* original = values.data();
    fail_array(1);
    bool caught{};
    try
    { values.push_back(values[0]); }
    catch (const std::bad_alloc&)
    { caught = true; }
    CHECK(failure_consumed);
    CHECK(caught);
    CHECK(values.data() == original);
    REQUIRE(values.size() == 2);
    CHECK(values[0] == 1);
    CHECK(values[1] == 2);
    values.push_back(values[0]);
    CHECK(values.back() == 1);
}

TEST_CASE("Array map rolls back pointer growth bucket and node allocation failures") {
    for (size_t ordinal = 1; ordinal <= 3; ++ordinal)
    {
        INFO("failed array allocation ordinal=", ordinal);
        int original = 11;
        int candidate = 22;
        ark::array_map<std::string, int> values;
        const std::string existing_name = "original";
        const std::string new_name = "candidate";
        REQUIRE(values.add_element(existing_name, &original));
        fail_array(ordinal); // Pointer array, index bucket expansion, then index node.
        bool caught{};
        try
        { values.add_element(new_name, &candidate); }
        catch (const std::bad_alloc&)
        { caught = true; }
        CHECK(failure_consumed);
        CHECK(caught);
        REQUIRE(values.get_count() == 1);
        CHECK(values[0] == &original);
        CHECK(values.get_element(existing_name) == &original);
        CHECK_FALSE(values.exist_element(new_name));
        CHECK(values.get_element(new_name) == nullptr);
        size_t index = 99;
        CHECK(values.exist_element(existing_name, index));
        CHECK(index == 0);
        REQUIRE(values.add_element(new_name, &candidate));
        REQUIRE(values.get_count() == 2);
        CHECK(values[1] == &candidate);
        CHECK(values.get_element(new_name) == &candidate);
        CHECK(values.exist_element(new_name, index));
        CHECK(index == 1);
        values.clear();
        CHECK(values.get_count() == 0);
        CHECK(original == 11);
        CHECK(candidate == 22);
    }
}

TEST_CASE("Array map failed duplicate insertion preserves the existing name and pointee") {
    int original = 7;
    int candidate = 8;
    ark::array_map<std::string, int> values;
    const std::string name = "same";
    REQUIRE(values.add_element(name, &original));
    fail_array(3); // Bucket expansion succeeds; allocating the duplicate node fails.
    bool caught{};
    try
    { values.add_element(name, &candidate); }
    catch (const std::bad_alloc&)
    { caught = true; }
    CHECK(failure_consumed);
    CHECK(caught);
    REQUIRE(values.get_count() == 1);
    CHECK(values.get_element(name) == &original);
    REQUIRE(values.add_element(name, &candidate));
    REQUIRE(values.get_count() == 2);
    CHECK(values[0] == &original);
    CHECK(values[1] == &candidate);
    CHECK(values.get_element(name) == &candidate); // Existing duplicate-name semantics.
}
