// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <ark/random/random_generator.hpp>

// Keep the first inclusion separate to check standalone inclusion and the include guard.
#include <array>
#include <cstdint>
#include <limits>

#include "../network/doctest.h"

#include <ark/random/random_generator.hpp>
#include <ark/random/random_xoshiro.hpp>

TEST_CASE("Random generator retains its seed sequence and resets its count") {
    ark::random_generator generator;
    CHECK(generator.get_seed() == 0);
    CHECK(generator.get_random_count() == 0);
    generator.set_seed(1);

    const std::array<std::uint32_t, 6> expected{476707713, 1186278907, 505671508, 2137716191, 936145377, 1215825599};
    for (const auto value : expected) {
        CHECK(generator.random() == value);
    }
    CHECK(generator.get_seed() == 3535774167u);
    CHECK(generator.get_random_count() == 6);
    generator.set_seed(1);
    CHECK(generator.get_seed() == 1);
    CHECK(generator.get_random_count() == 0);
    CHECK(generator.random() == expected.front());
}

TEST_CASE("Signed random ranges retain legacy in-range values and unsigned overloads") {
    ark::random_generator signed_values;
    ark::random_generator unsigned_values;
    signed_values.set_seed(1);
    unsigned_values.set_seed(1);

    const std::array<int, 6> expected_signed{13, 7, 8, 91, 77, 99};
    const std::array<std::uint32_t, 6> expected_unsigned{23, 17, 18, 101, 87, 109};
    for (std::size_t index = 0; index < expected_signed.size(); ++index) {
        CHECK(signed_values.random(-100, 100) == expected_signed[index]);
        CHECK(unsigned_values.random(10u, 110u) == expected_unsigned[index]);
    }
    CHECK(signed_values.get_seed() == unsigned_values.get_seed());
    CHECK(signed_values.get_random_count() == unsigned_values.get_random_count());
    CHECK(unsigned_values.random(0u) == 0);
    CHECK(unsigned_values.random(10u, 10u) == 10);
    CHECK(unsigned_values.get_seed() == 3535774167u);
    CHECK(unsigned_values.get_random_count() == 6);
}

TEST_CASE("Signed random ranges decode full-width spans without overflow") {
    ark::random_generator generator;
    generator.set_seed(1);
    const std::array<int, 6> expected{-1670775935, -961204741, -1641812140, -9767457, -1211338271, -931658049};
    for (const auto value : expected) {
        CHECK(generator.random(std::numeric_limits<int>::min(), std::numeric_limits<int>::max()) == value);
    }
    CHECK(generator.get_seed() == 3535774167u);
    CHECK(generator.get_random_count() == 6);
}

TEST_CASE("Signed random ranges preserve half-open reversed and equal bounds") {
    constexpr int lowest = std::numeric_limits<int>::min();
    constexpr int highest = std::numeric_limits<int>::max();
    const std::array<std::array<int, 2>, 12> ranges{
        {{lowest, highest}, {lowest, 0}, {-1, highest}, {lowest, 1}, {-100, 100}, {-100, -1}, {0, highest},
            {lowest, lowest + 1}, {highest - 1, highest}, {lowest, lowest}, {highest, highest}, {0, 0}}};
    for (const auto& bounds : ranges) {
        CAPTURE(bounds[0]);
        CAPTURE(bounds[1]);
        for (const std::uint32_t seed : {0u, 1u, 123456789u, std::numeric_limits<std::uint32_t>::max()}) {
            ark::random_generator forward;
            ark::random_generator reversed;
            forward.set_seed(seed);
            reversed.set_seed(seed);
            for (int index = 0; index < 64; ++index) {
                const int value = forward.random(bounds[0], bounds[1]);
                CHECK(value == reversed.random(bounds[1], bounds[0]));
                if (bounds[0] == bounds[1]) {
                    CHECK(value == bounds[0]);
                    CHECK(forward.get_seed() == seed);
                    CHECK(forward.get_random_count() == 0);
                } else {
                    CHECK(value >= bounds[0]);
                    CHECK(value < bounds[1]);
                    CHECK(forward.get_random_count() == index + 1);
                }
                CHECK(forward.get_seed() == reversed.get_seed());
                CHECK(forward.get_random_count() == reversed.get_random_count());
            }
        }
    }
}

namespace {

template<typename generator_type, std::size_t count>
void check_xoshiro_sequence(
    generator_type& generator, const std::array<typename generator_type::result_type, count>& expected) {
    for (const auto value : expected) {
        CHECK(generator.random() == value);
    }
}

} // namespace

