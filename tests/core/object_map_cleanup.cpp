// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <compare>
#include <cstddef>
#include <functional>
#include <memory>
#include <stdexcept>

#include "../network/doctest.h"

#include <ark/container/map.hpp>

namespace {

struct guarded_key
{
    int value{};
    static inline int copies_before_failure{-1};
    static inline int copies{};

    explicit guarded_key(int value)
        : value(value) {
    }

    guarded_key(const guarded_key& other)
        : value(other.value) {
        if (copies_before_failure == 0)
        {
            throw std::runtime_error("key copy failed");
        }
        if (copies_before_failure > 0)
        {
            --copies_before_failure;
        }
        ++copies;
    }

    guarded_key(guarded_key&&) = default;
    guarded_key& operator=(const guarded_key&) = default;
    auto operator<=>(const guarded_key&) const = default;
};

struct cleanup_value
{
    explicit cleanup_value(int& destroyed)
        : destroyed(destroyed) {
    }

    ~cleanup_value() {
        ++destroyed;
    }

    int& destroyed;
};

} // namespace

template<>
struct std::hash<guarded_key>
{
    std::size_t operator()(const guarded_key& key) const noexcept {
        return std::hash<int>{}(key.value);
    }
};

namespace {

using owned_map = ark::object_map<guarded_key, cleanup_value>;
using owned_hashmap = ark::object_hashmap<guarded_key, cleanup_value>;
using shared_map = ark::shared_object_map<guarded_key, cleanup_value>;
using shared_hashmap = ark::shared_object_hashmap<guarded_key, cleanup_value>;
using borrowed_map = ark::object_map_base<guarded_key, std::reference_wrapper<cleanup_value>>;
using borrowed_hashmap = ark::object_map_base<guarded_key, std::reference_wrapper<cleanup_value>, true>;

} // namespace

TEST_CASE_TEMPLATE("Owning object maps clear entries without copying keys or retaining dangling values", map_type,
    owned_map, owned_hashmap) {
    int destroyed{};
    map_type values;
    values.insert(guarded_key{1}, new cleanup_value(destroyed));
    values.insert(guarded_key{2}, new cleanup_value(destroyed));
    const auto copies = guarded_key::copies;
    bool caught{};
    guarded_key::copies_before_failure = 1;
    try
    { values.clear(); }
    catch (const std::runtime_error&)
    { caught = true; }
    guarded_key::copies_before_failure = -1;
    CHECK_FALSE(caught);
    CHECK(guarded_key::copies == copies);
    CHECK(values.empty());
    CHECK(destroyed == 2);
    values.clear();
    CHECK(destroyed == 2);
}

TEST_CASE_TEMPLATE("Owning object-map destruction does not copy keys", map_type, owned_map, owned_hashmap) {
    int destroyed{};
    int copies{};
    {
        map_type values;
        values.insert(guarded_key{1}, new cleanup_value(destroyed));
        values.insert(guarded_key{2}, new cleanup_value(destroyed));
        copies = guarded_key::copies;
        guarded_key::copies_before_failure = 0;
    }
    guarded_key::copies_before_failure = -1;
    CHECK(guarded_key::copies == copies);
    CHECK(destroyed == 2);
}

TEST_CASE_TEMPLATE(
    "Shared object maps release only their value owners without copying keys", map_type, shared_map, shared_hashmap) {
    int destroyed{};
    auto external = std::make_shared<cleanup_value>(destroyed);
    {
        map_type values;
        values.insert(guarded_key{1}, external);
        CHECK(external.use_count() == 2);
        const auto copies = guarded_key::copies;
        bool caught{};
        guarded_key::copies_before_failure = 0;
        try
        { values.clear(); }
        catch (const std::runtime_error&)
        { caught = true; }
        guarded_key::copies_before_failure = -1;
        CHECK_FALSE(caught);
        CHECK(guarded_key::copies == copies);
        CHECK(values.empty());
        CHECK(external.use_count() == 1);
        CHECK(destroyed == 0);
        values.insert(guarded_key{2}, external);
        guarded_key::copies_before_failure = 0;
    }
    guarded_key::copies_before_failure = -1;
    CHECK(external.use_count() == 1);
    CHECK(destroyed == 0);
    external.reset();
    CHECK(destroyed == 1);
}

TEST_CASE_TEMPLATE("Object maps with reference values retain borrowed objects during cleanup", map_type, borrowed_map,
    borrowed_hashmap) {
    int destroyed{};
    {
        cleanup_value borrowed(destroyed);
        {
            map_type values;
            values.insert(guarded_key{1}, std::ref(borrowed));
            const auto copies = guarded_key::copies;
            bool caught{};
            guarded_key::copies_before_failure = 0;
            try
            { values.clear(); }
            catch (const std::runtime_error&)
            { caught = true; }
            guarded_key::copies_before_failure = -1;
            CHECK_FALSE(caught);
            CHECK(guarded_key::copies == copies);
            CHECK(values.empty());
            CHECK(destroyed == 0);
            values.insert(guarded_key{2}, std::ref(borrowed));
            guarded_key::copies_before_failure = 0;
        }
        guarded_key::copies_before_failure = -1;
        CHECK(destroyed == 0);
    }
    CHECK(destroyed == 1);
}
