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

#include <cstring>

#include "ark/define.hpp"
#include "ark/enum.hpp"
#include "ark/kernel/node.hpp"
#include "ark/macros.hpp"

namespace ark {

class data
{
public:
    data& operator=(const data& rhs) = delete;

    inline static void* get_user_data(void* value) {
        return (char*)value + sizeof(size_t);
    }

    inline static size_t get_user_data_size(void* value) {
        return *((size_t*)value);
    }

    inline static size_t get_raw_user_data_size(size_t size) {
        return sizeof(size_t) + size;
    }

    inline static void init_raw_user_data(void* p, const void* data_ptr, size_t size) {
        *((size_t*)p) = size;
        memcpy((char*)p + sizeof(size_t), data_ptr, size);
    }

    bool equal(const data& src) {
        if (this->get_type() != src.get_type())
        {
            return false;
        }

        bool success = false;
        switch (this->get_type())
        {
        case data_type::dt_boolean:
            success = (get_bool() == src.get_bool());
            break;
        case data_type::dt_int32:
            success = (get_int() == src.get_int());
            break;
        case data_type::dt_int64:
            success = (get_int64() == src.get_int64());
            break;
        case data_type::dt_guid:
#if defined(ARK_USE_GUID_32)
            success = (get_int() == src.get_int());
#else
            success = (get_int64() == src.get_int64());
#endif
            break;
        case data_type::dt_uint32:
            success = (get_u_int() == src.get_u_int());
            break;
        case data_type::dt_uint64:
            success = (get_u_int64() == src.get_u_int64());
            break;
        case data_type::dt_float:
            success = (get_float() == src.get_float());
            break;
        case data_type::dt_double:
            success = (get_double() == src.get_double());
            break;
        case data_type::dt_string:
            success = equal(src.get_string());
            break;
        case data_type::dt_pointer:
            success = (get_pointer() == src.get_pointer());
            break;
        case data_type::dt_userdata:
            break;
        default:
            ARK_ASSERT_NO_EFFECT(false);
            break;
        }

        return success;
    }

    bool equal(const bool value) {
        if (this->get_type() != data_type::dt_boolean)
        {
            return false;
        }

        return get_bool() == value;
    }

    bool equal(const int value) {
        if (this->get_type() != data_type::dt_int32)
        {
            return false;
        }

        return get_int() == value;
    }

    bool equal(const int64_t value) {
        if (this->get_type() != data_type::dt_int64)
        {
            return false;
        }

        return get_int64() == value;
    }

    bool equal(const float value) {
        if (this->get_type() != data_type::dt_float)
        {
            return false;
        }

        return get_float() == value;
    }

    bool equal(const double value) {
        if (this->get_type() != data_type::dt_double)
        {
            return false;
        }

        return get_double() == value;
    }

    bool equal(const char* value) {
        if (this->get_type() != data_type::dt_string)
        {
            return false;
        }

        const char* current = get_string();
        return current == value || (current != nullptr && value != nullptr && std::strcmp(current, value) == 0);
    }

    bool equal(const void* value) {
        if (this->get_type() != data_type::dt_pointer)
        {
            return false;
        }

        return get_pointer() == value;
    }

    virtual data_type get_type() const = 0;

    virtual void set_default_value(data_type type) = 0;
    virtual bool is_null_value() const = 0;

    // Get data
    virtual bool get_bool() const = 0;
    virtual int get_int() const = 0;
    virtual int64_t get_int64() const = 0;
    virtual uint32_t get_u_int() const = 0;
    virtual uint64_t get_u_int64() const = 0;
    virtual float get_float() const = 0;
    virtual double get_double() const = 0;
    virtual const char* get_string() const = 0;
    virtual void* get_pointer() const = 0;
    virtual const void* get_user_data(size_t& size) const = 0;
    virtual void* get_raw_user_data() const = 0;

    // Set data
    virtual void set_unknown() = 0;
    virtual void set_bool(bool value) = 0;
    virtual void set_int(int value) = 0;
    virtual void set_int64(int64_t value) = 0;
    virtual void set_u_int(uint32_t value) = 0;
    virtual void set_u_int64(uint64_t value) = 0;
    virtual void set_float(float value) = 0;
    virtual void set_double(double value) = 0;
    virtual void set_string(const char* value) = 0;
    virtual void set_pointer(void* value) = 0;
    virtual void set_user_data(const void* value, size_t size) = 0;
    virtual void set_raw_user_data(void* value) = 0;

    virtual void assign(const data& src) = 0;
    virtual size_t get_mem_usage() const = 0;
    virtual std::string to_string() const = 0;
    virtual ~data() = default;

    virtual bool from(const node* data_ptr) = 0;
};

} // namespace ark
