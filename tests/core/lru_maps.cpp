// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <memory>
#include <stdexcept>
#include <utility>

#include "../network/doctest.h"

#include <ark/container/lru_map.hpp>

namespace {

struct checked_key
{
    int value{};
    static inline int copies_before_failure{-1};

    explicit checked_key(int value)
        : value(value) {
    }

    checked_key(const checked_key& other)
        : value(other.value) {
        if (copies_before_failure == 0)
        {
            throw std::runtime_error("key copy failed");
        }
        if (copies_before_failure > 0)
        {
            --copies_before_failure;
        }
    }

    checked_key(checked_key&&) = default;
    checked_key& operator=(const checked_key&) = default;
    bool operator==(const checked_key&) const = default;
};

} // namespace

template<>
struct std::hash<checked_key>
{
    std::size_t operator()(const checked_key& key) const noexcept {
        return std::hash<int>{}(key.value);
    }
};

TEST_CASE("LRU copy rebuilds its own recency links after the source is cleared") {
    ark::lru_map<int, int> source(3);
    source.insert(1, 10);
    source.insert(2, 20);
    source.insert(3, 30);
    REQUIRE(source.find(1) != source.end());
    ark::lru_map<int, int> copied(source);
    source.clear();
    REQUIRE(copied.find(2) != copied.end());
    copied.insert(4, 40);
    CHECK(copied.size() == 3);
    CHECK(copied.find(3) == copied.end());
    REQUIRE(copied.find(1) != copied.end());
    CHECK(copied.find(1)->second == 10);
    copied.erase(2);
    CHECK(copied.size() == 2);
}

TEST_CASE("LRU copy assignment replaces entries and preserves independent eviction order") {
    ark::lru_map<int, int> assigned(1);
    assigned.insert(9, 90);
    {
        ark::lru_map<int, int> source(2);
        source.insert(1, 10);
        source.insert(2, 20);
        assigned = source;
        source.erase(1);
    }
    CHECK(assigned.find(9) == assigned.end());
    REQUIRE(assigned.find(1) != assigned.end());
    assigned.insert(3, 30);
    CHECK(assigned.size() == 2);
    CHECK(assigned.find(2) == assigned.end());
    auto& self = assigned;
    assigned = self;
    REQUIRE(assigned.find(1) != assigned.end());
    assigned.insert(4, 40);
    CHECK(assigned.size() == 2);
    CHECK(assigned.find(3) == assigned.end());
}

TEST_CASE("LRU failed insertion preserves recency and can be followed by lookup erase and eviction") {
    bool saw_failure{};
    bool saw_success{};
    for (int limit = 0; limit < 12; ++limit)
    {
        ark::lru_map<checked_key, int> values(2);
        values.insert(checked_key{1}, 10);
        values.insert(checked_key{2}, 20);
        const checked_key candidate{3};
        checked_key::copies_before_failure = limit;
        bool caught{};
        try
        { values.insert(candidate, 30); }
        catch (const std::runtime_error&)
        { caught = true; }
        checked_key::copies_before_failure = -1;
        if (caught)
        {
            saw_failure = true;
            REQUIRE(values.size() == 2);
            CHECK(values.find(candidate) == values.end());
            values.insert(checked_key{4}, 40);
            CHECK(values.find(checked_key{1}) == values.end());
            REQUIRE(values.find(checked_key{2}) != values.end());
            values.erase(checked_key{2});
            CHECK(values.size() == 1);
        }
        else
        {
            saw_success = true;
            REQUIRE(values.size() == 2);
            CHECK(values.find(checked_key{1}) == values.end());
            REQUIRE(values.find(candidate) != values.end());
            CHECK(values.find(candidate)->second == 30);
            break;
        }
    }
    CHECK(saw_failure);
    CHECK(saw_success);
}

TEST_CASE("LRU key copy failure during assignment retains both caches and their eviction order") {
    bool saw_failure{};
    bool saw_success{};
    for (int limit = 0; limit < 12; ++limit)
    {
        const auto source_owner = std::make_shared<int>(10);
        ark::lru_map<checked_key, std::shared_ptr<int>> source(2);
        source.insert(checked_key{1}, source_owner);
        source.insert(checked_key{2}, std::make_shared<int>(20));
        REQUIRE(source.find(checked_key{1}) != source.end());
        const auto destination_owner = std::make_shared<int>(90);
        ark::lru_map<checked_key, std::shared_ptr<int>> destination(1);
        destination.insert(checked_key{9}, destination_owner);
        bool caught{};
        checked_key::copies_before_failure = limit;
        try
        { destination = source; }
        catch (const std::runtime_error&)
        { caught = true; }
        checked_key::copies_before_failure = -1;
        REQUIRE(source.size() == 2);
        if (caught)
        {
            saw_failure = true;
            REQUIRE(destination.size() == 1);
            REQUIRE(destination.find(checked_key{9}) != destination.end());
            CHECK(destination.find(checked_key{9})->second == destination_owner);
            CHECK(destination_owner.use_count() == 2);
            CHECK(source_owner.use_count() == 2);
            destination.insert(checked_key{8}, std::make_shared<int>(80));
            CHECK(destination.find(checked_key{9}) == destination.end());
            CHECK(destination.size() == 1);
            source.insert(checked_key{3}, std::make_shared<int>(30));
            CHECK(source.find(checked_key{2}) == source.end());
            REQUIRE(source.find(checked_key{1}) != source.end());
        }
        else
        {
            saw_success = true;
            CHECK(destination_owner.use_count() == 1);
            CHECK(source_owner.use_count() == 3);
            source.clear();
            destination.insert(checked_key{3}, std::make_shared<int>(30));
            CHECK(destination.size() == 2);
            CHECK(destination.find(checked_key{2}) == destination.end());
            REQUIRE(destination.find(checked_key{1}) != destination.end());
            break;
        }
    }
    CHECK(saw_failure);
    CHECK(saw_success);
}

TEST_CASE("LRU duplicate insertion retains the stored value and recency") {
    ark::lru_map<int, std::shared_ptr<int>> values(2);
    const auto first = std::make_shared<int>(10);
    values.insert(1, first);
    values.insert(2, std::make_shared<int>(20));
    const auto rejected = std::make_shared<int>(99);
    values.insert(1, rejected);
    CHECK(rejected.use_count() == 1);
    values.insert(3, std::make_shared<int>(30));
    CHECK(values.find(1) == values.end());
    CHECK(first.use_count() == 1);
    CHECK(values.size() == 2);
}

TEST_CASE("LRU swap keeps recency links with their values and capacities") {
    ark::lru_map<int, int> first(2);
    first.insert(1, 10);
    first.insert(2, 20);
    ark::lru_map<int, int> second(1);
    second.insert(9, 90);
    first.swap(second);
    first.insert(8, 80);
    CHECK(first.size() == 1);
    CHECK(first.find(9) == first.end());
    REQUIRE(second.find(1) != second.end());
    second.insert(3, 30);
    CHECK(second.size() == 2);
    CHECK(second.find(2) == second.end());
    second.erase(1);
    CHECK(second.size() == 1);
}
