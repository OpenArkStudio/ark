// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <array>
#include <cstdint>
#include <string>

#include "../network/doctest.h"

#include <ark/kernel/data_impl.hpp>

namespace {

using string_data = ark::data_base<8>;

const std::array<std::string, 5> string_values{"", "a", "1234567", "12345678", std::string(128, 'x')};

} // namespace

DOCTEST_TEST_CASE("Kernel string data compare content in independent inline and allocated storage") {
    for (const auto& value : string_values) {
        DOCTEST_CAPTURE(value);
        string_data left(ark::data_type::dt_string, value.c_str());
        string_data right(ark::data_type::dt_string, value.c_str());
        std::string external = value;
        DOCTEST_CHECK(left.get_string() != right.get_string());
        DOCTEST_CHECK(left.get_string() != external.c_str());
        DOCTEST_CHECK(left.equal(right));
        DOCTEST_CHECK(right.equal(left));
        DOCTEST_CHECK(left.equal(external.c_str()));
        DOCTEST_CHECK(right.equal(external.c_str()));
        DOCTEST_CHECK(left.equal(left.get_string()));
        string_data other(ark::data_type::dt_string, (value + "!").c_str());
        DOCTEST_CHECK_FALSE(left.equal(other));
        DOCTEST_CHECK_FALSE(left.equal(other.get_string()));
    }
}

DOCTEST_TEST_CASE("Kernel string data compare values across different buffer capacities and preserve type checks") {
    ark::data_base<4> allocated(ark::data_type::dt_string, "same value");
    ark::data_base<64> inlined(ark::data_type::dt_string, "same value");
    DOCTEST_CHECK(allocated.get_string() != inlined.get_string());
    DOCTEST_CHECK(allocated.equal(inlined));
    DOCTEST_CHECK(inlined.equal(allocated));
    string_data numeric(ark::data_type::dt_int32, 7);
    string_data text(ark::data_type::dt_string, "7");
    DOCTEST_CHECK_FALSE(text.equal(numeric));
    DOCTEST_CHECK_FALSE(numeric.equal(text));
    DOCTEST_CHECK_FALSE(numeric.equal("7"));
    DOCTEST_CHECK_FALSE(numeric.equal(static_cast<const char*>(nullptr)));
}

DOCTEST_TEST_CASE("Kernel string comparisons preserve null pointer distinctions without dereferencing null") {
    string_data empty(ark::data_type::dt_string, "");
    DOCTEST_CHECK(empty.equal(""));
    DOCTEST_CHECK_FALSE(empty.equal(static_cast<const char*>(nullptr)));
    // Public release leaves allocated string data typed but without storage.
    // Exercise the existing null-pointer comparison contract in that state.
    string_data first(ark::data_type::dt_string, "allocated first");
    string_data second(ark::data_type::dt_string, "allocated second");
    first.release();
    second.release();
    DOCTEST_REQUIRE(first.get_string() == nullptr);
    DOCTEST_REQUIRE(second.get_string() == nullptr);
    DOCTEST_CHECK(first.equal(static_cast<const char*>(nullptr)));
    DOCTEST_CHECK(first.equal(second));
    DOCTEST_CHECK(second.equal(first));
    DOCTEST_CHECK_FALSE(first.equal(empty));
    DOCTEST_CHECK_FALSE(empty.equal(first));
    DOCTEST_CHECK_FALSE(first.equal(""));
}

DOCTEST_TEST_CASE("Kernel string swap preserves both values across inline boundaries and repeated swaps") {
    for (const auto& first : string_values) {
        for (const auto& second : string_values) {
            DOCTEST_CAPTURE(first);
            DOCTEST_CAPTURE(second);
            string_data left(ark::data_type::dt_string, first.c_str());
            string_data right(ark::data_type::dt_string, second.c_str());
            left.swap(right);
            DOCTEST_CHECK(left.get_type() == ark::data_type::dt_string);
            DOCTEST_CHECK(right.get_type() == ark::data_type::dt_string);
            DOCTEST_CHECK(std::string(left.get_string()) == second);
            DOCTEST_CHECK(std::string(right.get_string()) == first);
            left.swap(right);
            DOCTEST_CHECK(std::string(left.get_string()) == first);
            DOCTEST_CHECK(std::string(right.get_string()) == second);
        }
    }
    // data_impl uses the production allocator and a four-byte inline buffer.
    ark::data_impl small(ark::data_type::dt_string, "abc");
    ark::data_impl large(ark::data_type::dt_string, "longer than four bytes");
    small.swap(large);
    DOCTEST_CHECK(std::string(small.get_string()) == "longer than four bytes");
    DOCTEST_CHECK(std::string(large.get_string()) == "abc");
    small.swap(large);
    DOCTEST_CHECK(std::string(small.get_string()) == "abc");
    DOCTEST_CHECK(std::string(large.get_string()) == "longer than four bytes");
}

DOCTEST_TEST_CASE("Kernel string self swaps and copy assignment retain independent owning values") {
    for (const auto& value : string_values) {
        DOCTEST_CAPTURE(value);
        string_data original(ark::data_type::dt_string, value.c_str());
        original.swap(original);
        DOCTEST_CHECK(std::string(original.get_string()) == value);
        string_data copy(original);
        original.set_string("replacement");
        DOCTEST_CHECK(std::string(copy.get_string()) == value);
        const auto& self = copy;
        copy = self;
        DOCTEST_CHECK(std::string(copy.get_string()) == value);
        for (const auto& before : string_values) {
            DOCTEST_CAPTURE(before);
            string_data assigned(ark::data_type::dt_string, before.c_str());
            assigned = copy;
            DOCTEST_CHECK(std::string(assigned.get_string()) == value);
            DOCTEST_CHECK(assigned.get_string() != copy.get_string());
            assigned.set_string("changed independently");
            DOCTEST_CHECK(std::string(copy.get_string()) == value);
            assigned.assign(copy);
            DOCTEST_CHECK(std::string(assigned.get_string()) == value);
            DOCTEST_CHECK(assigned.get_string() != copy.get_string());
        }
    }
}

DOCTEST_TEST_CASE("Kernel inline strings survive swaps with scalar data without changing scalar values") {
    string_data text(ark::data_type::dt_string, "short");
    string_data numeric(ark::data_type::dt_int64, std::int64_t{42});
    text.swap(numeric);
    DOCTEST_CHECK(text.get_type() == ark::data_type::dt_int64);
    DOCTEST_CHECK(text.get_int64() == 42);
    DOCTEST_CHECK(numeric.get_type() == ark::data_type::dt_string);
    DOCTEST_CHECK(std::string(numeric.get_string()) == "short");
    text.swap(numeric);
    DOCTEST_CHECK(text.get_type() == ark::data_type::dt_string);
    DOCTEST_CHECK(std::string(text.get_string()) == "short");
    DOCTEST_CHECK(numeric.get_type() == ark::data_type::dt_int64);
    DOCTEST_CHECK(numeric.get_int64() == 42);
}
