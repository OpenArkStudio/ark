// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <cstddef>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "../network/doctest.h"

#include <ark/container/array_map.hpp>
#include <ark/container/list.hpp>
#include <ark/container/map.hpp>
#include <ark/string_pod.hpp>

namespace {

struct counted_object
{
    explicit counted_object(int& destroyed)
        : destroyed_(destroyed) {
    }

    ~counted_object() {
        ++destroyed_;
    }

    int& destroyed_;
};

using owned_object_map = ark::object_map<int, counted_object>;
using owned_object_hashmap = ark::object_hashmap<int, counted_object>;
using shared_object_map = ark::shared_object_map<int, counted_object>;
using shared_object_hashmap = ark::shared_object_hashmap<int, counted_object>;

static_assert(!std::is_copy_constructible_v<owned_object_map>);
static_assert(!std::is_copy_assignable_v<owned_object_map>);
static_assert(std::is_move_constructible_v<owned_object_map>);
static_assert(std::is_move_assignable_v<owned_object_map>);
static_assert(std::is_nothrow_move_constructible_v<owned_object_map> ==
              std::is_nothrow_move_constructible_v<owned_object_map::map_type>);
static_assert(std::is_nothrow_move_assignable_v<owned_object_map> ==
              std::is_nothrow_move_assignable_v<owned_object_map::map_type>);
static_assert(!std::is_copy_constructible_v<owned_object_hashmap>);
static_assert(!std::is_copy_assignable_v<owned_object_hashmap>);
static_assert(std::is_move_constructible_v<owned_object_hashmap>);
static_assert(std::is_move_assignable_v<owned_object_hashmap>);
static_assert(std::is_nothrow_move_constructible_v<owned_object_hashmap> ==
              std::is_nothrow_move_constructible_v<owned_object_hashmap::map_type>);
static_assert(std::is_nothrow_move_assignable_v<owned_object_hashmap> ==
              std::is_nothrow_move_assignable_v<owned_object_hashmap::map_type>);
static_assert(std::is_copy_constructible_v<shared_object_map>);
static_assert(std::is_copy_assignable_v<shared_object_map>);
static_assert(std::is_move_constructible_v<shared_object_map>);
static_assert(std::is_move_assignable_v<shared_object_map>);
static_assert(std::is_nothrow_move_constructible_v<shared_object_map> ==
              std::is_nothrow_move_constructible_v<shared_object_map::map_type>);
static_assert(std::is_nothrow_move_assignable_v<shared_object_map> ==
              std::is_nothrow_move_assignable_v<shared_object_map::map_type>);
static_assert(std::is_copy_constructible_v<shared_object_hashmap>);
static_assert(std::is_copy_assignable_v<shared_object_hashmap>);
static_assert(std::is_move_constructible_v<shared_object_hashmap>);
static_assert(std::is_move_assignable_v<shared_object_hashmap>);
static_assert(std::is_nothrow_move_constructible_v<shared_object_hashmap> ==
              std::is_nothrow_move_constructible_v<shared_object_hashmap::map_type>);
static_assert(std::is_nothrow_move_assignable_v<shared_object_hashmap> ==
              std::is_nothrow_move_assignable_v<shared_object_hashmap::map_type>);

} // namespace

DOCTEST_TEST_CASE("List removal keeps the void API and removes all matching values under C++20") {
    ark::list<int> values;
    values.push_back(1);
    values.push_back(2);
    values.push_back(1);
    values.push_back(3);
    values.push_back(1);
    static_assert(std::is_void_v<decltype(values.remove(1))>);
    values.remove(1);
    DOCTEST_CHECK(values.size() == 2);
    const std::vector<int> expected{2, 3};
    DOCTEST_CHECK(std::vector<int>(values.begin(), values.end()) == expected);
    values.remove(99);
    DOCTEST_CHECK(values.size() == 2);
    values.remove(2);
    values.remove(3);
    DOCTEST_CHECK(values.empty());
    values.remove(1);
    DOCTEST_CHECK(values.empty());
}

