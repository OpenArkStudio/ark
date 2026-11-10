// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>

#include "../network/doctest.h"

#include <ark/lexical_cast.hpp>
#include <ark/plugin_manager.hpp>

DOCTEST_TEST_CASE("application manifest paths preserve TOML path strings") {
    ark::plugin_manager manager;
    manager.set_app_conf("conf/timers.app.toml");
    DOCTEST_CHECK(manager.get_app_conf() == "conf/timers.app.toml");
    manager.set_app_conf("conf/with spaces/custom.toml");
    DOCTEST_CHECK(manager.get_app_conf() == "conf/with spaces/custom.toml");
}

namespace {

template<typename type>
concept has_module_lookup = requires(ark::plugin_manager& manager) { manager.find_module<type>(); };

static_assert(has_module_lookup<ark::module>);
static_assert(!has_module_lookup<int>);

} // namespace

DOCTEST_TEST_CASE("Core conversion templates preserve supported values and errors") {
    DOCTEST_REQUIRE(lexical_cast<int>(std::string("42")) == 42);
    DOCTEST_REQUIRE(lexical_cast<int>(7) == 7);
    DOCTEST_REQUIRE(lexical_cast<std::string>(9) == "9");
    DOCTEST_REQUIRE(lexical_cast<std::string>(std::string("same")) == "same");
    DOCTEST_REQUIRE(lexical_cast<bool>(-1));
    DOCTEST_REQUIRE_FALSE(lexical_cast<bool>(0));
    DOCTEST_REQUIRE(lexical_cast<bool>("true"));
    DOCTEST_REQUIRE_FALSE(lexical_cast<bool>(std::string("false")));
    DOCTEST_REQUIRE_THROWS_AS(lexical_cast<bool>("nope"), std::invalid_argument);
    DOCTEST_REQUIRE_FALSE(lexical_cast<bool>("invalid"));
}

DOCTEST_TEST_CASE("Plugin-manager typed lookup exposes the requested module type") {
    static_assert(std::is_same_v<decltype(ark::plugin_manager::instance()->find_module<ark::module>()), ark::module*>);
    DOCTEST_REQUIRE(ark::plugin_manager::instance() != nullptr);
}

DOCTEST_TEST_CASE("Unsigned 32-bit conversion preserves decimal boundaries and accepted prefixes") {
    struct conversion_case {
        const char* text;
        std::uint32_t expected;
    };

    const conversion_case cases[] = {{"0", 0u}, {"2147483647", 2147483647u}, {"2147483648", 2147483648u},
        {"4045620583", 4045620583u}, {"4294967295", std::numeric_limits<std::uint32_t>::max()}, {" \t+42suffix", 42u},
        {"-1", std::numeric_limits<std::uint32_t>::max()}};
    for (const auto& value : cases) {
        DOCTEST_CAPTURE(value.text);
        DOCTEST_CHECK(lexical_cast<std::uint32_t>(std::string(value.text)) == value.expected);
        DOCTEST_CHECK(lexical_cast<std::uint32_t>(value.text) == value.expected);
    }
}

DOCTEST_TEST_CASE("Unsigned 64-bit conversion preserves decimal boundaries and accepted prefixes") {
    struct conversion_case {
        const char* text;
        std::uint64_t expected;
    };

    const conversion_case cases[] = {{"0", 0ULL}, {"4294967295", 4294967295ULL},
        {"9223372036854775807", 9223372036854775807ULL}, {"9223372036854775808", 9223372036854775808ULL},
        {"18446744073709551614", 18446744073709551614ULL},
        {"18446744073709551615", std::numeric_limits<std::uint64_t>::max()}, {" \t+42suffix", 42ULL},
        {"-1", std::numeric_limits<std::uint64_t>::max()}};
    for (const auto& value : cases) {
        DOCTEST_CAPTURE(std::string(value.text));
        DOCTEST_CHECK(lexical_cast<std::uint64_t>(std::string(value.text)) == value.expected);
        DOCTEST_CHECK(lexical_cast<std::uint64_t>(value.text) == value.expected);
    }
}
