// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <array>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include "../network/doctest.h"

#include <ark/container/vector.hpp>

namespace {

struct vector_object
{
    vector_object(int value, int& destroyed)
        : value(value)
        , destroyed(destroyed) {
    }

    ~vector_object() {
        ++destroyed;
    }

    int value;
    int& destroyed;
};

struct incomplete_object;

static_assert(std::is_same_v<ark::vector_value_type<int, false>::value_type, int*>);
static_assert(std::is_same_v<ark::vector_value_type<int, true>::value_type, std::shared_ptr<int>>);
static_assert(std::is_same_v<ark::vector_value_type<const int, false>::value_type, const int*>);
static_assert(std::is_same_v<ark::vector_value_type<const int, true>::value_type, std::shared_ptr<const int>>);
static_assert(std::is_same_v<ark::vector_value_type<incomplete_object, false>::value_type, incomplete_object*>);
static_assert(
    std::is_same_v<ark::vector_value_type<incomplete_object, true>::value_type, std::shared_ptr<incomplete_object>>);

using raw_objects = ark::pointer_vector<vector_object>;
using shared_objects = ark::shared_vector<vector_object>;

static_assert(!std::is_copy_constructible_v<raw_objects>);
static_assert(!std::is_copy_assignable_v<raw_objects>);
static_assert(std::is_nothrow_move_constructible_v<raw_objects>);
static_assert(std::is_nothrow_move_assignable_v<raw_objects>);
static_assert(std::is_copy_constructible_v<shared_objects>);
static_assert(std::is_copy_assignable_v<shared_objects>);
static_assert(std::is_nothrow_move_constructible_v<shared_objects>);
static_assert(std::is_nothrow_move_assignable_v<shared_objects>);
static_assert(std::is_same_v<raw_objects::vector_type, std::vector<vector_object*>>);
static_assert(std::is_same_v<shared_objects::vector_type, std::vector<std::shared_ptr<vector_object>>>);
static_assert(std::is_same_v<raw_objects::reference, vector_object*&>);
static_assert(std::is_same_v<shared_objects::reference, std::shared_ptr<vector_object>&>);
static_assert(std::is_same_v<raw_objects::iterator, raw_objects::vector_type::iterator>);
static_assert(std::is_same_v<shared_objects::const_iterator, shared_objects::vector_type::const_iterator>);
static_assert(std::is_same_v<raw_objects::reverse_iterator, raw_objects::vector_type::reverse_iterator>);
static_assert(
    std::is_same_v<shared_objects::const_reverse_iterator, shared_objects::vector_type::const_reverse_iterator>);
static_assert(std::is_same_v<ark::value_vector<int>, std::vector<int>>);

} // namespace

DOCTEST_TEST_CASE("Pointer vectors release replacement erased cleared and remaining objects exactly once") {
    std::array<int, 4> destroyed{};
    {
        raw_objects values;
        values.push_back(new vector_object(10, destroyed[0]));
        values.push_back(new vector_object(20, destroyed[1]));
        auto* same = values[0];
        values.push_back(0, same);
        DOCTEST_CHECK(destroyed[0] == 0);
        DOCTEST_CHECK(values.at(0) == same);

        values.push_back(0, new vector_object(30, destroyed[2]));
        DOCTEST_CHECK(destroyed[0] == 1);
        DOCTEST_CHECK(destroyed[1] == 0);
        DOCTEST_CHECK(values[0]->value == 30);
        values.erase(1);
        DOCTEST_CHECK(destroyed[1] == 1);
        DOCTEST_CHECK(values.size() == 2);
        DOCTEST_CHECK(values.at(1) == nullptr);
        values.erase(1);
        DOCTEST_CHECK(destroyed[1] == 1);

        values.clear();
        DOCTEST_CHECK(values.empty());
        DOCTEST_CHECK(destroyed[2] == 1);
        values.clear();
        DOCTEST_CHECK(destroyed[2] == 1);
        values.push_back(new vector_object(40, destroyed[3]));
    }
    const std::array<int, 4> expected{1, 1, 1, 1};
    DOCTEST_CHECK(destroyed == expected);
}

