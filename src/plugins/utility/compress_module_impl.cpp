/*
 * This source file is part of ARK
 * For the latest info, see https://github.com/ArkNX
 *
 * Copyright (c) 2013-2020 ArkNX authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"),
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

// #include "base/AFLZ4.hpp"
#include "ark/compress/zlib.hpp"
#include "ark/compress/zstd.hpp"
#include "ark/define.hpp"
#include "ark/logger.hpp"
#include "ark/module_base.hpp"
#include "ark/utility/compress_module.hpp"
#include "utility_plugin.hpp"

namespace ark {

class compress_module_impl final : public module_base<compress_module> {

private:
    std::unique_ptr<zstd_compress> zstd_{nullptr};

public:
    bool init() override;

    bool compress(IN const compress_type type, IN const char* in_data, IN std::size_t in_size,
        OUT std::string& compressed_buff, IN uint8_t compress_level = 1) override;

    bool decompress(IN const compress_type type, IN const char* in_data, IN std::size_t in_size,
        OUT std::string& decompressed_buff) override;

protected:
    bool load_config();

    bool z_std_normal_compress(IN const char* in_data, IN std::size_t in_size, OUT std::string& compressed_buff,
        IN uint8_t compress_level = 1);

    bool z_std_normal_decompress(IN const char* in_data, IN std::size_t in_size, OUT std::string& decompressed_buff);

    // bool LZ4Compress(IN const char* in_data, IN std::size_t in_size, OUT std::string& compressed_buff,
    //     IN uint8_t compress_level = 1) override;
    // bool LZ4NormalDecompress(
    //     IN const char* in_data, IN std::size_t in_size, OUT std::string& decompressed_buff) override;

    bool zlib_compress(IN const char* in_data, IN std::size_t in_size, OUT std::string& compressed_buff,
        IN uint8_t compress_level = 1);
    bool zlib_normal_decompress(IN const char* in_data, IN std::size_t in_size, OUT std::string& decompressed_buff);
};

bool compress_module_impl::init() {
    zstd_ = std::make_unique<zstd_compress>();

    if (!load_config()) {
        return false;
    }

    // Init zstd dict

    // other

    return true;
}

bool compress_module_impl::compress(IN const compress_type type, IN const char* in_data, IN std::size_t in_size,
    OUT std::string& compressed_buff, IN uint8_t compress_level) {
    bool result = false;
    switch (type) {
    case compress_type::compress_type_zlib:
        result = zlib_compress(in_data, in_size, compressed_buff, compress_level);
        break;
    case compress_type::compress_type_zstd:
        result = z_std_normal_compress(in_data, in_size, compressed_buff, compress_level);
        break;
    default:
        ARK_LOG_ERROR("no this compress type = {}", static_cast<int>(type));
        break;
    }

    return result;
}

bool compress_module_impl::decompress(
    IN const compress_type type, IN const char* in_data, IN std::size_t in_size, OUT std::string& decompressed_buff) {
    bool result = false;
    switch (type) {
    case compress_type::compress_type_zlib:
        result = zlib_normal_decompress(in_data, in_size, decompressed_buff);
        break;
    case compress_type::compress_type_zstd:
        result = z_std_normal_decompress(in_data, in_size, decompressed_buff);
        break;
    default:
        ARK_LOG_ERROR("no this compress type = {}", static_cast<int>(type));
        break;
    }

    return result;
}

bool compress_module_impl::z_std_normal_compress(IN const char* in_data, IN std::size_t in_size,
    OUT std::string& compressed_buff, IN uint8_t compress_level /* = 1*/) {
    return zstd_->normal_compress(in_data, in_size, compress_level, compressed_buff);
}

bool compress_module_impl::z_std_normal_decompress(
    IN const char* in_data, IN std::size_t in_size, OUT std::string& decompressed_buff) {
    return zstd_->normal_decompress(in_data, in_size, decompressed_buff);
}

// bool AFCCompressModule::LZ4Compress(IN const char* in_data, IN std::size_t in_size, OUT std::string& compressed_buff,
//     IN uint8_t compress_level /* = 1*/)
// {
//     return AFLZ4Compress::Compress(in_data, in_size, compress_level, compressed_buff);
// }

// bool AFCCompressModule::LZ4NormalDecompress(
//     IN const char* in_data, IN std::size_t in_size, OUT std::string& decompressed_buff)
// {
//     return AFLZ4Compress::Decompress(in_data, in_size, decompressed_buff);
// }

bool compress_module_impl::zlib_compress(IN const char* in_data, IN std::size_t in_size,
    OUT std::string& compressed_buff, IN uint8_t compress_level /* = 1*/) {
    return zlib_compress::compress(in_data, in_size, compress_level, compressed_buff);
}

bool compress_module_impl::zlib_normal_decompress(
    IN const char* in_data, IN std::size_t in_size, OUT std::string& decompressed_buff) {
    return zlib_compress::decompress(in_data, in_size, decompressed_buff);
}

bool compress_module_impl::load_config() {
    // TODO: load zstd dicts and other args
    return true;
}

namespace detail {
std::unique_ptr<compress_module> make_compress_module() {
    return std::make_unique<compress_module_impl>();
}
} // namespace detail

} // namespace ark