DOCTEST_TEST_CASE("List reverse iterators delimit every value and the empty range") {
    ark::list<int> values;
    DOCTEST_CHECK(values.rbegin() == values.rend());
    values.push_back(1);
    values.push_back(2);
    values.push_back(3);
    const std::vector<int> expected{3, 2, 1};
    DOCTEST_CHECK(std::vector<int>(values.rbegin(), values.rend()) == expected);
    values.clear();
    DOCTEST_CHECK(values.rbegin() == values.rend());
}

DOCTEST_TEST_CASE("Object-map constant end iterators delimit all entries") {
    ark::shared_object_map<int, std::string> values;
    values.insert(1, std::make_shared<std::string>("one"));
    values.insert(2, std::make_shared<std::string>("two"));
    const auto& constant_values = values;
    DOCTEST_CHECK(constant_values.cbegin() != constant_values.cend());
    DOCTEST_CHECK(constant_values.cend() == constant_values.end());
    int count = 0;
    for (auto it = constant_values.cbegin(); it != constant_values.cend(); ++it)
    {
        ++count;
    }
    DOCTEST_CHECK(count == 2);
}

DOCTEST_TEST_CASE_TEMPLATE("Object maps release erased cleared and remaining objects exactly once", map_type,
    owned_object_map, owned_object_hashmap) {
    int destroyed = 0;
    {
        map_type values;
        values.insert(1, new counted_object(destroyed));
        values.insert(2, new counted_object(destroyed));
        DOCTEST_REQUIRE(values.erase(1));
        DOCTEST_REQUIRE(destroyed == 1);
        DOCTEST_REQUIRE_FALSE(values.erase(1));

        values.erase(values.find(2));
        DOCTEST_REQUIRE(destroyed == 2);
        DOCTEST_REQUIRE(values.empty());
        DOCTEST_CHECK(values.erase(values.end()) == values.end());
        values.insert(3, nullptr);
        DOCTEST_REQUIRE(values.erase(3));
        DOCTEST_CHECK(destroyed == 2);

        values.insert(4, new counted_object(destroyed));
        values.clear();
        DOCTEST_REQUIRE(values.empty());
        DOCTEST_REQUIRE(destroyed == 3);
        values.clear();
        DOCTEST_CHECK(destroyed == 3);

        values.insert(5, new counted_object(destroyed));
        auto rejected = std::make_unique<counted_object>(destroyed);
        DOCTEST_REQUIRE_FALSE(values.insert(5, rejected.get()).second);
        DOCTEST_CHECK(destroyed == 3);
        rejected.reset();
        DOCTEST_CHECK(destroyed == 4);
    }
    DOCTEST_CHECK(destroyed == 5);
}

DOCTEST_TEST_CASE_TEMPLATE("Object-map moves transfer entries and release overwritten destination objects", map_type,
    owned_object_map, owned_object_hashmap) {
    int destroyed = 0;
    int replaced = 0;
    int reused = 0;
    {
        map_type source;
        auto* owned = new counted_object(destroyed);
        source.insert(1, owned);
        source.insert(2, nullptr);
        map_type moved(std::move(source));
        DOCTEST_REQUIRE(source.empty());
        DOCTEST_REQUIRE(moved.size() == 2);
        DOCTEST_CHECK(moved.find_value(1) == owned);
        DOCTEST_CHECK(moved.find(2) != moved.end());
        DOCTEST_CHECK(moved.find_value(2) == nullptr);
        source.insert(3, new counted_object(reused));
        source.clear();
        DOCTEST_CHECK(reused == 1);

        map_type destination;
        destination.insert(9, new counted_object(replaced));
        destination = std::move(moved);
        DOCTEST_CHECK(replaced == 1);
        DOCTEST_REQUIRE(moved.empty());
        DOCTEST_REQUIRE(destination.size() == 2);
        DOCTEST_CHECK(destination.find_value(1) == owned);
        DOCTEST_CHECK(destination.find(2) != destination.end());
        auto& self = destination;
        destination = std::move(self);
        DOCTEST_REQUIRE(destination.size() == 2);
        DOCTEST_CHECK(destination.find_value(1) == owned);
        DOCTEST_CHECK(destroyed == 0);
    }
    DOCTEST_CHECK(destroyed == 1);
    DOCTEST_CHECK(replaced == 1);
    DOCTEST_CHECK(reused == 1);
}

