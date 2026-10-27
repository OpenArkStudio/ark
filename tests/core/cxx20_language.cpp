// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

#include "../network/doctest.h"

#include <ark/container/array_map.hpp>
#include <ark/net/net_define.hpp>
#include <ark/uid_generator.hpp>

namespace {

struct custom_value
{
};

enum class integer_enum : std::uint32_t
{
    value
};

template<typename value_type>
constexpr bool exact_integer_contract() {
    return ark::is_buildin_type<value_type>::yes == 1 && ark::is_buildin_type<value_type>::no == 0 &&
           ark::is_buildin_type<const value_type>::yes == 0 && ark::is_buildin_type<const value_type>::no == 1 &&
           ark::is_buildin_type<volatile value_type>::yes == 0 && ark::is_buildin_type<volatile value_type>::no == 1 &&
           ark::is_buildin_type<const volatile value_type>::yes == 0 &&
           ark::is_buildin_type<const volatile value_type>::no == 1 && ark::is_buildin_type<value_type&>::yes == 0 &&
           ark::is_buildin_type<value_type&>::no == 1 && ark::is_buildin_type<value_type&&>::yes == 0 &&
           ark::is_buildin_type<value_type&&>::no == 1;
}

static_assert(exact_integer_contract<std::uint16_t>());
static_assert(exact_integer_contract<std::uint32_t>());
static_assert(exact_integer_contract<std::uint64_t>());
static_assert(exact_integer_contract<std::int16_t>());
static_assert(exact_integer_contract<std::int32_t>());
static_assert(exact_integer_contract<std::int64_t>());

template<typename... value_types>
constexpr bool all_other_types() {
    return ((ark::is_buildin_type<value_types>::yes == 0 && ark::is_buildin_type<value_types>::no == 1) && ...);
}

static_assert(all_other_types<bool, char, signed char, unsigned char, wchar_t, char8_t, char16_t, char32_t, std::int8_t,
    std::uint8_t, float, double, long double, integer_enum, custom_value, std::string, int*, int[2]>());
static_assert(std::is_enum_v<decltype(ark::is_buildin_type<std::uint16_t>::yes)>);
static_assert(std::is_same_v<decltype(ark::check_buildin_type(std::declval<const std::uint16_t&>())), bool>);
static_assert(!noexcept(ark::check_buildin_type(std::declval<const std::uint16_t&>())));
static_assert(noexcept(ark::array_map<std::string, int>{}));

static_assert(clear_hour == 5);
static_assert(std::is_same_v<decltype(clear_hour), const int>);
static_assert(ark::ark_socket_send_size == 32768 && ark::ark_socket_recv_size == 32768);
static_assert(ark::ark_tcp_recv_buffer_size == 1048576 && ark::ark_http_recv_buffer_size == 32768);
static_assert(ark::ark_connect_timeout.count() == 5 && ark::ark_net_heart_time.count() == 30 &&
              ark::ark_reconnect_time.count() == 5);
static_assert(std::is_same_v<decltype(ark::ark_connect_timeout), const std::chrono::seconds>);
static_assert(
    ark::ark_process_net_msg_count_once == 100 && ark::ark_msg_max_length == 4096 && ark::ark_conv_start_id == 1000);
static_assert(ark::ark_kcp_connection_timeout_time == 5000 && ark::ark_kcp_recv_buffer_size == 4096 &&
              ark::ark_kcp_update_interval_time == 10 && ark::ark_kcp_resend_connect_msg_interval == 3000 &&
              ark::ark_kcp_connect_timeout_time == 60000);

#if defined(ARK_USE_GUID_32)
static_assert(!noexcept(std::declval<ark::uid_generator&>().get_uid()));
#elif !defined(ARK_USE_GUID_128)
static_assert(ark::time_bits == 29 && ark::worker_bit == 24 && ark::seq_bits == 10 && ark::uid_epoch == 1542643200);
static_assert(std::is_same_v<decltype(ark::uid_epoch), const int64_t>);
static_assert(!noexcept(std::declval<ark::bits_allocator&>().alloc(0, 0, 0)));
static_assert(!noexcept(std::declval<ark::uid_generator&>().get_uid(0)));
static_assert([] {
    ark::bits_allocator allocator(29, 24, 10);
    return allocator.alloc(536870911, 16777215, 1023) == std::numeric_limits<int64_t>::max();
}());
#endif

} // namespace

