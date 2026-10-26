// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <ark/hash/murmur_hash.hpp>

// Keep the second inclusion separate to retain the include-guard check.
#include <ark/hash/murmur_hash.hpp>

void murmur_hash_header_check() {
    const uint8_t key[16]{};
    uint8_t output[16]{};
    ark::murmur_hash hash;
    hash.murmur_hash3_x86_32(key, 16, 0, output);
    hash.murmur_hash3_x86_128(key, 16, 0, output);
    hash.murmur_hash3_x64_128(key, 16, 0, output);
    hash.murmur_hash3_x86_32_legacy_msvc(key, 16, 0, output);
    hash.murmur_hash3_x86_128_legacy_msvc(key, 16, 0, output);
    hash.murmur_hash3_x64_128_legacy_msvc(key, 16, 0, output);
    hash.murmur_hash2(key, 16, output);
}
