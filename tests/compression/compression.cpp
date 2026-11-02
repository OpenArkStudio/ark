// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <atomic>
#include <barrier>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "../network/doctest.h"

#include <ark/compress/lz4.hpp>
#include <ark/compress/zlib.hpp>
#include <ark/compress/zstd.hpp>

namespace {

constexpr std::size_t capacity = 1024 * 1024;
constexpr const char* prefix = "existing:";

static_assert(!std::is_copy_constructible_v<ark::zstd_compress>);
static_assert(!std::is_copy_assignable_v<ark::zstd_compress>);
static_assert(std::is_nothrow_move_constructible_v<ark::zstd_compress>);
static_assert(std::is_nothrow_move_assignable_v<ark::zstd_compress>);

std::string payload(std::size_t size, uint32_t seed = 1) {
    std::string result(size, '\0');
    for (char& byte : result)
    {
        seed = seed * 1664525u + 1013904223u;
        byte = static_cast<char>(seed >> 24);
    }
    return result;
}

struct dictionary_file
{
    std::filesystem::path path;

    dictionary_file()
        : path(std::filesystem::temp_directory_path() /
               ("ark-compression-dictionary-" +
                   std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
        std::ofstream output(path, std::ios::binary);
        const auto bytes = payload(4096);
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        output.close();
        if (!output)
        {
            throw std::runtime_error("cannot write temporary dictionary");
        }
    }

    ~dictionary_file() {
        std::error_code error;
        std::filesystem::remove(path, error);
    }
};

std::string external_zlib(const std::string& input) {
    uLongf size = compressBound(static_cast<uLong>(input.size()));
    std::string output(size, '\0');
    REQUIRE(compress2(reinterpret_cast<Bytef*>(output.data()), &size, reinterpret_cast<const Bytef*>(input.data()),
                static_cast<uLong>(input.size()), 1) == Z_OK);
    output.resize(size);
    return output;
}

std::string external_lz4(const std::string& input) {
    std::string output(LZ4_compressBound(static_cast<int>(input.size())), '\0');
    const auto size = LZ4_compress_default(
        input.data(), output.data(), static_cast<int>(input.size()), static_cast<int>(output.size()));
    REQUIRE(size > 0);
    output.resize(size);
    return output;
}

std::string external_zstd(const std::string& input, bool content_size = true) {
    auto* context = ZSTD_createCCtx();
    REQUIRE(context != nullptr);
    REQUIRE_FALSE(ZSTD_isError(ZSTD_CCtx_setParameter(context, ZSTD_c_contentSizeFlag, content_size ? 1 : 0)));
    std::string output(ZSTD_compressBound(input.size()), '\0');
    const auto size = ZSTD_compress2(context, output.data(), output.size(), input.data(), input.size());
    ZSTD_freeCCtx(context);
    REQUIRE_FALSE(ZSTD_isError(size));
    output.resize(size);
    return output;
}

template<typename bound_type, typename compress_type>
void check_compression_boundary(bound_type bound, compress_type compress) {
    std::size_t rejected = 0;
    while (static_cast<std::size_t>(bound(rejected)) < capacity)
    {
        ++rejected;
    }
    REQUIRE(rejected > 0);
    auto input = payload(rejected);
    std::string output = prefix;
    CHECK(compress(input.data(), rejected - 1, output));
    CHECK(output.starts_with(prefix));
    output = prefix;
    CHECK_FALSE(compress(input.data(), rejected, output));
    CHECK(output == prefix);
    CHECK_FALSE(compress(input.data(), (std::numeric_limits<std::size_t>::max)(), output));
    CHECK(output == prefix);
}

} // namespace

TEST_CASE("Zstandard moves retain populated contexts dictionaries and frame compatibility") {
    dictionary_file dictionary;
    const auto input = payload(4096).substr(123, 2048);
    ark::zstd_compress source;
    REQUIRE(source.init_z_std_dict(1, dictionary.path.string(), 1));
    std::string frame;
    REQUIRE(source.dict_compress(1, input.data(), input.size(), frame));
    std::string output;
    REQUIRE(source.dict_decompress(1, frame.data(), frame.size(), output));
    REQUIRE(output == input);

    ark::zstd_compress moved(std::move(source));
    output = prefix;
    CHECK(moved.dict_decompress(1, frame.data(), frame.size(), output));
    CHECK(output == prefix + input);
    std::string moved_frame;
    CHECK(moved.dict_compress(1, input.data(), input.size(), moved_frame));
    CHECK(moved_frame == frame);

    ark::zstd_compress destination;
    REQUIRE(destination.init_z_std_dict(2, dictionary.path.string(), 3));
    std::string old_frame;
    REQUIRE(destination.dict_compress(2, input.data(), input.size(), old_frame));
    output.clear();
    REQUIRE(destination.dict_decompress(2, old_frame.data(), old_frame.size(), output));
    destination = std::move(moved);
    auto* same = &destination;
    destination = std::move(*same);
    output = prefix;
    CHECK(destination.dict_decompress(1, frame.data(), frame.size(), output));
    CHECK(output == prefix + input);
    moved_frame.clear();
    CHECK(destination.dict_compress(1, input.data(), input.size(), moved_frame));
    CHECK(moved_frame == frame);

    for (auto* reused : {&source, &moved})
    {
        REQUIRE(reused->init_z_std_dict(1, dictionary.path.string(), 1));
        std::string reused_frame;
        REQUIRE(reused->dict_compress(1, input.data(), input.size(), reused_frame));
        CHECK(reused_frame == frame);
    }

    const auto normal_frame = external_zstd(input);
    output = prefix;
    CHECK(destination.normal_decompress(normal_frame.data(), normal_frame.size(), output));
    CHECK(output == prefix + input);
}

TEST_CASE("compression preserves binary bytes and appends successful results") {
    const auto input = payload(8192);
    ark::zstd_compress zstd;
    for (int algorithm = 0; algorithm < 3; ++algorithm)
    {
        std::string compressed = prefix;
        const bool packed = algorithm == 0   ? ark::zlib_compress::compress(input.data(), input.size(), 1, compressed)
                            : algorithm == 1 ? ark::lz4_compress::compress(input.data(), input.size(), 1, compressed)
                                             : zstd.normal_compress(input.data(), input.size(), 1, compressed);
        REQUIRE(packed);
        REQUIRE(compressed.starts_with(prefix));
        compressed.erase(0, std::char_traits<char>::length(prefix));
        std::string output = prefix;
        const bool unpacked =
            algorithm == 0   ? ark::zlib_compress::decompress(compressed.data(), compressed.size(), output)
            : algorithm == 1 ? ark::lz4_compress::decompress(compressed.data(), compressed.size(), output)
                             : zstd.normal_decompress(compressed.data(), compressed.size(), output);
        CHECK(unpacked);
        CHECK(output == prefix + input);
    }
}

TEST_CASE("compression retains strict compression bound and rejects unrepresentable input sizes") {
    check_compression_boundary([](std::size_t size) { return compressBound(static_cast<uLong>(size)); },
        [](const char* data, std::size_t size, std::string& output) {
            return ark::zlib_compress::compress(data, size, 1, output);
        });
    check_compression_boundary([](std::size_t size) { return LZ4_compressBound(static_cast<int>(size)); },
        [](const char* data, std::size_t size, std::string& output) {
            return ark::lz4_compress::compress(data, size, 1, output);
        });
    ark::zstd_compress zstd;
    check_compression_boundary([](std::size_t size) { return ZSTD_compressBound(size); },
        [&](const char* data, std::size_t size, std::string& output) {
            return zstd.normal_compress(data, size, 1, output);
        });
    dictionary_file dictionary;
    REQUIRE(zstd.init_z_std_dict(1, dictionary.path.string(), 1));
    check_compression_boundary([](std::size_t size) { return ZSTD_compressBound(size); },
        [&](const char* data, std::size_t size, std::string& output) {
            return zstd.dict_compress(1, data, size, output);
        });

    std::string output = prefix;
    const auto too_large = static_cast<std::size_t>((std::numeric_limits<int>::max)()) + 1;
    CHECK_FALSE(ark::lz4_compress::compress("x", static_cast<std::size_t>(LZ4_MAX_INPUT_SIZE) + 1, 1, output));
    CHECK_FALSE(ark::lz4_compress::decompress("x", too_large, output));
    if constexpr (sizeof(std::size_t) > sizeof(uLong))
    {
        const auto source_size = static_cast<std::size_t>((std::numeric_limits<uLong>::max)()) + 1;
        CHECK_FALSE(ark::zlib_compress::decompress("x", source_size, output));
    }
    CHECK(output == prefix);
}

TEST_CASE("decompression accepts exactly one MiB and rejects larger outputs") {
    ark::zstd_compress zstd;
    dictionary_file dictionary;
    REQUIRE(zstd.init_z_std_dict(1, dictionary.path.string(), 1));
    for (const std::size_t size : {capacity - 1, capacity, capacity + 1})
    {
        const std::string input(size, 'a');
        const auto zlib_frame = external_zlib(input);
        const auto lz4_frame = external_lz4(input);
        const auto zstd_frame = external_zstd(input);
        std::string output = prefix;
        CHECK(ark::zlib_compress::decompress(zlib_frame.data(), zlib_frame.size(), output) == (size <= capacity));
        CHECK(output == (size <= capacity ? prefix + input : prefix));
        output = prefix;
        CHECK(ark::lz4_compress::decompress(lz4_frame.data(), lz4_frame.size(), output) == (size <= capacity));
        CHECK(output == (size <= capacity ? prefix + input : prefix));
        output = prefix;
        CHECK(zstd.normal_decompress(zstd_frame.data(), zstd_frame.size(), output) == (size <= capacity));
        CHECK(output == (size <= capacity ? prefix + input : prefix));
        // A raw-content dictionary has ID zero, matching this standard frame.
        output = prefix;
        CHECK(zstd.dict_decompress(1, zstd_frame.data(), zstd_frame.size(), output) == (size <= capacity));
        CHECK(output == (size <= capacity ? prefix + input : prefix));
    }
}

TEST_CASE("corrupt truncated and unknown-size frames preserve caller output") {
    ark::zstd_compress zstd;
    dictionary_file dictionary;
    REQUIRE(zstd.init_z_std_dict(1, dictionary.path.string(), 1));
    const std::string input = payload(512);
    const auto zlib_frame = external_zlib(input);
    const auto lz4_frame = external_lz4(input);
    const auto zstd_frame = external_zstd(input);
    std::string output = prefix;
    CHECK_FALSE(ark::zlib_compress::decompress(zlib_frame.data(), zlib_frame.size() - 1, output));
    CHECK_FALSE(ark::lz4_compress::decompress(lz4_frame.data(), lz4_frame.size() - 1, output));
    CHECK_FALSE(zstd.normal_decompress(zstd_frame.data(), zstd_frame.size() - 1, output));
    CHECK_FALSE(zstd.dict_decompress(1, zstd_frame.data(), zstd_frame.size() - 1, output));
    CHECK_FALSE(ark::zlib_compress::decompress("bad", 3, output));
    CHECK_FALSE(ark::lz4_compress::decompress("\xff", 1, output));
    const std::string different_dictionary("\x28\xb5\x2f\xfd\x21\x01\x00\x01\x00\x00", 10);
    REQUIRE(ZSTD_getFrameContentSize(different_dictionary.data(), different_dictionary.size()) == 0);
    REQUIRE(ZSTD_getDictID_fromFrame(different_dictionary.data(), different_dictionary.size()) == 1);
    CHECK_FALSE(zstd.dict_decompress(1, different_dictionary.data(), different_dictionary.size(), output));
    for (const auto& frame : {std::string("bad"), external_zstd(input, false),
             std::string("\x28\xb5\x2f\xfd\xe0\x01\x00\x00\x00\x01\x00\x00\x00", 13)})
    {
        CHECK_FALSE(zstd.normal_decompress(frame.data(), frame.size(), output));
        CHECK_FALSE(zstd.dict_decompress(1, frame.data(), frame.size(), output));
    }
    CHECK(output == prefix);
}

TEST_CASE("empty inputs retain each codec's existing behavior") {
    ark::zstd_compress zstd;
    dictionary_file dictionary;
    REQUIRE(zstd.init_z_std_dict(1, dictionary.path.string(), 1));
    std::string compressed;
    std::string output = prefix;
    REQUIRE(ark::zlib_compress::compress("", 0, 1, compressed));
    CHECK(ark::zlib_compress::decompress(compressed.data(), compressed.size(), output));
    compressed.clear();
    REQUIRE(ark::lz4_compress::compress("", 0, 1, compressed));
    CHECK_FALSE(ark::lz4_compress::decompress(compressed.data(), compressed.size(), output));
    compressed.clear();
    REQUIRE(zstd.normal_compress("", 0, 1, compressed));
    CHECK(zstd.normal_decompress(compressed.data(), compressed.size(), output));
    compressed.clear();
    REQUIRE(zstd.dict_compress(1, "", 0, compressed));
    CHECK(zstd.dict_decompress(1, compressed.data(), compressed.size(), output));
    CHECK(output == prefix);
}

TEST_CASE("parallel compression calls own their output buffers") {
    dictionary_file dictionary;
    ark::zstd_compress normal_zstd;
    constexpr int workers = 6;
    std::barrier start(workers);
    std::atomic<bool> passed = true;
    std::vector<std::jthread> threads;
    for (int worker = 0; worker < workers; ++worker)
    {
        threads.emplace_back([&, worker] {
            // Dictionary contexts belong to one thread; sharing one instance is unsupported.
            ark::zstd_compress zstd;
            if (!zstd.init_z_std_dict(1, dictionary.path.string(), 1))
            {
                passed = false;
            }
            start.arrive_and_wait();
            for (int iteration = 0; iteration < 20; ++iteration)
            {
                const auto input = payload(128 * 1024 + iteration, worker * 100 + iteration + 1);
                std::string packed;
                std::string output = prefix;
                if (!ark::zlib_compress::compress(input.data(), input.size(), 1, packed) ||
                    !ark::zlib_compress::decompress(packed.data(), packed.size(), output) || output != prefix + input)
                {
                    passed = false;
                }
                packed.clear();
                output = prefix;
                if (!ark::lz4_compress::compress(input.data(), input.size(), 1, packed) ||
                    !ark::lz4_compress::decompress(packed.data(), packed.size(), output) || output != prefix + input)
                {
                    passed = false;
                }
                packed.clear();
                output = prefix;
                if (!normal_zstd.normal_compress(input.data(), input.size(), 1, packed) ||
                    !normal_zstd.normal_decompress(packed.data(), packed.size(), output) || output != prefix + input)
                {
                    passed = false;
                }
                packed.clear();
                output = prefix;
                if (!zstd.dict_compress(1, input.data(), input.size(), packed) ||
                    !zstd.dict_decompress(1, packed.data(), packed.size(), output) || output != prefix + input)
                {
                    passed = false;
                }
            }
        });
    }
    threads.clear();
    CHECK(passed.load());
}
