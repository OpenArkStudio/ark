// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <ark/hash/murmur_hash.hpp>

// Keep the first inclusion separate to check standalone inclusion and the include guard.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>

#include "../network/doctest.h"

#include <ark/hash/murmur_hash.hpp>

namespace {

static_assert(std::is_member_function_pointer_v<decltype(&ark::murmur_hash::murmur_hash3_x86_128)>);
static_assert(std::is_member_function_pointer_v<decltype(&ark::murmur_hash::murmur_hash3_x86_128_legacy_msvc)>);
static_assert(std::is_pointer_v<decltype(&ark::murmur_hash::murmur_hash3_x86_32)>);
static_assert(std::is_pointer_v<decltype(&ark::murmur_hash::murmur_hash3_x64_128)>);

void hash_x86_128(const void* key, int length, std::uint32_t seed, void* output) {
    ark::murmur_hash{}.murmur_hash3_x86_128(key, length, seed, output);
}

void hash_x86_128_legacy(const void* key, int length, std::uint32_t seed, void* output) {
    ark::murmur_hash{}.murmur_hash3_x86_128_legacy_msvc(key, length, seed, output);
}

void hash_murmur2(const void* key, int length, std::uint32_t, void* output) {
    ark::murmur_hash::murmur_hash2(key, length, output);
}

template<std::size_t output_size, typename hash_function>
std::uint32_t verification_value(hash_function hash) {
    // Austin Appleby's SMHasher VerificationTest: all prefix lengths 0..255,
    // seeds 256-length, then hash their concatenated digests with seed zero.
    std::array<std::uint8_t, 256> key{};
    std::array<std::uint8_t, output_size * 256> hashes{};
    std::array<std::uint8_t, output_size> result{};
    for (int length = 0; length < 256; ++length)
    {
        key[length] = static_cast<std::uint8_t>(length);
        hash(key.data(), length, 256 - length, hashes.data() + length * output_size);
    }
    hash(hashes.data(), static_cast<int>(hashes.size()), 0, result.data());
    return std::uint32_t{result[0]} | (std::uint32_t{result[1]} << 8) | (std::uint32_t{result[2]} << 16) |
           (std::uint32_t{result[3]} << 24);
}

template<typename word_type, std::size_t word_count>
std::array<word_type, word_count> little_words(const std::array<std::uint8_t, sizeof(word_type) * word_count>& bytes) {
    std::array<word_type, word_count> words{};
    for (std::size_t word = 0; word < word_count; ++word)
    {
        for (std::size_t byte = 0; byte < sizeof(word_type); ++byte)
        {
            words[word] |= static_cast<word_type>(bytes[word * sizeof(word_type) + byte]) << (byte * 8);
        }
    }
    return words;
}

template<std::size_t output_size, typename hash_function>
void check_byte_buffers(hash_function hash) {
    alignas(8) std::array<std::uint8_t, 257> key{};
    for (std::size_t index = 0; index < key.size(); ++index)
    {
        key[index] = static_cast<std::uint8_t>(index * 37 + 0x80);
    }
    // Cover every tail path and both sides of several full-block boundaries.
    for (int length = 0; length <= 257; ++length)
    {
        if (length > 65 && length != 127 && length != 128 && length != 129 && length != 255 && length != 256 &&
            length != 257)
        {
            continue;
        }
        for (const auto seed : {0u, 1u, 0x80000000u, 0xffffffffu})
        {
            alignas(8) std::array<std::uint8_t, output_size> expected{};
            hash(key.data(), length, seed, expected.data());
            for (std::size_t offset = 0; offset < 8; ++offset)
            {
                CAPTURE(length);
                CAPTURE(seed);
                CAPTURE(offset);
                // The readable range ends at the allocation boundary, so a sanitizer
                // can detect full-block reads past the exact input length.
                auto input = std::make_unique<std::uint8_t[]>(std::max<std::size_t>(1, length + offset));
                std::copy_n(key.begin(), length, input.get() + offset);
                alignas(8) std::array<std::uint8_t, output_size + 16> output;
                output.fill(0xa5);
                const auto output_offset = (offset + 3) % 8;
                hash(input.get() + offset, length, seed, output.data() + output_offset);
                CHECK(std::equal(expected.begin(), expected.end(), output.begin() + output_offset));
                CHECK(std::all_of(
                    output.begin(), output.begin() + output_offset, [](auto value) { return value == 0xa5; }));
                CHECK(std::all_of(output.begin() + output_offset + output_size, output.end(),
                    [](auto value) { return value == 0xa5; }));
            }
        }
    }
}

} // namespace

