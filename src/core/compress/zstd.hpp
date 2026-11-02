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

#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <utility>
#include <vector>

#include <zstd.h>

#include "ark/macros.hpp"

namespace ark {

#define PRE_ALLOCATED_ZSTD_BUFF_SIZE (1024 * 1024)

/**
 * zstd usage and some functions
 * 1. simple compress for normal using, because we don't need to generate dict for every data type.
 * 2. different big data use different dict, you need generate those dicts using the commands below first.
 * (Plz see the zstd readme to find how to generate dict)
 * TODO: 3. add streaming compress for some situations.
 */

class zstd_compress final
{
private:
    struct resource_deleter
    {
        void operator()(ZSTD_CCtx* value) const noexcept {
            ZSTD_freeCCtx(value);
        }

        void operator()(ZSTD_DCtx* value) const noexcept {
            ZSTD_freeDCtx(value);
        }

        void operator()(ZSTD_CDict* value) const noexcept {
            ZSTD_freeCDict(value);
        }

        void operator()(ZSTD_DDict* value) const noexcept {
            ZSTD_freeDDict(value);
        }

        void operator()(void* value) const noexcept {
            free(value);
        }
    };

    struct dictionary
    {
        std::unique_ptr<ZSTD_CDict, resource_deleter> compress;
        std::unique_ptr<ZSTD_DDict, resource_deleter> decompress;
    };

    std::map<uint16_t, dictionary> dictionaries_;
    // Destroy cached contexts before the dictionaries they may reference.
    std::unique_ptr<ZSTD_CCtx, resource_deleter> compress_ctx_;
    std::unique_ptr<ZSTD_DCtx, resource_deleter> decompress_ctx_;

public:
    zstd_compress() = default;
    ~zstd_compress() = default;
    zstd_compress(const zstd_compress&) = delete;
    zstd_compress& operator=(const zstd_compress&) = delete;
    zstd_compress(zstd_compress&&) = default;

    zstd_compress& operator=(zstd_compress&& other) noexcept {
        static_assert(noexcept(dictionaries_ = std::move(other.dictionaries_)));
        if (this != &other)
        {
            compress_ctx_ = std::move(other.compress_ctx_);
            decompress_ctx_ = std::move(other.decompress_ctx_);
            dictionaries_ = std::move(other.dictionaries_);
        }
        return *this;
    }

    /// simple compress and decompress
    bool normal_compress(
        IN const char* in_data, IN std::size_t in_size, IN uint8_t compress_level, OUT std::string& compressed_buff) {
        if (in_size >= PRE_ALLOCATED_ZSTD_BUFF_SIZE)
        {
            return false;
        }

        size_t const pre_buff_size = ZSTD_compressBound(in_size); // calc needed size
        if (pre_buff_size >= PRE_ALLOCATED_ZSTD_BUFF_SIZE)
        {
            return false;
        }

        std::vector<char> pre_buffer(pre_buff_size);
        std::size_t const compressed_size =
            ZSTD_compress(pre_buffer.data(), pre_buff_size, in_data, in_size, compress_level);
        if (ZSTD_isError(compressed_size))
        {
            return false;
        }

        compressed_buff.append(pre_buffer.data(), compressed_size);

        return true;
    }

    bool normal_decompress(IN const char* in_data, IN std::size_t in_size, OUT std::string& decompressed_buff) {
        unsigned long long const r_size = ZSTD_getFrameContentSize(in_data, in_size);
        if (r_size == ZSTD_CONTENTSIZE_ERROR || r_size == ZSTD_CONTENTSIZE_UNKNOWN ||
            r_size > PRE_ALLOCATED_ZSTD_BUFF_SIZE)
        {
            return false;
        }

        std::vector<char> pre_buffer(PRE_ALLOCATED_ZSTD_BUFF_SIZE);
        size_t const decompressed_size =
            ZSTD_decompress(pre_buffer.data(), static_cast<size_t>(r_size), in_data, in_size);
        // When zstd knows the content size, it will error if it doesn't match.
        if (ZSTD_isError(decompressed_size) || decompressed_size != r_size)
        {
            return false;
        }

        decompressed_buff.append(pre_buffer.data(), decompressed_size);
        return true;
    }