// Fixed compatibility vectors from tests/test_random.cpp, included in check-core.
TEST_CASE("Xoshiro xoroshiro128_starstar retains fixed seeded and jump sequences") {
    ark::xoroshiro128_starstar generator(0x12345678);
    check_xoshiro_sequence(
        generator, std::array<ark::xoroshiro128_starstar::result_type, 10>{0x6baaab3a, 0x9385fdbb, 0x99ae85fe,
                       0x88e2358c, 0x6592b0ce, 0x20eadc84, 0x6a8d80b7, 0xa676abb7, 0x726ef19a, 0xd4e33183});
    generator.get_core().jump();
    check_xoshiro_sequence(
        generator, std::array<ark::xoroshiro128_starstar::result_type, 10>{0xb40ef1a3, 0xeaf5f926, 0xdaedd531,
                       0xd75a0342, 0xf5cd493d, 0x4081f259, 0x92a08a09, 0x8e7856ff, 0x3eecdb2, 0x34cd820c});
}

TEST_CASE("Xoshiro xoroshiro128_plus retains fixed seeded and jump sequences") {
    ark::xoroshiro128_plus generator(0x12345678);
    check_xoshiro_sequence(
        generator, std::array<ark::xoroshiro128_plus::result_type, 10>{0xf1c950fb, 0x3c9b4647, 0x212ab3fb, 0x9930ad20,
                       0xe891a595, 0x5f3d9875, 0x264dd138, 0xb23b09e7, 0xb351ccd6, 0xf6342d85});
    generator.get_core().jump();
    check_xoshiro_sequence(
        generator, std::array<ark::xoroshiro128_plus::result_type, 10>{0x65981904, 0x2d406a8b, 0xf722cc27, 0xd93f4e0,
                       0xc8c324e, 0x47adc6fc, 0x2e331b89, 0x2de59177, 0x68298f16, 0x3c1f786});
}

TEST_CASE("Xoshiro xoshiro256_starstar retains fixed seeded and jump sequences") {
    ark::xoshiro256_starstar generator(0x12345678);
    check_xoshiro_sequence(
        generator, std::array<ark::xoshiro256_starstar::result_type, 10>{0x83ea18ca6bf49f0c, 0xca24436623484287,
                       0xe7965c078396b9dc, 0xb9036864adb81725, 0x5c35ba435d92ab9b, 0x45202e3b4085a2cc,
                       0xa1e9410f7f528e4e, 0x48eb35365566f0b2, 0x90412e6e9ae131c3, 0xba4a1df61ca0b237});
    generator.get_core().jump();
    check_xoshiro_sequence(
        generator, std::array<ark::xoshiro256_starstar::result_type, 10>{0x555e786541686a07, 0x6b5e064426c7dce4,
                       0x63a8537c3e2de537, 0x1b1eb7c6ef11c6c, 0x9d6eaac62ce6a4f4, 0x8b2cb439dc19fadd,
                       0xb749c288597ceac4, 0x92da83e146fdff1d, 0x110d38c051cededc, 0xadab1aacca67bd98});
    generator.get_core().long_jump();
    check_xoshiro_sequence(
        generator, std::array<ark::xoshiro256_starstar::result_type, 10>{0x855835566240e421, 0xef37ac417209f23c,
                       0x27d0b120fbc1263b, 0x3acf06c9c8bebcfc, 0x8e4c39858e961d77, 0x54ef99ad7133b336,
                       0x507e39520e592971, 0xf17538afb5568bf2, 0x9b12bf7bc0ad1cb, 0xe30b952ddb0edc22});
}

TEST_CASE("Xoshiro xoshiro256_plus retains fixed seeded and jump sequences") {
    ark::xoshiro256_plus generator(0x12345678);
    check_xoshiro_sequence(
        generator, std::array<ark::xoshiro256_plus::result_type, 10>{0xa6766515ded04cf7, 0x8b8ef459334146d2,
                       0xa5e132765ffaea1b, 0x42a32c7899960f6, 0xb55425c32b032ae, 0x4f5b510e66ed35c3, 0x86364d228b55294e,
                       0x15fee24568b9f2e4, 0x79172607cb7639c8, 0x380558a4c62a5df3});
    generator.get_core().jump();
    check_xoshiro_sequence(
        generator, std::array<ark::xoshiro256_plus::result_type, 10>{0x65e286c2682c7ede, 0x6fd589cda019d6da,
                       0x74ca58c461d9116, 0xaf78ff6a0b528fa, 0x532d4519bc355dff, 0x4fd9664f92848fd0, 0xe871531860e04f51,
                       0x9f66375cd3511195, 0xedb2c3bd1fad482d, 0xabd051255c4a95a4});
    generator.get_core().long_jump();
    check_xoshiro_sequence(
        generator, std::array<ark::xoshiro256_plus::result_type, 10>{0x823dc8ffe9ec3044, 0x2282021c3619f998,
                       0x58b7e7b6e4bf12c2, 0xba93e7ae8ffd60be, 0x7c5604182a5e5c, 0xfe2b1a4e6cc03509, 0x3cc13d59418b58b6,
                       0xee043f7276571ff3, 0xb0cefcacf43f423d, 0x2c06bd7d4b2a7b61});
}
