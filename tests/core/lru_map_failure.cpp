// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <cstdlib>
#include <memory>
#include <new>

#include "../network/doctest.h"

#include <ark/container/lru_map.hpp>

namespace {

int allocations_before_failure{-1};

} // namespace

void* operator new(std::size_t size) {
    if (allocations_before_failure == 0)
    {
        allocations_before_failure = -1;
        throw std::bad_alloc{};
    }
    if (allocations_before_failure > 0)
    {
        --allocations_before_failure;
    }
    if (auto* result = std::malloc(size ? size : 1))
    {
        return result;
    }
    throw std::bad_alloc{};
}

void operator delete(void* value) noexcept {
    std::free(value);
}

void operator delete(void* value, std::size_t) noexcept {
    std::free(value);
}

TEST_CASE("LRU allocation failure rolls back insertion and releases candidate value owners") {
    bool saw_failure{};
    bool saw_success{};
    for (int limit = 0; limit < 64; ++limit)
    {
        ark::lru_map<int, std::shared_ptr<int>> values(2);
        values.insert(1, std::make_shared<int>(10));
        values.insert(2, std::make_shared<int>(20));
        const auto candidate = std::make_shared<int>(30);
        bool caught{};
        allocations_before_failure = limit;
        try
        { values.insert(3, candidate); }
        catch (const std::bad_alloc&)
        { caught = true; }
        allocations_before_failure = -1;
        REQUIRE(values.size() == 2);
        if (caught)
        {
            saw_failure = true;
            CHECK(candidate.use_count() == 1);
            CHECK(values.find(3) == values.end());
            values.insert(4, std::make_shared<int>(40));
            CHECK(values.find(1) == values.end());
            REQUIRE(values.find(2) != values.end());
            values.erase(2);
            CHECK(values.size() == 1);
        }
        else
        {
            saw_success = true;
            CHECK(candidate.use_count() == 2);
            CHECK(values.find(1) == values.end());
            REQUIRE(values.find(3) != values.end());
            break;
        }
    }
    CHECK(saw_failure);
    CHECK(saw_success);
}

TEST_CASE("LRU allocation failure during copy assignment retains both caches and their owners") {
    bool saw_failure{};
    bool saw_success{};
    for (int limit = 0; limit < 64; ++limit)
    {
        const auto source_owner = std::make_shared<int>(10);
        ark::lru_map<int, std::shared_ptr<int>> source(2);
        source.insert(1, source_owner);
        source.insert(2, std::make_shared<int>(20));
        const auto destination_owner = std::make_shared<int>(90);
        ark::lru_map<int, std::shared_ptr<int>> destination(1);
        destination.insert(9, destination_owner);
        bool caught{};
        allocations_before_failure = limit;
        try
        { destination = source; }
        catch (const std::bad_alloc&)
        { caught = true; }
        allocations_before_failure = -1;
        REQUIRE(source.size() == 2);
        if (caught)
        {
            saw_failure = true;
            REQUIRE(destination.size() == 1);
            REQUIRE(destination.find(9) != destination.end());
            CHECK(destination.find(9)->second == destination_owner);
            CHECK(destination_owner.use_count() == 2);
            CHECK(source_owner.use_count() == 2);
            destination.insert(8, std::make_shared<int>(80));
            CHECK(destination.find(9) == destination.end());
            CHECK(destination.size() == 1);
        }
        else
        {
            saw_success = true;
            CHECK(destination_owner.use_count() == 1);
            CHECK(source_owner.use_count() == 3);
            source.clear();
            REQUIRE(destination.find(1) != destination.end());
            destination.insert(3, std::make_shared<int>(30));
            CHECK(destination.size() == 2);
            CHECK(destination.find(2) == destination.end());
            break;
        }
    }
    CHECK(saw_failure);
    CHECK(saw_success);
}