DOCTEST_TEST_CASE_TEMPLATE("Shared object maps copy ownership and move entries while retaining external owners",
    map_type, shared_object_map, shared_object_hashmap) {
    int destroyed = 0;
    int replaced = 0;
    auto owner = std::make_shared<counted_object>(destroyed);
    {
        map_type values;
        values.insert(1, owner);
        map_type copied(values);
        DOCTEST_CHECK(owner.use_count() == 3);
        map_type assigned;
        assigned.insert(9, std::make_shared<counted_object>(replaced));
        assigned = copied;
        DOCTEST_CHECK(replaced == 1);
        DOCTEST_CHECK(owner.use_count() == 4);

        map_type moved(std::move(values));
        DOCTEST_REQUIRE(values.empty());
        DOCTEST_CHECK(moved.find_value(1) == owner);
        DOCTEST_CHECK(owner.use_count() == 4);
        values.insert(9, std::make_shared<counted_object>(replaced));
        values.clear();
        DOCTEST_CHECK(replaced == 2);
        map_type destination;
        destination.insert(9, std::make_shared<counted_object>(replaced));
        destination = std::move(moved);
        DOCTEST_REQUIRE(moved.empty());
        DOCTEST_CHECK(replaced == 3);
        DOCTEST_CHECK(destination.find_value(1) == owner);
        auto& self = destination;
        destination = std::move(self);
        DOCTEST_CHECK(destination.find_value(1) == owner);
        DOCTEST_CHECK(owner.use_count() == 4);

        DOCTEST_REQUIRE(copied.erase(1));
        assigned.clear();
        DOCTEST_CHECK(owner.use_count() == 2);
        DOCTEST_REQUIRE(destroyed == 0);
    }
    DOCTEST_CHECK(owner.use_count() == 1);
    owner.reset();
    DOCTEST_REQUIRE(destroyed == 1);
}

namespace {

struct pod_iterator_buckets : ark::string_traits<char>
{
    static std::size_t hash(const char* key) {
        return key[0] == 'z' ? 2 : 0;
    }
};

} // namespace

DOCTEST_TEST_CASE("POD string postfix values retain prior positions across chains and buckets") {
    ark::pod_string<char, std::size_t, pod_iterator_buckets> values(4);
    DOCTEST_REQUIRE(values.add("alpha", 11));
    DOCTEST_REQUIRE(values.add("beta", 22));
    DOCTEST_REQUIRE(values.add("zeta", 33));
    auto current = values.begin();
    DOCTEST_REQUIRE(current != values.end());
    const auto first = current++;
    DOCTEST_REQUIRE(current != values.end());
    const auto second = current++;
    DOCTEST_REQUIRE(current != values.end());
    DOCTEST_CHECK(std::string(current.get_key()) == "zeta");
    auto* prefix_result = &++current;
    DOCTEST_CHECK(prefix_result == &current);
    DOCTEST_CHECK(current == values.end());
    // Saved iterator values still name live nodes after the source reaches end.
    DOCTEST_CHECK(first == values.find("beta"));
    DOCTEST_CHECK(second == values.find("alpha"));
    DOCTEST_CHECK(std::string(first.get_key()) == "beta");
    DOCTEST_CHECK(*first.get_data() == 22);
    DOCTEST_CHECK(std::string(second.get_key()) == "alpha");
    DOCTEST_CHECK(*second.get_data() == 11);
}

