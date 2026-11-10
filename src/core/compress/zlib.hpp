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

#pragma once

#include <limits>
#include <string>
#include <vector>

#include <zlib.h>

#include "ark/macros.hpp"

namespace ark {

#define PRE_ALLOCATED_ZLIB_BUFF_SIZE (1024 * 1024)

// Some usage tips.
// compressBound() returns an upper bound on the compressed size after compress() or compress2() on sourceLen bytes.
// It would be used before a compress() or compress2() call to allocate the destination buffer.

class zlib_compress final {
public:
    static bool compress(
        IN const char* in_data, IN std::size_t in_size, IN uint8_t compress_level, OUT std::string& compress_buff) {
        if (in_size >= PRE_ALLOCATED_ZLIB_BUFF_SIZE || in_size > (std::numeric_limits<uLong>::max)()) {
            return false;
        }

        uLongf pre_buff_size = compressBound(static_cast<uLong>(in_size)); // calc needed size
        if (pre_buff_size >= PRE_ALLOCATED_ZLIB_BUFF_SIZE) {
            return false;
        }

        std::vector<unsigned char> pre_buffer(pre_buff_size);
        int ret = compress2(pre_buffer.data(), &pre_buff_size, reinterpret_cast<const Bytef*>(in_data),
            static_cast<uLong>(in_size), compress_level);
        if (ret != Z_OK) {
            return false;
        }

        compress_buff.append(reinterpret_cast<const char*>(pre_buffer.data()), pre_buff_size);
        return true;
    }

    static bool decompress(IN const char* in_data, IN std::size_t in_size, OUT std::string& decompress_buff) {
        if (in_size > (std::numeric_limits<uLong>::max)()) {
            return false;
        }

        std::vector<unsigned char> pre_buffer(PRE_ALLOCATED_ZLIB_BUFF_SIZE);
        uLongf pre_buff_size = static_cast<uLongf>(pre_buffer.size());
        uLong source_size = static_cast<uLong>(in_size);
        int ret = uncompress2(pre_buffer.data(), &pre_buff_size, reinterpret_cast<const Bytef*>(in_data), &source_size);
        if (ret != Z_OK) {
            return false;
        }

        decompress_buff.append(reinterpret_cast<const char*>(pre_buffer.data()), pre_buff_size);
        return true;
    }
};

} // namespace ark
