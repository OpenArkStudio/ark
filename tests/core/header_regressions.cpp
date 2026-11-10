// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

#include "../network/doctest.h"

#include <ark/guid128.hpp>

DOCTEST_TEST_CASE("128-bit GUID decimal strings round-trip the full unsigned range") {
    constexpr auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    const std::array<std::pair<std::uint64_t, std::uint64_t>, 6> components = {
        {{0, 0}, {0, maximum}, {maximum, 0}, {maximum, maximum}, {maximum / 2 + 1, 42}, {123, 456}}};
    for (const auto& [high, low] : components) {
        const ark::guid_t128 original(high, low);
        ark::guid_t128 parsed(7, 9);
        DOCTEST_CAPTURE(original.to_string());
        DOCTEST_REQUIRE(parsed.from_string(original.to_string()));
        DOCTEST_CHECK(parsed == original);
    }
    ark::guid_t128 parsed(7, 9);
    DOCTEST_REQUIRE(parsed.from_string("0001-0002"));
    DOCTEST_CHECK(parsed.to_string() == "1-2");
}

DOCTEST_TEST_CASE("128-bit GUID parsing rejects malformed and overflowing components without changing the value") {
    const std::array<std::string, 17> invalid = {"", "1", "1-", "-1", "a-1", "1-a", "1-2-3", " 1-2", "1- 2", "1-2 ",
        "+1-2", "1-+2", "1--2", "18446744073709551616-0", "0-18446744073709551616", std::string("1-2\0", 4),
        std::string("1\0-2", 4)};
    for (const auto& input : invalid) {
        ark::guid_t128 parsed(7, 9);
        DOCTEST_CAPTURE(input);
        DOCTEST_CHECK_FALSE(parsed.from_string(input));
        DOCTEST_CHECK(parsed.high == 7);
        DOCTEST_CHECK(parsed.low == 9);
    }
}
