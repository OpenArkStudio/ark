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

#include <spdlog/fmt/fmt.h>

#include "ark/platform.hpp"

// Input param type
#ifndef IN
#define IN
#endif

// Output param type
#ifndef OUT
#define OUT
#endif

// Input and output param type
#ifndef INOUT
#define INOUT
#endif

#define MAX_NAME 256
#define MAX_BUF 256

#ifndef MAX_PATH
#define MAX_PATH 256
#endif

#ifndef ARK_DOCKER_ALPINE
#ifndef ARK_PLATFORM_DARWIN
#ifndef strlcpy
/*
 * Copy src to string dst of size siz.  At most siz-1 characters
 * will be copied.  Always NUL terminates (unless siz == 0).
 * Returns strlen(src); if retval >= siz, truncation occurred.
 */
inline size_t strlcpy(char* dst, const char* src, size_t siz) {
    char* d = dst;
    const char* s = src;
    size_t n = siz;

    /* Copy as many bytes as will fit */
    if ((n != 0) && ((--n) != 0)) {
        do {
            if ((*d++ = *s++) == 0) {
                break;
            }
        } while (--n != 0);
    }

    /* Not enough room in dst, add NUL and traverse rest of src */
    if (n == 0) {
        if (siz != 0) {
            *d = '\0'; /* NUL-terminate dst */
        }

        while (*s++)
            ;
    }

    return (s - src - 1); /* count does not include NUL */
}
#endif // strlcpy
#endif // ARK_PLATFORM_DARWIN
#endif // ARK_DOCKER_ALPINE

#ifdef ARK_PLATFORM_WIN