DOCTEST_TEST_CASE("Pointer-vector moves transfer slots empty the source and release a populated destination") {
    std::array<int, 4> destroyed{};
    {
        raw_objects source;
        source.resize(3);
        source.push_back(0, new vector_object(10, destroyed[0]));
        source.push_back(2, new vector_object(20, destroyed[1]));
        auto* first = source[0];
        auto* last = source[2];
        raw_objects moved(std::move(source));
        DOCTEST_CHECK(source.empty());
        DOCTEST_CHECK(moved.size() == 3);
        DOCTEST_CHECK(moved[0] == first);
        DOCTEST_CHECK(moved[1] == nullptr);
        DOCTEST_CHECK(moved[2] == last);
        source.push_back(new vector_object(30, destroyed[2]));
        source.clear();
        DOCTEST_CHECK(destroyed[2] == 1);

        raw_objects destination;
        destination.push_back(new vector_object(40, destroyed[3]));
        destination = std::move(moved);
        DOCTEST_CHECK(destroyed[3] == 1);
        DOCTEST_CHECK(moved.empty());
        DOCTEST_CHECK(destination.size() == 3);
        DOCTEST_CHECK(destination[0] == first);
        DOCTEST_CHECK(destination[2] == last);
        auto& self = destination;
        destination = std::move(self);
        DOCTEST_CHECK(destination.size() == 3);
        DOCTEST_CHECK(destination[0] == first);
        DOCTEST_CHECK(destination[1] == nullptr);
        DOCTEST_CHECK(destination[2] == last);
        DOCTEST_CHECK(destroyed[0] == 0);
        DOCTEST_CHECK(destroyed[1] == 0);
    }
    const std::array<int, 4> expected{1, 1, 1, 1};
    DOCTEST_CHECK(destroyed == expected);
}

DOCTEST_TEST_CASE("Shared-vector copies and replacement retain external owners and release only their references") {
    std::array<int, 3> destroyed{};
    {
        auto owner = std::make_shared<vector_object>(10, destroyed[0]);
        shared_objects values;
        values.push_back(owner);
        shared_objects copied(values);
        DOCTEST_CHECK(owner.use_count() == 3);
        values.push_back(0, std::make_shared<vector_object>(20, destroyed[1]));
        DOCTEST_CHECK(destroyed[0] == 0);
        DOCTEST_CHECK(copied[0] == owner);
        DOCTEST_CHECK(owner.use_count() == 2);

        shared_objects assigned;
        assigned.push_back(std::make_shared<vector_object>(30, destroyed[2]));
        assigned = copied;
        DOCTEST_CHECK(destroyed[2] == 1);
        DOCTEST_CHECK(assigned[0] == owner);
        DOCTEST_CHECK(owner.use_count() == 3);
        copied.erase(0);
        DOCTEST_CHECK(copied.size() == 1);
        DOCTEST_CHECK(copied[0] == nullptr);
        DOCTEST_CHECK(owner.use_count() == 2);
        values.clear();
        DOCTEST_CHECK(destroyed[1] == 1);
        owner.reset();
        DOCTEST_CHECK(destroyed[0] == 0);
        assigned.clear();
        DOCTEST_CHECK(destroyed[0] == 1);
    }
    const std::array<int, 3> expected{1, 1, 1};
    DOCTEST_CHECK(destroyed == expected);
}