TEST_CASE("Built-in type checks retain const-reference deduction and exact explicit types") {
    std::uint16_t value{42};
    const std::uint16_t constant{42};
    volatile std::uint16_t changing{42};
    CHECK(ark::check_buildin_type(value));
    CHECK(ark::check_buildin_type(constant));
    CHECK_FALSE(ark::check_buildin_type<const std::uint16_t>(constant));
    CHECK_FALSE(ark::check_buildin_type<std::uint16_t&>(value));
    CHECK_FALSE(ark::check_buildin_type(changing));
    CHECK_FALSE(ark::check_buildin_type(std::uint8_t{42}));
    CHECK_FALSE(ark::check_buildin_type(true));
    CHECK_FALSE(ark::check_buildin_type(char16_t{42}));
    CHECK_FALSE(ark::check_buildin_type(integer_enum::value));
    CHECK_FALSE(ark::check_buildin_type(custom_value{}));
}

#if !defined(ARK_USE_GUID_32) && !defined(ARK_USE_GUID_128)
TEST_CASE("UID bit layouts reject invalid widths before shifting") {
    const std::array<std::array<int, 3>, 9> invalid{{{-1, 64, 0}, {64, -1, 0}, {0, 0, 64}, {29, 24, 9}, {29, 24, 11},
        {63, 1, -1}, {std::numeric_limits<int>::max(), 0, 0},
        {0, std::numeric_limits<int>::max(), std::numeric_limits<int>::max()},
        {0, 0, std::numeric_limits<int>::min()}}};
    for (const auto& widths : invalid)
    {
        CAPTURE(widths);
        CHECK_THROWS_AS(ark::bits_allocator(widths[0], widths[1], widths[2]), std::invalid_argument);
    }
}

TEST_CASE("UID bit allocation retains fixed values and maximum unsigned fields") {
    ark::bits_allocator allocator(ark::time_bits, ark::worker_bit, ark::seq_bits);
    CHECK(allocator.get_total_bits() == 64);
    CHECK(allocator.get_sign_bits() == 1);
    CHECK(allocator.get_timestamp_bits() == 29);
    CHECK(allocator.get_worker_id_bits() == 24);
    CHECK(allocator.get_sequence_bits() == 10);
    CHECK(allocator.get_max_delta_seconds() == 536870911);
    CHECK(allocator.get_max_sequence() == 1023);

    struct allocation_case
    {
        int64_t delta;
        int64_t worker;
        int64_t sequence;
        int64_t expected;
    };

    const std::array<allocation_case, 6> cases{{{0, 0, 0, 0}, {1, 0, 0, 17179869184}, {0, 1, 0, 1024}, {0, 0, 1, 1},
        {123456789, 16777215, 1023, 2120971502076559359}, {536870911, 16777215, 1023, 9223372036854775807}}};
    for (const auto& input : cases)
    {
        CAPTURE(input.delta);
        CAPTURE(input.worker);
        CAPTURE(input.sequence);
        CHECK(allocator.alloc(input.delta, input.worker, input.sequence) == input.expected);
    }
}

TEST_CASE("UID fields cannot spill into adjacent fields") {
    ark::bits_allocator allocator(29, 24, 10);
    for (const auto& value : std::array<std::array<int64_t, 3>, 7>{{{-1, 0, 0}, {536870912, 0, 0}, {0, -1, 0},
             {0, 16777216, 0}, {0, 0, -1}, {0, 0, 1024}, {std::numeric_limits<int64_t>::max(), 0, 0}}})
    {
        CAPTURE(value);
        CHECK_THROWS_AS(allocator.alloc(value[0], value[1], value[2]), std::out_of_range);
    }
    CHECK(allocator.alloc(1, 0, 0) == 17179869184);

    for (const auto& widths : std::array<std::array<int, 3>, 3>{{{63, 0, 0}, {0, 63, 0}, {0, 0, 63}}})
    {
        ark::bits_allocator edge(widths[0], widths[1], widths[2]);
        for (int field = 0; field < 3; ++field)
        {
            std::array<int64_t, 3> value{};
            value[field] = std::numeric_limits<int64_t>::max();
            if (widths[field] == 63)
            {
                CHECK(edge.alloc(value[0], value[1], value[2]) == value[field]);
            }
            else
            {
                CHECK_THROWS_AS(edge.alloc(value[0], value[1], value[2]), std::out_of_range);
            }
        }
    }
}
#endif