    /// Dictionary calls mutate cached contexts. Use one instance per thread or external synchronization.
    /**
     * Create zstd compress dict and decompress dict from dict file
     */
    bool init_z_std_dict(uint16_t dict_type, std::string const& filename, int c_level) {
        std::size_t dict_size = 0;
        std::unique_ptr<void, resource_deleter> dict_buffer(malloc_and_load_file_or_die(filename.c_str(), &dict_size));
        dictionary candidate;
        candidate.compress.reset(ZSTD_createCDict(dict_buffer.get(), dict_size, c_level));
        ARK_ASSERT_RET_VAL(candidate.compress != nullptr, false);
        candidate.decompress.reset(ZSTD_createDDict(dict_buffer.get(), dict_size));
        ARK_ASSERT_RET_VAL(candidate.decompress != nullptr, false);

        // Keep the first dictionary for a key, including the successful duplicate result.
        dictionaries_.try_emplace(dict_type, std::move(candidate));
        return true;
    }

    bool dict_compress(
        IN uint16_t dict_type, IN const char* in_data, IN std::size_t in_size, OUT std::string& compressed_buff) {
        if (in_size >= PRE_ALLOCATED_ZSTD_BUFF_SIZE)
        {
            return false;
        }

        size_t const pre_buff_size = ZSTD_compressBound(in_size);
        if (pre_buff_size >= PRE_ALLOCATED_ZSTD_BUFF_SIZE)
        {
            return false;
        }

        if (compress_ctx_ == nullptr)
        {
            compress_ctx_.reset(ZSTD_createCCtx());
        }

        ARK_ASSERT_RET_VAL(compress_ctx_ != nullptr, false);

        auto iter = dictionaries_.find(dict_type);
        ARK_ASSERT_RET_VAL(iter != dictionaries_.end(), false);

        std::vector<char> pre_buffer(pre_buff_size);
        std::size_t const compressed_size = ZSTD_compress_usingCDict(
            compress_ctx_.get(), pre_buffer.data(), pre_buff_size, in_data, in_size, iter->second.compress.get());
        if (ZSTD_isError(compressed_size))
        {
            return false;
        }

        compressed_buff.append(pre_buffer.data(), compressed_size);
        return true;
    }

    bool dict_decompress(
        IN uint16_t dict_type, IN const char* in_data, IN std::size_t in_size, OUT std::string& decompressed_buff) {
        unsigned long long const r_size = ZSTD_getFrameContentSize(in_data, in_size);
        if (r_size == ZSTD_CONTENTSIZE_ERROR || r_size == ZSTD_CONTENTSIZE_UNKNOWN ||
            r_size > PRE_ALLOCATED_ZSTD_BUFF_SIZE)
        {
            return false;
        }

        auto iter = dictionaries_.find(dict_type);
        ARK_ASSERT_RET_VAL(iter != dictionaries_.end(), false);
        ARK_ASSERT_RET_VAL(iter->second.decompress != nullptr, false);

        unsigned const expected_dict_id = ZSTD_getDictID_fromDDict(iter->second.decompress.get());
        unsigned const actual_dict_id = ZSTD_getDictID_fromFrame(in_data, in_size);
        if (actual_dict_id != expected_dict_id)
        {
            return false;
        }

        if (decompress_ctx_ == nullptr)
        {
            decompress_ctx_.reset(ZSTD_createDCtx());
        }

        ARK_ASSERT_RET_VAL(decompress_ctx_ != nullptr, false);

        std::vector<char> pre_buffer(PRE_ALLOCATED_ZSTD_BUFF_SIZE);
        std::size_t decompressed_size = ZSTD_decompress_usingDDict(decompress_ctx_.get(), pre_buffer.data(),
            static_cast<size_t>(r_size), in_data, in_size, iter->second.decompress.get());
        // When zstd knows the content size, it will error if it doesn't match.
        if (ZSTD_isError(decompressed_size) || decompressed_size != r_size)
        {
            return false;
        }

        decompressed_buff.append(pre_buffer.data(), decompressed_size);
        return true;
    }