DOCTEST_TEST_CASE("Shared-vector moves preserve external references and safely release overwritten destinations") {
    std::array<int, 3> destroyed{};
    {
        auto owner = std::make_shared<vector_object>(10, destroyed[0]);
        shared_objects source;
        source.push_back(owner);
        shared_objects moved(std::move(source));
        DOCTEST_CHECK(source.empty());
        DOCTEST_CHECK(moved[0] == owner);
        DOCTEST_CHECK(owner.use_count() == 2);
        source.push_back(std::make_shared<vector_object>(20, destroyed[1]));

        shared_objects destination;
        destination.push_back(std::make_shared<vector_object>(30, destroyed[2]));
        destination = std::move(moved);
        DOCTEST_CHECK(destroyed[2] == 1);
        DOCTEST_CHECK(moved.empty());
        DOCTEST_CHECK(destination[0] == owner);
        DOCTEST_CHECK(owner.use_count() == 2);
        auto& self = destination;
        destination = std::move(self);
        DOCTEST_CHECK(destination[0] == owner);
        DOCTEST_CHECK(owner.use_count() == 2);
        owner.reset();
        DOCTEST_CHECK(destroyed[0] == 0);
    }
    const std::array<int, 3> expected{1, 1, 1};
    DOCTEST_CHECK(destroyed == expected);
}

DOCTEST_TEST_CASE_TEMPLATE("Object-vector slots and const forward and reverse iteration preserve their contracts",
    vector_type, ark::pointer_vector<int>, ark::shared_vector<int>) {
    auto make_value = [](int value) {
        if constexpr (std::is_same_v<typename vector_type::value_type, int*>)
        {
            return new int(value);
        }
        else
        {
            return std::make_shared<int>(value);
        }
    };
    vector_type values;
    const auto& observed = values;
    DOCTEST_CHECK(observed.cbegin() == observed.cend());
    DOCTEST_CHECK(observed.crbegin() == observed.crend());
    DOCTEST_CHECK(values.begin() == values.end());
    DOCTEST_CHECK(values.rbegin() == values.rend());
    values.resize(3);
    DOCTEST_CHECK(values.size() == 3);
    DOCTEST_CHECK(values.first_empty() == 0);
    values.push_back(make_value(1));
    values.push_back(2, make_value(3));
    DOCTEST_CHECK(values.first_empty() == 1);
    values.push_back(make_value(2));
    DOCTEST_CHECK(values.first_empty() == 3);
    DOCTEST_CHECK(values.at(99) == nullptr);
    values.resize(1);
    DOCTEST_CHECK(values.size() == 3);

    std::vector<int> forward;
    for (auto it = observed.cbegin(); it != observed.cend(); ++it)
    {
        forward.push_back(**it);
    }
    const std::vector<int> expected_forward{1, 2, 3};
    DOCTEST_CHECK(forward == expected_forward);
    std::vector<int> reverse;
    for (auto it = observed.crbegin(); it != observed.crend(); ++it)
    {
        reverse.push_back(**it);
    }
    const std::vector<int> expected_reverse{3, 2, 1};
    DOCTEST_CHECK(reverse == expected_reverse);

    values.erase(1);
    DOCTEST_CHECK(values.size() == 3);
    DOCTEST_CHECK(values.at(1) == nullptr);
    DOCTEST_CHECK(values.first_empty() == 1);
    values.push_back(make_value(4));
    DOCTEST_CHECK(*values[1] == 4);
    values.push_back(99, make_value(5));
    DOCTEST_CHECK(values.size() == 4);
    DOCTEST_CHECK(*values[3] == 5);
    DOCTEST_CHECK(values.first_empty() == 4);
    values.clear();
    DOCTEST_CHECK(values.empty());
    DOCTEST_CHECK(observed.cbegin() == observed.cend());
    DOCTEST_CHECK(observed.crbegin() == observed.crend());
}

#ifdef NDEBUG
DOCTEST_TEST_CASE("Object-vector invalid indices retain the release fallback") {
    ark::pointer_vector<int> values;
    values.push_back(new int(7));
    DOCTEST_CHECK(values[99] == nullptr);
    values.erase(99);
    DOCTEST_CHECK(values.size() == 1);
    DOCTEST_CHECK(*values[0] == 7);
}
#endif
