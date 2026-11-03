// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

#include <cerrno>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>

#include <spdlog/common.h>

namespace ark::detail {

inline std::filesystem::path logging_path_from_utf8(std::string_view value) {
    return std::filesystem::path(std::u8string(value.begin(), value.end()));
}

inline std::string logging_path_to_utf8(const std::filesystem::path& value) {
    const auto bytes = value.u8string();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

inline std::filesystem::path logging_native_path(const spdlog::filename_t& value) {
#if defined(_WIN32) && defined(SPDLOG_WCHAR_FILENAMES)
    return std::filesystem::path(value);
#else
    return logging_path_from_utf8(value);
#endif
}

inline spdlog::filename_t logging_filename(const std::filesystem::path& value) {
#if defined(_WIN32) && defined(SPDLOG_WCHAR_FILENAMES)
    return value.native();
#else
    return logging_path_to_utf8(value);
#endif
}

// Framework sinks use native filesystem paths without changing spdlog's
// filename_t or the ABI of an independently built spdlog dependency.
class logging_file
{
public:
    void open(const spdlog::filename_t& filename, bool truncate = false) {
        close();
        filename_ = logging_native_path(filename);
        for (int attempt = 0; attempt < 5; ++attempt)
        {
            std::error_code ignored;
            if (!filename_.parent_path().empty())
            {
                std::filesystem::create_directories(filename_.parent_path(), ignored);
            }
            if (truncate)
            {
                // Keep writes in append mode, including after truncation, as
                // external tools may rotate or truncate the open log file.
                std::ofstream truncated(filename_, std::ios::binary | std::ios::trunc);
                if (!truncated)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }
            }
            stream_.clear();
            stream_.open(filename_, std::ios::binary | std::ios::app);
            if (stream_)
            {
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        fail("Failed opening file ");
    }

    void reopen(bool truncate) {
        if (filename_.empty())
        {
            spdlog::throw_spdlog_ex("Failed re opening file - was not opened before");
        }
        open(logging_filename(filename_), truncate);
    }

    void close() {
        if (stream_.is_open())
        {
            stream_.close();
        }
    }

    void flush() {
        stream_.flush();
        if (!stream_)
        {
            fail("Failed flush to file ");
        }
    }

    void write(const spdlog::memory_buf_t& buffer) {
        if (!stream_.is_open())
        {
            return;
        }
        stream_.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        if (!stream_)
        {
            fail("Failed writing to file ");
        }
    }

    std::size_t size() {
        if (!stream_.is_open())
        {
            fail("Cannot use size() on closed file ");
        }
        stream_.seekp(0, std::ios::end);
        const auto position = stream_.tellp();
        if (position == std::streampos(-1))
        {
            fail("Failed sizing file ");
        }
        return static_cast<std::size_t>(position);
    }

private:
    [[noreturn]] void fail(const char* reason) const {
        spdlog::throw_spdlog_ex(std::string(reason) + logging_path_to_utf8(filename_), errno);
    }

    std::filesystem::path filename_;
    std::ofstream stream_;
};

} // namespace ark::detail
