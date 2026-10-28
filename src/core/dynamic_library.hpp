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

#include <algorithm>
#include <string>
#include <utility>

#include "ark/logger.hpp"
#include "ark/macros.hpp"

namespace ark {

namespace detail {
struct dynamic_library_test_access;
}

class dynamic_library
{
public:
    explicit dynamic_library(const std::string& lib_name)
        : name_(lib_name) {
#ifdef ARK_RUN_MODE_DEBUG
        name_.append("_d");
#endif

#ifdef ARK_PLATFORM_WIN
        name_.append(".dll");
#elif defined ARK_PLATFORM_LINUX
        name_.append(".so");
#elif defined ARK_PLATFORM_DARWIN
        name_.append(".dylib");
#endif

        ARK_LOG_INFO("LoadPlugin: {}", name_);
    }

    bool load(const std::string& path) {
#ifdef ARK_PLATFORM_WIN
        const auto result = load_windows_library(path + name_);
        lib_inst_ = result.first;
        if (!lib_inst_)
        {
            // Preserve the native failure after temporary path buffers retire.
            SetLastError(result.second);
        }
#else
        std::string dynamic_lib_path = path + name_;
        lib_inst_ = DYNLIB_LOAD(dynamic_lib_path.c_str());
#endif

        return lib_inst_ != nullptr;
    }

    bool unload() {
        return unload_with([](DYNLIB_HANDLE handle) { return DYNLIB_UNLOAD(handle); });
    }

    // Get the name of the library
    const std::string& get_name(void) const {
        return name_;
    }

    DYNLIB_HANDLE get_symbol(const std::string& symbol_name) {
        return (DYNLIB_HANDLE)DYNLIB_GETSYM(lib_inst_, symbol_name.c_str());
    }

private:
    friend struct detail::dynamic_library_test_access;

#ifdef ARK_PLATFORM_WIN
    static std::pair<DYNLIB_HANDLE, DWORD> load_windows_library(const std::string& path) {
        const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.c_str(), -1, nullptr, 0);
        if (count == 0)
        {
            return {nullptr, GetLastError()};
        }
        std::wstring requested(count, L'\0');
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.c_str(), -1, requested.data(), count) == 0)
        {
            return {nullptr, GetLastError()};
        }
        const auto capacity = GetFullPathNameW(requested.c_str(), 0, nullptr, nullptr);
        if (capacity == 0)
        {
            return {nullptr, GetLastError()};
        }
        std::wstring absolute(capacity, L'\0');
        const auto length = GetFullPathNameW(requested.c_str(), capacity, absolute.data(), nullptr);
        if (length == 0)
        {
            return {nullptr, GetLastError()};
        }
        if (length >= capacity)
        {
            return {nullptr, ERROR_INSUFFICIENT_BUFFER};
        }
        absolute.resize(length);
        std::replace(absolute.begin(), absolute.end(), L'/', L'\\');
        // An absolute native-separator path makes the existing altered search
        // policy well-defined, without changing process-wide DLL directories.
        const auto handle = LoadLibraryExW(absolute.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        return {reinterpret_cast<DYNLIB_HANDLE>(handle), handle ? ERROR_SUCCESS : GetLastError()};
    }
#endif

    static bool native_unload_succeeded(int result) {
#ifdef ARK_PLATFORM_WIN
        return result != 0;
#else
        return result == 0;
#endif
    }

    template<typename close_type>
    bool unload_with(close_type close) {
        if (!lib_inst_)
        {
            return true;
        }
        if (!native_unload_succeeded(close(lib_inst_)))
        {
            return false;
        }
        lib_inst_ = nullptr;
        return true;
    }

    std::string name_{};
    DYNLIB_HANDLE lib_inst_{nullptr};
};

} // namespace ark
