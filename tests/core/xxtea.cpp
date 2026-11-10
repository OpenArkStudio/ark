// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>
#include <memory>
#include <string>
#include <string_view>

#include "../network/doctest.h"

#define MX 17
#define DELTA 23
#include <ark/crypto/xxtea.hpp>

static_assert(MX == 17 && DELTA == 23);
#undef MX
#undef DELTA

namespace {

std::string encrypt(const std::string& message, const void* key) {
    size_t length = 0;
    std::unique_ptr<void, decltype(&std::free)> value(
        ark::xxtea::xxtea_encrypt(message.data(), message.size(), key, &length), &std::free);
    if (!value) {
        return {};
    }
    return {static_cast<const char*>(value.get()), length};
}

} // namespace

DOCTEST_TEST_CASE("XXTEA reads short keys only through their terminating zero") {
    const std::string message("binary\0message", 14);
    for (size_t length = 0; length < 16; ++length) {
        DOCTEST_CAPTURE(length);
        // Exact allocation makes reading beyond the terminator observable.
        auto short_key = std::make_unique<char[]>(length + 1);
        std::fill_n(short_key.get(), length, 'k');
        short_key[length] = '\0';
        std::array<char, 16> padded{};
        std::fill_n(padded.begin(), length, 'k');
        const auto ciphertext = encrypt(message, short_key.get());
        DOCTEST_REQUIRE_FALSE(ciphertext.empty());
        DOCTEST_CHECK(ciphertext == encrypt(message, padded.data()));
        size_t decoded_size = 0;
        std::unique_ptr<void, decltype(&std::free)> decoded(
            ark::xxtea::xxtea_decrypt(ciphertext.data(), ciphertext.size(), short_key.get(), &decoded_size),
            &std::free);
        DOCTEST_REQUIRE(decoded != nullptr);
        DOCTEST_CHECK(std::string(static_cast<const char*>(decoded.get()), decoded_size) == message);
    }
}

DOCTEST_TEST_CASE("XXTEA preserves full-width and zero-terminated key normalization") {
    const std::string message = "message";
    const std::array<char, 16> full{'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    DOCTEST_CHECK(encrypt(message, full.data()) == encrypt(message, "0123456789abcdefignored"));
    const std::array<char, 16> embedded{'k', '\0', 'x', 'y'};
    DOCTEST_CHECK(encrypt(message, embedded.data()) == encrypt(message, "k"));
    std::string ciphertext = "prefix:";
    DOCTEST_REQUIRE(ark::xxtea::xxtea_encrypt(message, "k", ciphertext));
    DOCTEST_CHECK(ciphertext == "prefix:" + encrypt(message, "k"));
    std::string plaintext = "prefix:";
    DOCTEST_REQUIRE(ark::xxtea::xxtea_decrypt(ciphertext.substr(7), "k", plaintext));
    DOCTEST_CHECK(plaintext == "prefix:" + message);
    DOCTEST_CHECK_FALSE(ark::xxtea::xxtea_encrypt(std::string{}, "k", ciphertext));
    DOCTEST_CHECK_FALSE(ark::xxtea::xxtea_decrypt(std::string{}, "k", plaintext));
}

TEST_CASE("XXTEA retains the established ciphertext byte order") {
    const auto to_hex = [](const std::string& bytes) {
        std::string result;
        for (unsigned char value : bytes) {
            constexpr std::string_view digits = "0123456789abcdef";
            result.push_back(digits[value >> 4]);
            result.push_back(digits[value & 15]);
        }
        return result;
    };
    CHECK(to_hex(encrypt("a", "0123456789abcdef")) == "5b218b7473eeecb6");
    CHECK(to_hex(encrypt("message", "0123456789abcdef")) == "3959c64db71c871834f8e3a5");
    CHECK(to_hex(encrypt(std::string("binary\0message", 14), "0123456789abcdef")) ==
          "9045b8ec248871d113ceeee734167c29ef93eec4");
}

TEST_CASE("XXTEA rejects invalid raw buffer arguments before reading them") {
    std::array<unsigned char, 8> bytes{};
    size_t length = 99;
    for (size_t size : {size_t{1}, size_t{2}, size_t{3}, size_t{5}, size_t{6}, size_t{7}}) {
        CAPTURE(size);
        CHECK(ark::xxtea::xxtea_decrypt(bytes.data(), size, "k", &length) == nullptr);
        CHECK(length == 99);
    }
    CHECK(ark::xxtea::xxtea_encrypt(nullptr, 1, "k", &length) == nullptr);
    CHECK(ark::xxtea::xxtea_decrypt(nullptr, 8, "k", &length) == nullptr);
    CHECK(ark::xxtea::xxtea_encrypt(bytes.data(), 1, nullptr, &length) == nullptr);
    CHECK(ark::xxtea::xxtea_decrypt(bytes.data(), 8, nullptr, &length) == nullptr);
    CHECK(ark::xxtea::xxtea_encrypt(bytes.data(), 1, "k", nullptr) == nullptr);
    CHECK(ark::xxtea::xxtea_decrypt(bytes.data(), 8, "k", nullptr) == nullptr);
    if constexpr (sizeof(size_t) > sizeof(uint32_t)) {
        const size_t too_large = size_t{(std::numeric_limits<uint32_t>::max)()} + 1;
        CHECK(ark::xxtea::xxtea_encrypt(bytes.data(), too_large, "k", &length) == nullptr);
        CHECK(ark::xxtea::xxtea_decrypt(bytes.data(), too_large, "k", &length) == nullptr);
    }
}
