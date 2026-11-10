// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <new>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <type_traits>
#include <utility>
#include <vector>

#include "../network/doctest.h"
#include <zstd.h>

#include <ark/macros.hpp>

namespace {

enum resource : std::size_t {
    cctx,
    dctx,
    cdict,
    ddict,
    buffer,
    resource_count
};

std::array<int, resource_count> live{};
resource fail_resource = resource_count;
bool fail_map{};
bool fail_next_new{};

bool fail(resource kind) {
    if (fail_resource == kind) {
        fail_resource = resource_count;
        return true;
    }
    return false;
}

ZSTD_CCtx* create_cctx() {
    auto* value = fail(cctx) ? nullptr : ZSTD_createCCtx();
    live[cctx] += value != nullptr;
    return value;
}

ZSTD_DCtx* create_dctx() {
    auto* value = fail(dctx) ? nullptr : ZSTD_createDCtx();
    live[dctx] += value != nullptr;
    return value;
}

ZSTD_CDict* create_cdict(const void* data, std::size_t size, int level) {
    auto* value = fail(cdict) ? nullptr : ZSTD_createCDict(data, size, level);
    live[cdict] += value != nullptr;
    return value;
}

ZSTD_DDict* create_ddict(const void* data, std::size_t size) {
    auto* value = fail(ddict) ? nullptr : ZSTD_createDDict(data, size);
    live[ddict] += value != nullptr;
    if (value && std::exchange(fail_map, false)) {
        fail_next_new = true;
    }
    return value;
}

std::size_t free_cctx(ZSTD_CCtx* value) noexcept {
    live[cctx] -= value != nullptr;
    return ZSTD_freeCCtx(value);
}

std::size_t free_dctx(ZSTD_DCtx* value) noexcept {
    live[dctx] -= value != nullptr;
    return ZSTD_freeDCtx(value);
}

std::size_t free_cdict(ZSTD_CDict* value) noexcept {
    live[cdict] -= value != nullptr;
    return ZSTD_freeCDict(value);
}

std::size_t free_ddict(ZSTD_DDict* value) noexcept {
    live[ddict] -= value != nullptr;
    return ZSTD_freeDDict(value);
}

void* allocate_buffer(std::size_t size) {
    auto* value = std::malloc(size);
    live[buffer] += value != nullptr;
    return value;
}

void free_buffer(void* value) noexcept {
    live[buffer] -= value != nullptr;
    std::free(value);
}

struct dictionary_file {
    std::filesystem::path path;
    std::string bytes;