DOCTEST_TEST_CASE("POD string iterator data points to mutable scalar storage") {
    ark::pod_string<char, std::size_t> values;
    DOCTEST_REQUIRE(values.add("count", 4));
    const auto found = values.find("count");
    DOCTEST_REQUIRE(found != values.end());
    auto* stored = found.get_data();
    DOCTEST_REQUIRE(stored == &found.get_node()->data);
    *stored = 19;
    std::size_t copy{};
    DOCTEST_REQUIRE(values.get_data("count", copy));
    DOCTEST_CHECK(copy == 19);
    DOCTEST_REQUIRE(values.set("count", 27));
    DOCTEST_CHECK(values.get_count() == 1);
    DOCTEST_CHECK(found.get_data() == stored);
    DOCTEST_CHECK(*stored == 27);
}

DOCTEST_TEST_CASE("POD string pointer mappings expose the stored pointer slot without owning the pointee") {
    int first = 5;
    int second = 8;
    {
        ark::pod_string<char, int*> values(2);
        DOCTEST_REQUIRE(values.add("slot", &first));
        auto found = values.find("slot");
        DOCTEST_REQUIRE(found != values.end());
        int** slot = found.get_data();
        DOCTEST_REQUIRE(slot == &found.get_node()->data);
        DOCTEST_CHECK(*slot == &first);
        *slot = &second;
        int* copy{};
        DOCTEST_REQUIRE(values.get_data("slot", copy));
        DOCTEST_CHECK(copy == &second);
        DOCTEST_REQUIRE(values.set("slot", nullptr));
        DOCTEST_CHECK(found.get_data() == slot);
        DOCTEST_CHECK(*slot == nullptr);
        const auto prior = found++;
        DOCTEST_CHECK(found == values.end());
        DOCTEST_CHECK(prior.get_data() == slot);
    }
    DOCTEST_CHECK(first == 5);
    DOCTEST_CHECK(second == 8);
}

DOCTEST_TEST_CASE("POD string empty and missing lookup paths reach end without dereferencing it") {
    for (const std::size_t buckets : {std::size_t{0}, std::size_t{4}})
    {
        ark::pod_string<char, std::size_t> values(buckets);
        DOCTEST_CHECK(values.begin() == values.end());
        DOCTEST_CHECK(values.find("missing") == values.end());
        DOCTEST_CHECK_FALSE(values.exists("missing"));
        std::size_t unchanged = 91;
        DOCTEST_CHECK_FALSE(values.get_data("missing", unchanged));
        DOCTEST_CHECK(unchanged == 91);
        DOCTEST_REQUIRE(values.add("only", 7));
        auto current = values.begin();
        DOCTEST_REQUIRE(current != values.end());
        const auto prior = current++;
        DOCTEST_CHECK(current == values.end());
        DOCTEST_CHECK(*prior.get_data() == 7);
    }
}

DOCTEST_TEST_CASE("Array map keeps its POD string index lookup and borrowed values") {
    int first = 12;
    int second = 24;
    {
        ark::array_map<std::string, int> values;
        DOCTEST_REQUIRE(values.add_element("first", &first));
        DOCTEST_REQUIRE(values.add_element("second", &second));
        DOCTEST_CHECK(values.get_count() == 2);
        DOCTEST_CHECK(values.get_element("first") == &first);
        DOCTEST_CHECK(values.get_element("second") == &second);
        DOCTEST_CHECK(values.get_element("missing") == nullptr);
        std::size_t index = 9;
        DOCTEST_REQUIRE(values.exist_element("second", index));
        DOCTEST_CHECK(index == 1);
        DOCTEST_CHECK(values[index] == &second);
    }
    DOCTEST_CHECK(first == 12);
    DOCTEST_CHECK(second == 24);
}