TEST_CASE("Murmur3 retains upstream verification values and the legacy swapped-block policy") {
    // Independent standard values and verification procedure:
    // https://github.com/aappleby/smhasher/blob/master/src/main.cpp#L67-L70
    // https://github.com/aappleby/smhasher/blob/master/src/KeysetTest.cpp#L11-L38
    // Swapped-block values were derived from the same public-domain reference
    // with only full-block byte order reversed; they are ARK compatibility fixtures.
    CHECK(verification_value<4>(ark::murmur_hash::murmur_hash3_x86_32) == 0xb0f57ee3u);
    CHECK(verification_value<16>(hash_x86_128) == 0xb3ece62au);
    CHECK(verification_value<16>(ark::murmur_hash::murmur_hash3_x64_128) == 0x6384ba69u);
    CHECK(verification_value<4>(ark::murmur_hash::murmur_hash3_x86_32_legacy_msvc) == 0xd1ab67cfu);
    CHECK(verification_value<16>(hash_x86_128_legacy) == 0x2df8b022u);
    CHECK(verification_value<16>(ark::murmur_hash::murmur_hash3_x64_128_legacy_msvc) == 0x501e703bu);
}

TEST_CASE("Murmur3 rotations preserve full-width seeds and multi-block golden words") {
    std::array<std::uint8_t, 33> key{};
    for (std::size_t index = 0; index < key.size(); ++index)
    {
        key[index] = static_cast<std::uint8_t>(index);
    }

    struct golden_case
    {
        int length;
        std::uint32_t seed;
        std::uint32_t x86_32;
        std::array<std::uint32_t, 4> x86_128;
        std::array<std::uint64_t, 2> x64_128;
    };

    // Fixed scalar-reference vectors exercise every production rotation count,
    // high-bit seed preservation, exact blocks and a multi-block tail.
    const std::array<golden_case, 3> little{{
        {16, 0xffffffffu, 0x9fd6e060u, {0x4f872353u, 0x3ffde8eeu, 0x0c593cefu, 0xd276cbb5u},
            {0x75298e938ac005bfull, 0x26873cd6ed1b3b1eull}},
        {33, 0x12345678u, 0x2a4f25b0u, {0x0101769du, 0x6e0928d4u, 0x5f9361deu, 0xea784760u},
            {0xa94d180f895f3670ull, 0x9635b8a3feb6f62full}},
        {33, 0xffffffffu, 0xa5737017u, {0x53948860u, 0xb573ee1du, 0xb2e1371cu, 0x7d39669eu},
            {0xf14b6deed9373a70ull, 0xf83f262a61312136ull}},
    }};
    const std::array<golden_case, 3> swapped{{
        {16, 0xffffffffu, 0x2d93b016u, {0x84c0695cu, 0x73902949u, 0x6bfdce30u, 0xefc92f7cu},
            {0x703b8fedb7306652ull, 0xb1ce86b7edcd8628ull}},
        {33, 0x12345678u, 0xb255cc88u, {0xaea33583u, 0x1fd73070u, 0x36c553ffu, 0x9f396b6du},
            {0xa34f5dea69959035ull, 0x734c22e1993ef38full}},
        {33, 0xffffffffu, 0x74b4bba8u, {0xc8d61452u, 0x878dd688u, 0xad51e4b6u, 0x5b85edf7u},
            {0xcdcdff4c5a639071ull, 0x8332c8a91ba2cf3dull}},
    }};
    for (std::size_t index = 0; index < little.size(); ++index)
    {
        CAPTURE(index);
        if (index == 2)
        {
            for (std::size_t byte = 0; byte < key.size(); ++byte)
            {
                key[byte] = static_cast<std::uint8_t>(byte * 37 + 0x80);
            }
        }
        for (const bool legacy : {false, true})
        {
            CAPTURE(legacy);
            const auto& expected = legacy ? swapped[index] : little[index];
            std::array<std::uint8_t, 4> actual32{};
            std::array<std::uint8_t, 16> actual86{};
            std::array<std::uint8_t, 16> actual64{};
            const auto hash32 =
                legacy ? ark::murmur_hash::murmur_hash3_x86_32_legacy_msvc : ark::murmur_hash::murmur_hash3_x86_32;
            const auto hash86 = legacy ? hash_x86_128_legacy : hash_x86_128;
            const auto hash64 =
                legacy ? ark::murmur_hash::murmur_hash3_x64_128_legacy_msvc : ark::murmur_hash::murmur_hash3_x64_128;
            hash32(key.data(), expected.length, expected.seed, actual32.data());
            hash86(key.data(), expected.length, expected.seed, actual86.data());
            hash64(key.data(), expected.length, expected.seed, actual64.data());
            CHECK((little_words<std::uint32_t, 1>(actual32)[0] == expected.x86_32));
            CHECK((little_words<std::uint32_t, 4>(actual86) == expected.x86_128));
            CHECK((little_words<std::uint64_t, 2>(actual64) == expected.x64_128));
        }
    }
}