    explicit dictionary_file(unsigned seed)
        : path(std::filesystem::temp_directory_path() /
               ("ark-zstd-ownership-" + std::to_string(seed) + "-" +
                   std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
        , bytes(4096, '\0') {
        for (auto& byte : bytes) {
            seed = seed * 1664525u + 1013904223u;
            byte = static_cast<char>(seed >> 24);
        }
        std::ofstream output(path, std::ios::binary);
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        output.close();
        REQUIRE(output.good());
    }

    ~dictionary_file() {
        std::error_code error;
        std::filesystem::remove(path, error);
    }
};

} // namespace

// Only this executable replaces map allocation and the native resource entrypoints.
// Production inline definitions never mix with differently expanded definitions.
void* operator new(std::size_t size) {
    if (std::exchange(fail_next_new, false)) {
        throw std::bad_alloc{};
    }
    if (auto* value = std::malloc(size ? size : 1)) {
        return value;
    }
    throw std::bad_alloc{};
}

void operator delete(void* value) noexcept {
    std::free(value);
}

void operator delete(void* value, std::size_t) noexcept {
    std::free(value);
}

#define ZSTD_createCCtx create_cctx
#define ZSTD_createDCtx create_dctx
#define ZSTD_createCDict create_cdict
#define ZSTD_createDDict create_ddict
#define ZSTD_freeCCtx free_cctx
#define ZSTD_freeDCtx free_dctx
#define ZSTD_freeCDict free_cdict
#define ZSTD_freeDDict free_ddict
#define malloc allocate_buffer
#define free free_buffer
#include <ark/compress/zstd.hpp>
#undef free
#undef malloc
#undef ZSTD_freeDDict
#undef ZSTD_freeCDict
#undef ZSTD_freeDCtx
#undef ZSTD_freeCCtx
#undef ZSTD_createDDict
#undef ZSTD_createCDict
#undef ZSTD_createDCtx
#undef ZSTD_createCCtx

namespace {

std::string frame(ark::zstd_compress& codec, const std::string& input, std::uint16_t key = 1) {
    std::string result;
    REQUIRE(codec.dict_compress(key, input.data(), input.size(), result));
    std::string output = "prefix:";
    REQUIRE(codec.dict_decompress(key, result.data(), result.size(), output));
    REQUIRE(output == "prefix:" + input);
    return result;
}

void check_empty_resources() {
    CHECK(live == std::array<int, resource_count>{});
}

} // namespace

TEST_CASE("Zstandard duplicate dictionary keys retain the first pair without leaking candidates") {
    dictionary_file original(1);
    dictionary_file replacement(7);
    check_empty_resources();
    {
        ark::zstd_compress codec;
        REQUIRE(codec.init_z_std_dict(1, original.path.string(), 1));
        const auto input = original.bytes.substr(100, 2048);
        const auto first_frame = frame(codec, input);
        REQUIRE(first_frame.size() < input.size()); // Exercise actual dictionary references.
        const auto initial = live;
        for (int attempt = 0; attempt < 8; ++attempt) {
            CHECK(codec.init_z_std_dict(1, replacement.path.string(), 15));
            CHECK(live == initial);
            CHECK(frame(codec, input) == first_frame);
        }
        CHECK(live[buffer] == 0);
    }
    check_empty_resources();
}

TEST_CASE("Zstandard dictionary creation failures preserve existing entries and release partial candidates") {
    dictionary_file dictionary(1);
    check_empty_resources();
    {
        ark::zstd_compress codec;
        REQUIRE(codec.init_z_std_dict(1, dictionary.path.string(), 1));
        const auto input = dictionary.bytes.substr(100, 2048);
        const auto original = frame(codec, input);
        const auto initial = live;
        for (auto kind : {cdict, ddict}) {
            fail_resource = kind;
            CHECK_FALSE(codec.init_z_std_dict(2, dictionary.path.string(), 1));
            CHECK(fail_resource == resource_count);
            CHECK(live == initial);
            std::string output = "unchanged";
            CHECK_FALSE(codec.dict_compress(2, input.data(), input.size(), output));
            CHECK_FALSE(codec.dict_decompress(2, original.data(), original.size(), output));
            CHECK(output == "unchanged");
            CHECK(frame(codec, input) == original);
        }
        REQUIRE(codec.init_z_std_dict(2, dictionary.path.string(), 1));
        CHECK(frame(codec, input, 2) == original);
    }
    check_empty_resources();
}

TEST_CASE("Zstandard dictionary insertion exceptions leave no partial entry and allow retry") {
    dictionary_file dictionary(1);
    check_empty_resources();
    {
        ark::zstd_compress codec;
        REQUIRE(codec.init_z_std_dict(1, dictionary.path.string(), 1));
        const auto input = dictionary.bytes.substr(100, 2048);
        const auto original = frame(codec, input);
        const auto initial = live;
        fail_map = true;
        bool caught{};
        try {
            codec.init_z_std_dict(2, dictionary.path.string(), 1);
        } catch (const std::bad_alloc&) {
            caught = true;
        }
        CHECK_FALSE(fail_map);
        CHECK_FALSE(fail_next_new);
        CHECK(caught);
        CHECK(live == initial);
        std::string output = "unchanged";
        CHECK_FALSE(codec.dict_compress(2, input.data(), input.size(), output));
        CHECK_FALSE(codec.dict_decompress(2, original.data(), original.size(), output));
        CHECK(output == "unchanged");
        CHECK(frame(codec, input) == original);
        REQUIRE(codec.init_z_std_dict(2, dictionary.path.string(), 1));
        CHECK(frame(codec, input, 2) == original);
    }
    check_empty_resources();
}

TEST_CASE("Zstandard context creation failures preserve dictionaries and caller output for retry") {
    dictionary_file dictionary(1);
    check_empty_resources();
    {
        ark::zstd_compress codec;
        REQUIRE(codec.init_z_std_dict(1, dictionary.path.string(), 1));
        const auto input = dictionary.bytes.substr(100, 2048);
        std::string packed = "unchanged";
        fail_resource = cctx;
        CHECK_FALSE(codec.dict_compress(1, input.data(), input.size(), packed));
        CHECK(fail_resource == resource_count);
        CHECK(packed == "unchanged");
        CHECK(live[cctx] == 0);
        packed.clear();
        REQUIRE(codec.dict_compress(1, input.data(), input.size(), packed));
        std::string output = "prefix:";
        fail_resource = dctx;
        CHECK_FALSE(codec.dict_decompress(1, packed.data(), packed.size(), output));
        CHECK(fail_resource == resource_count);
        CHECK(output == "prefix:");
        CHECK(live[dctx] == 0);
        REQUIRE(codec.dict_decompress(1, packed.data(), packed.size(), output));
        CHECK(output == "prefix:" + input);
    }
    check_empty_resources();
}

TEST_CASE("Zstandard populated moves release overwritten owners once and retain transferred resources") {
    dictionary_file dictionary(1);
    const auto input = dictionary.bytes.substr(100, 2048);
    check_empty_resources();
    {
        ark::zstd_compress source;
        REQUIRE(source.init_z_std_dict(1, dictionary.path.string(), 1));
        const auto expected = frame(source, input);
        const auto initial = live;
        ark::zstd_compress moved(std::move(source));
        CHECK(live == initial);
        CHECK(frame(moved, input) == expected);
        ark::zstd_compress destination;
        REQUIRE(destination.init_z_std_dict(2, dictionary.path.string(), 2));
        frame(destination, input, 2);
        CHECK(live[cctx] == 2);
        CHECK(live[dctx] == 2);
        CHECK(live[cdict] == 2);
        CHECK(live[ddict] == 2);
        destination = std::move(moved);
        CHECK(live == initial);
        CHECK(frame(destination, input) == expected);
        auto* same = &destination;
        destination = std::move(*same);
        CHECK(live == initial);
        CHECK(frame(destination, input) == expected);
        std::string output = "unchanged";
        CHECK_FALSE(destination.dict_compress(2, input.data(), input.size(), output));
        CHECK(output == "unchanged");
    }
    check_empty_resources();
}

TEST_CASE("Zstandard fallible move construction preserves the source after allocation failure" *
          doctest::skip(std::is_nothrow_move_constructible_v<ark::zstd_compress>)) {
    if constexpr (!std::is_nothrow_move_constructible_v<ark::zstd_compress>) {
        dictionary_file dictionary(1);
        const auto input = dictionary.bytes.substr(100, 2048);
        check_empty_resources();
        {
            ark::zstd_compress source;
            REQUIRE(source.init_z_std_dict(1, dictionary.path.string(), 1));
            const auto expected = frame(source, input);
            const auto initial = live;
            bool caught{};
            // Arm immediately before construction; doctest assertion setup may allocate too.
            fail_next_new = true;
            try {
                ark::zstd_compress moved(std::move(source));
            } catch (const std::bad_alloc&) {
                caught = true;
            }
            const bool consumed = !std::exchange(fail_next_new, false);
            CHECK(consumed);
            CHECK(caught);
            CHECK(live == initial);
            CHECK(frame(source, input) == expected);

            ark::zstd_compress moved(std::move(source));
            CHECK(live == initial);
            CHECK(frame(moved, input) == expected);
        }
        check_empty_resources();
    }
}