    /// streaming compress and decompress

protected:
    /*
     * Define the returned error code from utility functions.
     */
    enum common_error_code
    {
        error_fsize = 1,
        error_fopen = 2,
        error_fclose = 3,
        error_fread = 4,
        error_fwrite = 5,
        error_load_file = 6,
        error_save_file = 7,
        error_malloc = 8,
        error_large_file = 9,
    };

    /*! malloc_orDie() :
     * Allocate memory.
     *
     * @return If successful this function returns a pointer to allo-
     * cated memory.  If there is an error, this function will send that
     * error to stderr and exit.
     */
    static void* malloc_or_die(std::size_t size) {
        void* const buff = malloc(size);
        if (buff)
        {
            return buff;
        }

        /* error */
        perror("malloc");
        exit(common_error_code::error_malloc);
    }

    /*! fsize_orDie() :
     * Get the size of a given file path.
     *
     * @return The size of a given file path.
     */
    static size_t fsize_or_die(const char* filename) {
        struct stat st;
        if (stat(filename, &st) != 0)
        {
            /* error */
            perror(filename);
            exit(common_error_code::error_fsize);
        }

        off_t const file_size = st.st_size;
        size_t const size = (size_t)file_size;
        /* 1. fileSize should be non-negative,
         * 2. if off_t -> size_t type conversion results in discrepancy,
         *    the file size is too large for type size_t.
         */
        if ((file_size < 0) || (file_size != (off_t)size))
        {
            fprintf(stderr, "%s : filesize too large \n", filename);
            exit(common_error_code::error_large_file);
        }

        return size;
    }

    /*! fopen_orDie() :
     * Open a file using given file path and open option.
     *
     * @return If successful this function will return a FILE pointer to an
     * opened file otherwise it sends an error to stderr and exits.
     */
    static FILE* fopen_or_die(const char* filename, const char* instruction) {
        FILE* const in_file = fopen(filename, instruction);
        if (in_file)
        {
            return in_file;
        }
        /* error */
        perror(filename);
        exit(common_error_code::error_fopen);
    }

    /*! loadFile_orDie() :
     * load file into buffer (memory).
     *
     * Note: This function will send an error to stderr and exit if it
     * cannot read data from the given file path.
     *
     * @return If successful this function will load file into buffer and
     * return file size, otherwise it will printout an error to stderr and exit.
     */
    static size_t load_file_or_die(const char* file_name, void* buffer, size_t buffer_size) {
        size_t const file_size = fsize_or_die(file_name);
        ARK_ASSERT_RET_VAL(file_size <= buffer_size, 0);

        FILE* const in_file = fopen_or_die(file_name, "rb");
        size_t const read_size = fread(buffer, 1, file_size, in_file);
        if (read_size != (size_t)file_size)
        {
            fprintf(stderr, "fread: %s : %s \n", file_name, strerror(errno));
            exit(common_error_code::error_fread);
        }
        fclose(in_file); /* can't fail, read only */
        return file_size;
    }

    /*! mallocAndLoadFile_orDie() :
     * allocate memory buffer and then load file into it.
     *
     * Note: This function will send an error to stderr and exit if memory allocation
     * fails or it cannot read data from the given file path.
     *
     * @return If successful this function will return buffer and bufferSize(=fileSize),
     * otherwise it will printout an error to stderr and exit.
     */
    static void* malloc_and_load_file_or_die(const char* file_name, size_t* buffer_size) {
        size_t const file_size = fsize_or_die(file_name);
        *buffer_size = file_size;
        void* const buffer = malloc_or_die(*buffer_size);
        load_file_or_die(file_name, buffer, *buffer_size);
        return buffer;
    }
};

} // namespace ark