TEST_CASE("Murmur2 retains seed-zero golden words around block boundaries") {
    std::array<std::uint8_t, 33> key{};
    for (std::size_t index = 0; index < key.size(); ++index)
    {
        key[index] = static_cast<std::uint8_t>(index);
    }
    const std::array<std::pair<int, std::uint32_t>, 10> cases{
        {{0, 0}, {1, 0xe94e6ebdu}, {3, 0xe2558031u}, {4, 0xa4985988u}, {15, 0x8184d621u}, {16, 0x5f3c0743u},
            {17, 0xb1aad377u}, {31, 0x1d7d6ee1u}, {32, 0xf1bc6c23u}, {33, 0x915a6941u}}};
    for (const auto& [length, expected] : cases)
    {
        CAPTURE(length);
        std::array<std::uint8_t, 4> result{};
        ark::murmur_hash::murmur_hash2(key.data(), length, result.data());
        CHECK((little_words<std::uint32_t, 1>(result)[0] == expected));
    }
}

TEST_CASE("Murmur3 default and legacy policies agree until a complete block is present") {
    const std::array<std::uint8_t, 16> key{
        0x80, 0xff, 0x00, 0x01, 0xa5, 0x5a, 0x81, 0xfe, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09};
    using hash_function = void (*)(const void*, int, std::uint32_t, void*);

    struct policy_case
    {
        hash_function standard;
        hash_function legacy;
        int block_size;
    };

    const std::array<policy_case, 3> policies{{
        {ark::murmur_hash::murmur_hash3_x86_32, ark::murmur_hash::murmur_hash3_x86_32_legacy_msvc, 4},
        {hash_x86_128, hash_x86_128_legacy, 16},
        {ark::murmur_hash::murmur_hash3_x64_128, ark::murmur_hash::murmur_hash3_x64_128_legacy_msvc, 16},
    }};
    for (const auto& policy : policies)
    {
        for (int length = 0; length < policy.block_size; ++length)
        {
            for (const auto seed : {0u, 0x80000000u, 0xffffffffu})
            {
                CAPTURE(length);
                CAPTURE(seed);
                std::array<std::uint8_t, 16> standard{};
                std::array<std::uint8_t, 16> legacy{};
                policy.standard(key.data(), length, seed, standard.data());
                policy.legacy(key.data(), length, seed, legacy.data());
                CHECK(standard == legacy);
            }
        }
    }
}

TEST_CASE("All Murmur variants accept byte-aligned inputs and outputs without changing digest bytes") {
    check_byte_buffers<4>(ark::murmur_hash::murmur_hash3_x86_32);
    check_byte_buffers<16>(hash_x86_128);
    check_byte_buffers<16>(ark::murmur_hash::murmur_hash3_x64_128);
    check_byte_buffers<4>(hash_murmur2);
    check_byte_buffers<4>(ark::murmur_hash::murmur_hash3_x86_32_legacy_msvc);
    check_byte_buffers<16>(hash_x86_128_legacy);
    check_byte_buffers<16>(ark::murmur_hash::murmur_hash3_x64_128_legacy_msvc);
}
