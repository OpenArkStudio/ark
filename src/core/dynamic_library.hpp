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
        std::string dynamic_lib_path = path + name_;
        lib_inst_ = DYNLIB_LOAD(dynamic_lib_path.c_str());

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
