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

#include <lz4.h>

#include "ark/macros.hpp"

namespace ark {

#define PRE_ALLOCATED_LZ4_BUFF_SIZE (1024 * 1024)

class lz4_compress {
public:
    static bool compress(
        IN const char* in_data, IN std::size_t in_size, IN int c_level, OUT std::string& compress_buff) {
        if (in_size > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
            return false;
        }

        const int source_size = static_cast<int>(in_size);
        const int pre_buff_size = LZ4_compressBound(source_size);
        if (pre_buff_size <= 0 || pre_buff_size >= PRE_ALLOCATED_LZ4_BUFF_SIZE) {
            return false;
        }

        std::vector<char> pre_buffer(pre_buff_size);
        const int compressed_size = LZ4_compress_fast(in_data, pre_buffer.data(), source_size, pre_buff_size, c_level);
        if (compressed_size <= 0) {
            return false;
        }

        compress_buff.append(pre_buffer.data(), compressed_size);
        return true;
    }

    static bool decompress(IN const char* in_data, IN std::size_t in_size, OUT std::string& decompress_buff) {
        if (in_size > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
            return false;
        }

        std::vector<char> pre_buffer(PRE_ALLOCATED_LZ4_BUFF_SIZE);
        int const decompressed_size = LZ4_decompress_safe(
            in_data, pre_buffer.data(), static_cast<int>(in_size), static_cast<int>(pre_buffer.size()));
        if (decompressed_size <= 0) {
            return false;
        }

        decompress_buff.append(pre_buffer.data(), decompressed_size);
        return true;
    }
};

} // namespace ark