// Windows
#define ARK_SPRINTF sprintf_s
#define ARK_STRICMP _stricmp
#define ARK_STRNCPY strlcpy
#define ARK_ASSERT(exp_, msg_, file_, func_)                                                                           \
    do {                                                                                                               \
        if (!(exp_)) {                                                                                                 \
            std::string error_message("Message:");                                                                     \
            error_message += msg_ + std::string(" don't exist or some warning") + std::string("\n\nFile:") +           \
                             std::string(file_) + std::string("\n Function:") + func_;                                 \
            MessageBox(0, TEXT(error_message.c_str()), TEXT("Error_" #exp_), MB_RETRYCANCEL | MB_ICONERROR);           \
        }                                                                                                              \
        assert(exp_);                                                                                                  \
    } while (false)

#define ARK_EXPORT_FUNC extern "C" __declspec(dllexport)
#define ARK_EXPORT __declspec(dllexport)
#define ARK_IMPORT __declspec(dllimport)
#define ARK_UNUSED
#define FORCE_INLINE __forceinline

#define DYNLIB_HANDLE library_instance
#define DYNLIB_LOAD(a) LoadLibraryEx((a), NULL, LOAD_WITH_ALTERED_SEARCH_PATH)
#define DYNLIB_GETSYM(a, b) GetProcAddress((a), (b))
#define DYNLIB_UNLOAD(a) FreeLibrary((a))

struct HINSTANCE__;
typedef struct HINSTANCE__* library_instance;

#define ARK_FOLDER_SEP '\\'
#define FORCE_INLINE __forceinline
#define BIG_CONSTANT(x) (x)

#elif defined(ARK_PLATFORM_LINUX)

// Linux
#define ARK_SPRINTF snprintf
#define ARK_STRICMP strcasecmp
#define ARK_STRNCPY strlcpy
#define ARK_ASSERT(exp_, msg_, file_, func_)                                                                           \
    do {                                                                                                               \
        if ((exp_))                                                                                                    \
            break;                                                                                                     \
        assert(exp_);                                                                                                  \
    } while (false)

#define ARK_EXPORT_FUNC extern "C" __attribute((visibility("default")))
#define ARK_EXPORT __attribute((visibility("default")))
#define ARK_IMPORT __attribute((visibility("default")))
#define ARK_UNUSED __attribute__((unused))

#define DYNLIB_HANDLE void*
#define DYNLIB_LOAD(a) dlopen((a), RTLD_LAZY | RTLD_GLOBAL)
#define DYNLIB_GETSYM(a, b) dlsym((a), (b))
#define DYNLIB_UNLOAD(a) dlclose((a))

#define ARK_FOLDER_SEP '/'

#define FORCE_INLINE inline __attribute__((always_inline))
#define BIG_CONSTANT(x) (x##LLU)

#elif defined(ARK_PLATFORM_DARWIN)

#define ARK_SPRINTF snprintf
#define ARK_STRICMP strcasecmp
#define ARK_STRNCPY strlcpy
#define ARK_ASSERT(exp_, msg_, file_, func_)                                                                           \
    do {                                                                                                               \
        if ((exp_))                                                                                                    \
            break;                                                                                                     \
        assert(exp_);                                                                                                  \
    } while (0)

#define ARK_EXPORT_FUNC extern "C" __attribute((visibility("default")))
#define ARK_EXPORT __attribute((visibility("default")))
#define ARK_IMPORT __attribute((visibility("default")))
#define ARK_UNUSED __attribute__((unused))

#define DYNLIB_HANDLE void*
#define DYNLIB_LOAD(a) dlopen((a), RTLD_LAZY | RTLD_GLOBAL)
#define DYNLIB_GETSYM(a, b) dlsym((a), (b))
#define DYNLIB_UNLOAD(a) dlclose((a))

#define ARK_FOLDER_SEP '/'
#define FORCE_INLINE inline __attribute__((always_inline))
#define BIG_CONSTANT(x) (x##LLU)

#endif

/* NO-OP for little-endian platforms */
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__)
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define BYTESWAP32(x) (x)
#define BYTESWAP64(x) (x)
#endif
/* if __BYTE_ORDER__ is not predefined (like FreeBSD), use arch */
#elif defined(__i386) || defined(__x86_64) || defined(__alpha) || defined(__vax)

#define BYTESWAP32(x) (x)
#define BYTESWAP64(x) (x)
/* use __builtin_bswap32 if available */
#elif defined(__GNUC__) || defined(__clang__)
#ifdef __has_builtin
#if __has_builtin(__builtin_bswap32)
#define BYTESWAP32(x) __builtin_bswap32(x)
#endif // __has_builtin(__builtin_bswap32)
#if __has_builtin(__builtin_bswap64)
#define BYTESWAP64(x) __builtin_bswap64(x)
#endif // __has_builtin(__builtin_bswap64)
#endif // __has_builtin
#endif // defined(__GNUC__) || defined(__clang__)

/* last resort (big-endian w/o __builtin_bswap) */
#ifndef BYTESWAP32
#define BYTESWAP32(x)                                                                                                  \
    ((((x) & 0xFF) << 24) | (((x) >> 24) & 0xFF) | (((x) & 0x0000FF00) << 8) | (((x) & 0x00FF0000) >> 8))
#endif

#ifndef BYTESWAP64
#define BYTESWAP64(x)                                                                                                  \
    (((uint64_t)(x) << 56) | (((uint64_t)(x) << 40) & 0X00FF000000000000ULL) |                                         \
        (((uint64_t)(x) << 24) & 0X0000FF0000000000ULL) | (((uint64_t)(x) << 8) & 0X000000FF00000000ULL) |             \
        (((uint64_t)(x) >> 8) & 0X00000000FF000000ULL) | (((uint64_t)(x) >> 24) & 0X0000000000FF0000ULL) |             \
        (((uint64_t)(x) >> 40) & 0X000000000000FF00ULL) | ((uint64_t)(x) >> 56))
#endif

#define ARK_ASSERT_RET_VAL(exp_, val)                                                                                  \
    do {                                                                                                               \
        if ((exp_))                                                                                                    \
            break;                                                                                                     \
        assert(exp_);                                                                                                  \
        return val;                                                                                                    \
    } while (false)

#define ARK_ASSERT_RET_VAL_NO_EFFECT(exp_, val)                                                                        \
    do {                                                                                                               \
        if ((exp_))                                                                                                    \
            break;                                                                                                     \
        return val;                                                                                                    \
    } while (false)

#define ARK_ASSERT_BREAK(exp_)                                                                                         \
    if (!(exp_)) {                                                                                                     \
        assert(exp_);                                                                                                  \
        break;                                                                                                         \
    }

#define ARK_ASSERT_CONTINUE(exp_)                                                                                      \
    if (!(exp_)) {                                                                                                     \
        assert(exp_);                                                                                                  \
        continue;                                                                                                      \
    }

#define ARK_ASSERT_RET_NONE(exp_)                                                                                      \
    do {                                                                                                               \
        if ((exp_))                                                                                                    \
            break;                                                                                                     \
        assert(exp_);                                                                                                  \
        return;                                                                                                        \
    } while (false)

#define ARK_ASSERT_NO_EFFECT(exp_)                                                                                     \
    do {                                                                                                               \
        if (exp_)                                                                                                      \
            break;                                                                                                     \
        assert(exp_);                                                                                                  \
    } while (false)

#ifndef ARK_FUNCTION_LINE
#define ARK_FUNCTION_LINE __FUNCTION__, __LINE__
#endif

#ifndef ARK_FILE_FUNCTION_LINE
#define ARK_FILE_FUNCTION_LINE __FILE__, __FUNCTION__, __LINE__
#endif

#ifndef ARK_NEW
#define ARK_NEW new (std::nothrow) // when new failed, return nullptr
#endif

#ifndef ARK_NEW_ARRAY
#define ARK_NEW_ARRAY(element_type, size) new element_type[(size)];
#endif

#ifndef ARK_NEW_ARRAY_RET
#define ARK_NEW_ARRAY_RET(element_type, size)                                                                          \
    do {                                                                                                               \
        return new (std::nothrow) element_type[(size)];                                                                \
    } while (false);
#endif // ARK_NEW_ARRAY_RET

#ifndef ARK_DELETE
#define ARK_DELETE(p)                                                                                                  \
    do {                                                                                                               \
        if ((p) != nullptr) {                                                                                          \
            delete (p);                                                                                                \
            (p) = nullptr;                                                                                             \
        }                                                                                                              \
    } while (false);
#endif

#ifndef ARK_DELETE_ARRAY
#define ARK_DELETE_ARRAY(element_type, p)                                                                              \
    do {                                                                                                               \
        if ((p) != nullptr) {                                                                                          \
            element_type* t_ptr = static_cast<element_type*>((p));                                                     \
            delete[] t_ptr;                                                                                            \
            t_ptr = nullptr;                                                                                           \
        }                                                                                                              \
    } while (false);
#endif

/// math: is zero
#define IS_FLOAT_ZERO(type, value) (std::abs(value) < std::numeric_limits<type>::epsilon())

#define IS_FLOAT_EQUAL(type, lhs, rhs) (std::abs(lhs - rhs) < std::numeric_limits<type>::epsilon())

#define ARK_FORMAT(my_fmt, ...) fmt::format(fmt::runtime((my_fmt)) __VA_OPT__(, ) __VA_ARGS__)
#define ARK_FORMAT_FUNCTION(my_fmt, ...)                                                                               \
    fmt::format(fmt::runtime(std::string("[{}:{}]") + (my_fmt)), ARK_FUNCTION_LINE __VA_OPT__(, ) __VA_ARGS__);

#define GET_CLASS_NAME(class_name) (typeid(class_name).name())
//////////////////////////////////////////////////////////////////////////
// Plugin macros
#define ARK_DECLARE_PLUGIN(plugin_type)                                                                                \
    class plugin_type final : public plugin {                                                                          \
    public:                                                                                                            \
        int get_plugin_version() override {                                                                            \
            return 0;                                                                                                  \
        }                                                                                                              \
        const std::string get_plugin_name() override {                                                                 \
            return GET_CLASS_NAME(plugin_type);                                                                        \
        }                                                                                                              \
        void install() override;                                                                                       \
        void uninstall() override;                                                                                     \
        plugin_manager* get_plugin_manager() const override {                                                          \
            return plugin_manager_;                                                                                    \
        }                                                                                                              \
        void set_plugin_manager(plugin_manager* p) override {                                                          \
            ARK_ASSERT_RET_NONE(p != nullptr);                                                                         \
            plugin_manager_ = p;                                                                                       \
        }                                                                                                              \
        static std::string const& get_plugin_conf() {                                                                  \
            return plugin_conf_;                                                                                       \
        }                                                                                                              \
        void set_plugin_conf(std::string const& value) override {                                                      \
            plugin_conf_ = value;                                                                                      \
        }                                                                                                              \
                                                                                                                       \
    private:                                                                                                           \
        std::unordered_map<std::string, module*> modules_;                                                             \
        plugin_manager* plugin_manager_{nullptr};                                                                      \
        static std::string plugin_conf_;                                                                               \
    };

#define ARK_DECLARE_PLUGIN_DLL_FUNCTION(plugin_type)                                                                   \
    ARK_EXPORT_FUNC void DllEntryPlugin(                                                                               \
        plugin_manager* plugin_manager_ptr, std::string const& plugin_name, logger* logger) {                          \
        logger::init(logger);                                                                                          \
        plugin_manager_ptr->register_plugin<plugin_type>(plugin_name);                                                 \
    }                                                                                                                  \
    ARK_EXPORT_FUNC void DllExitPlugin(plugin_manager* plugin_manager_ptr) {                                           \
        plugin_manager_ptr->unregister_plugin<plugin_type>();                                                          \
    }                                                                                                                  \
    std::string plugin_type::plugin_conf_ = ""; // init the plugin_conf in here cuz the duplicate symbol

#define ARK_REGISTER_MODULE(module_type, implementation_type)                                                          \
    do {                                                                                                               \
        ARK_ASSERT_RET_NONE((std::is_base_of<module, module_type>::value));                                            \
        ARK_ASSERT_RET_NONE((std::is_base_of<module_type, implementation_type>::value));                               \
        module* registered_module = ARK_NEW implementation_type();                                                     \
        ARK_ASSERT_RET_NONE(registered_module != nullptr);                                                             \
        registered_module->set_plugin_manager(get_plugin_manager());                                                   \
        registered_module->set_name(GET_CLASS_NAME(module_type));                                                      \
        get_plugin_manager()->add_module(registered_module->get_name(), registered_module);                            \
        modules_.insert(std::make_pair(registered_module->get_name(), registered_module));                             \
        if (typeid(&module::update) != typeid(&implementation_type::update)) {                                         \
            get_plugin_manager()->add_update_module(registered_module);                                                \
        }                                                                                                              \
    } while (0)

#define ARK_UNREGISTER_MODULE(module_type, implementation_type)                                                        \
    {                                                                                                                  \
        ARK_ASSERT_RET_NONE((std::is_base_of<module, module_type>::value));                                            \
        ARK_ASSERT_RET_NONE((std::is_base_of<module_type, implementation_type>::value));                               \
        module* registered_module = dynamic_cast<module*>(get_plugin_manager()->template find_module<module_type>());  \
        ARK_ASSERT_RET_NONE(registered_module != nullptr);                                                             \
        get_plugin_manager()->remove_module(registered_module->get_name());                                            \
        get_plugin_manager()->remove_update_module(registered_module->get_name());                                     \
        modules_.erase(registered_module->get_name());                                                                 \
        ARK_DELETE(registered_module);                                                                                 \
    }

//////////////////////////////////////////////////////////////////////////
// Module macros
#define ARK_DECLARE_MODULE_FUNCTIONS                                                                                   \
public:                                                                                                                \
    plugin_manager* get_plugin_manager() const override {                                                              \
        return plugin_manager_;                                                                                        \
    }                                                                                                                  \
    void set_plugin_manager(plugin_manager* p) override {                                                              \
        ARK_ASSERT_RET_NONE(p != nullptr);                                                                             \
        plugin_manager_ = p;                                                                                           \
    }                                                                                                                  \
    const std::string& get_name() const override {                                                                     \
        return name_;                                                                                                  \
    }                                                                                                                  \
    void set_name(const std::string& value) override {                                                                 \
        name_ = value;                                                                                                 \
    }                                                                                                                  \
                                                                                                                       \
protected:                                                                                                             \
    template<typename module_type>                                                                                     \
    module_type* find_module() {                                                                                       \
        return get_plugin_manager()->template find_module<module_type>();                                              \
    }                                                                                                                  \
                                                                                                                       \
private:                                                                                                               \
    plugin_manager* plugin_manager_{nullptr};                                                                          \
    std::string name_{};
////////////////////////////////////////////////////////////////////////////

// clear player data time
constexpr int clear_hour = 5;
