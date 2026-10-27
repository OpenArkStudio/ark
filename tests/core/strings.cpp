// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <string>
#include <unordered_map>
#include <vector>

#include "../network/doctest.h"

#include <ark/string.hpp>
#include <ark/string_utils.hpp>

DOCTEST_TEST_CASE("String utilities treat empty patterns as no change") {
    for (const uint32_t limit : {0U, 1U, 2U})
    {
        std::vector<std::string> parts{"existing"};
        ark::string_utils::split(parts, "abc", "", limit);
        DOCTEST_CHECK(parts == std::vector<std::string>{"existing", "abc"});
        ark::string_utils::split(parts, "", "", limit);
        DOCTEST_CHECK(parts.size() == 2);
        parts.clear();
        ark::string_utils::split(parts, std::string("abc"), "", limit);
        DOCTEST_CHECK(parts == std::vector<std::string>{"abc"});
        ark::string_utils::split(parts, std::string{}, "", limit);
        DOCTEST_CHECK(parts.size() == 1);

        for (const auto replacement : {"", "x"})
        {
            DOCTEST_CHECK(ark::string_utils::replace("abc", "", replacement, limit) == "abc");
            DOCTEST_CHECK(ark::string_utils::replace(std::string("abc"), "", replacement, limit) == "abc");
            DOCTEST_CHECK(ark::string_utils::replace("", "", replacement, limit).empty());
            DOCTEST_CHECK(ark::string_utils::replace(std::string{}, "", replacement, limit).empty());
        }
        const std::string binary("a\0b", 3);
        DOCTEST_CHECK(ark::string_utils::replace(binary, "", "x", limit) == binary);
    }
}

DOCTEST_TEST_CASE("String utility patterns retain split and replacement limits") {
    std::vector<std::string> parts;
    ark::string_utils::split(parts, "::a::::b::", "::");
    DOCTEST_CHECK(parts == std::vector<std::string>{"", "a", "", "b"});
    parts.clear();
    ark::string_utils::split(parts, std::string("a::b::c"), "::", 1);
    DOCTEST_CHECK(parts == std::vector<std::string>{"a", "b::c"});
    parts = {"existing"};
    ark::string_utils::split(parts, "a::b::c", "::", 2);
    DOCTEST_CHECK(parts == std::vector<std::string>{"existing", "a", "b::c"});
    parts.clear();
    ark::string_utils::split(parts, std::string("a\0b:c", 5), ':');
    DOCTEST_CHECK(parts == std::vector<std::string>{std::string("a\0b", 3), "c"});

    for (const auto text : {"xooxoox", "plain", ""})
    {
        const std::string input(text);
        DOCTEST_CHECK(ark::string_utils::replace(text, "oo", "ee") == ark::string_utils::replace(input, "oo", "ee"));
    }
    DOCTEST_CHECK(ark::string_utils::replace("xooxoox", "oo", "ee") == "xeexeex");
    DOCTEST_CHECK(ark::string_utils::replace(std::string("xooxoox"), "oo", "ee", 1) == "xeexoox");
    DOCTEST_CHECK(ark::string_utils::replace("aaaa", "aa", "") == "");
}

DOCTEST_TEST_CASE("Small strings preserve content through growth, copies and inline/heap swaps") {
    ark::small_string<char, 8> short_text("abc");
    ark::small_string<char, 8> long_text("a longer string");
    short_text.swap(long_text);
    DOCTEST_REQUIRE(std::string(short_text.c_str()) == "a longer string");
    DOCTEST_REQUIRE(std::string(long_text.c_str()) == "abc");
    auto copy = short_text;
    short_text.clear();
    DOCTEST_REQUIRE(copy.size() == 15);
    DOCTEST_REQUIRE(std::string(copy.c_str()) == "a longer string");
    DOCTEST_REQUIRE(short_text.empty());
    short_text.append("12345678");
    short_text.append('9');
    DOCTEST_REQUIRE(short_text.size() == 9);
    DOCTEST_REQUIRE(std::string(short_text.c_str()) == "123456789");
    DOCTEST_REQUIRE(short_text.find("456") == 3);
    DOCTEST_REQUIRE(std::string(short_text.substr(2, 3).c_str()) == "345");
    long_text = copy;
    DOCTEST_REQUIRE(std::string(long_text.c_str()) == "a longer string");
}

DOCTEST_TEST_CASE("Small-string equality is consistent for both operand orders and inequalities") {
    const ark::small_string<char, 8> value("hello");
    const ark::small_string<char, 8> same("hello");
    const ark::small_string<char, 8> other("world");
    DOCTEST_CHECK(value == same);
    DOCTEST_CHECK_FALSE(value == other);
    DOCTEST_CHECK(value != other);
    DOCTEST_CHECK_FALSE(value != same);
    DOCTEST_CHECK(value == "hello");
    DOCTEST_CHECK("hello" == value);
    DOCTEST_CHECK_FALSE(value != "hello");
    DOCTEST_CHECK_FALSE("hello" != value);
    DOCTEST_CHECK(value != "world");
    DOCTEST_CHECK("world" != value);
}

DOCTEST_TEST_CASE("Data-node names work as hash-map keys with equal content") {
    std::unordered_map<ark::data_node_name, int, ark::hash<ark::data_node_name>> values;
    values.emplace(ark::data_node_name("health"), 42);
    values.emplace(ark::data_node_name("health"), 99);
    DOCTEST_REQUIRE(values.size() == 1);
    DOCTEST_REQUIRE(values.at(ark::data_node_name("health")) == 42);
    DOCTEST_REQUIRE_FALSE(values.contains(ark::data_node_name("energy")));
}
