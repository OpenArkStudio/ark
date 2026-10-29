// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>

#include "../network/doctest.h"

#include <ark/kernel/data_impl.hpp>

namespace {

struct measured_allocator
{
    static inline std::size_t calls{};
    static inline std::size_t requested{};
    static inline bool reject{};

    void* alloc(std::size_t size) {
        ++calls;
        requested = size;
        if (reject)
        {
            return nullptr;
        }
        return new char[size];
    }

    void free(void* value, std::size_t) {
        delete[] static_cast<char*>(value);
    }

    void swap(measured_allocator&) noexcept {
    }
};

using measured_data = ark::data_base<8, measured_allocator>;
constexpr char payload[] = "binary\0value";

struct reject_allocations
{
    reject_allocations() {
        measured_allocator::reject = true;
    }

    ~reject_allocations() {
        measured_allocator::reject = false;
    }
};

} // namespace

TEST_CASE("Kernel owned user data rejects unrepresentable storage before allocation") {
    const std::size_t limit = std::numeric_limits<std::uint32_t>::max();
    for (const auto size : {limit - sizeof(std::size_t) + 1, limit, std::numeric_limits<std::size_t>::max()})
    {
        CAPTURE(size);
        measured_data previous(ark::data_type::dt_string, "existing allocated value");
        const auto* storage = previous.get_string();
        const auto calls = measured_allocator::calls;
        {
            reject_allocations reject;
            // The rejecting allocator keeps the old, unchecked implementation's
            // wrapped request safe while exposing its incorrect allocation call.
            CHECK_THROWS_AS(previous.set_user_data(payload, size), std::length_error);
            CHECK_THROWS_AS(measured_data(ark::data_type::dt_userdata, payload, size), std::length_error);
        }
        CHECK(measured_allocator::calls == calls);
        CHECK(previous.get_type() == ark::data_type::dt_string);
        CHECK(previous.get_string() == storage);
        CHECK(std::string(previous.get_string()) == "existing allocated value");
    }
}

TEST_CASE("Kernel owned data permits the representable storage boundary without a giant allocation") {
    measured_data previous(ark::data_type::dt_string, "existing allocated value");
    const auto* storage = previous.get_string();
    const auto calls = measured_allocator::calls;
    constexpr std::size_t maximum = std::numeric_limits<std::uint32_t>::max();
    {
        reject_allocations reject;
        CHECK_THROWS_AS(previous.set_user_data(payload, maximum - sizeof(std::size_t)), std::bad_alloc);
    }
    CHECK(measured_allocator::calls == calls + 1);
    CHECK(measured_allocator::requested == maximum);
    CHECK(previous.get_type() == ark::data_type::dt_string);
    CHECK(previous.get_string() == storage);
    CHECK(std::string(previous.get_string()) == "existing allocated value");
}

TEST_CASE("Kernel owned data rejects oversized raw metadata during copy and assignment") {
    measured_data source(ark::data_type::dt_userdata, payload, sizeof(payload));
    measured_data destination(ark::data_type::dt_string, "existing allocated value");
    auto* const raw = source.get_raw_user_data();
    auto* const destination_storage = destination.get_string();
    auto& size = *static_cast<std::size_t*>(raw);
    const auto original_size = size;
    size = std::numeric_limits<std::size_t>::max();
    const auto calls = measured_allocator::calls;
    {
        reject_allocations reject;
        CHECK_THROWS_AS((void)measured_data(source), std::length_error);
        CHECK_THROWS_AS(measured_data(static_cast<const ark::data&>(source)), std::length_error);
        CHECK_THROWS_AS(destination.assign(source), std::length_error);
        CHECK_THROWS_AS(destination.set_raw_user_data(raw), std::length_error);
    }
    CHECK(measured_allocator::calls == calls);
    CHECK(source.get_raw_user_data() == raw);
    CHECK(size == std::numeric_limits<std::size_t>::max());
    size = original_size;
    CHECK(destination.get_type() == ark::data_type::dt_string);
    CHECK(destination.get_string() == destination_storage);
    CHECK(std::string(destination.get_string()) == "existing allocated value");
    std::size_t restored_size{};
    CHECK(source.get_user_data(restored_size) == static_cast<char*>(raw) + sizeof(std::size_t));
    CHECK(restored_size == sizeof(payload));
    CHECK(std::memcmp(source.get_user_data(restored_size), payload, sizeof(payload)) == 0);
}

TEST_CASE("Kernel owned data keeps valid inline string and binary storage behavior") {
    measured_data value;
    for (const auto* text : {"", "1234567", "12345678", "a longer allocated string"})
    {
        value.set_string(text);
        CHECK(std::string(value.get_string()) == text);
    }
    for (const auto size : {std::size_t{0}, sizeof(payload)})
    {
        value.set_user_data(payload, size);
        std::size_t actual{};
        const auto* bytes = value.get_user_data(actual);
        CHECK(actual == size);
        if (size)
        {
            CHECK(std::memcmp(bytes, payload, size) == 0);
        }
        measured_data copy(value);
        CHECK(copy.get_raw_user_data() != value.get_raw_user_data());
        std::size_t copied_size{};
        const auto* copied_bytes = copy.get_user_data(copied_size);
        CHECK(copied_size == size);
        if (size)
        {
            CHECK(std::memcmp(copied_bytes, payload, size) == 0);
        }
    }
}
