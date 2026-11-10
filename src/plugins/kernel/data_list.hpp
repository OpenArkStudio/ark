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

#include "ark/define.hpp"
#include "ark/kernel/data.hpp"
#include "ark/misc.hpp"
#include "ark/string.hpp"

namespace ark {

class data_list {
public:
    virtual bool concat(const data_list& src) = 0;
    virtual bool split(const std::string& src, const std::string& split) = 0;

    virtual bool append(const data& data) = 0;
    virtual bool append(const data_list& src, size_t start, size_t count) = 0;

    virtual void clear() = 0;

    virtual bool empty() const = 0;

    virtual size_t get_count() const = 0;

    virtual data_type get_type(size_t index) const = 0;

    // add data
    virtual bool add_bool(bool value) = 0;
    virtual bool add_int(int value) = 0;
    virtual bool add_int64(int64_t value) = 0;
    virtual bool add_u_int(uint32_t value) = 0;
    virtual bool add_u_int64(uint64_t value) = 0;
    virtual bool add_float(float value) = 0;
    virtual bool add_double(double value) = 0;
    virtual bool add_string(const char* value) = 0;
    virtual bool add_pointer(void* value) = 0;
    virtual bool add_user_data(const void* data_ptr, size_t size) = 0;
    virtual bool add_raw_user_data(void* value) = 0;

    // get data
    virtual bool get_bool(size_t index) const = 0;
    virtual int get_int(size_t index) const = 0;
    virtual int64_t int64(size_t index) const = 0;
    virtual uint32_t u_int(size_t index) const = 0;
    virtual uint64_t u_int64(size_t index) const = 0;
    virtual float get_float(size_t index) const = 0;
    virtual double get_double(size_t index) const = 0;
    virtual const char* string(size_t index) const = 0;
    virtual void* pointer(size_t index) const = 0;
    virtual const void* user_data(size_t index, size_t& size) const = 0;
    virtual void* raw_user_data(size_t index) const = 0;
    virtual const std::string to_string(size_t index) = 0;

    // get memory usage
    virtual size_t get_mem_usage() const = 0;

    template<typename... args_type>
    bool type_ex(const data_type arg1, const args_type&... args) const {
        bool success = true;
        data_type values[] = {arg1, args...};

        int index = 0;

        for (auto v : values) {
            if (v == data_type::dt_empty) {
                success = false;
                return success;
            }

            data_type var_type = get_type(index);

            if (var_type != v) {
                success = false;
                break;
            }

            ++index;
        }

        return success;
    }

    bool to_afi_data(size_t index, data& source_data) const {
        if (get_type(index) != source_data.get_type()) {
            return false;
        }

        switch (source_data.get_type()) {
        case data_type::dt_boolean:
            source_data.set_bool(get_bool(index));
            break;
        case data_type::dt_int32:
            source_data.set_int(get_int(index));
            break;
        case data_type::dt_int64:
            source_data.set_int64(int64(index));
            break;
        case data_type::dt_float:
            source_data.set_float(get_float(index));
            break;
        case data_type::dt_double:
            source_data.set_double(get_double(index));
            break;
        case data_type::dt_string:
            source_data.set_string(string(index));
            break;
        default:
            ARK_ASSERT_RET_VAL(0, false);
            break;
        }

        return true;
    }

    bool equal(size_t index, const data& source_data) const {
        if (get_type(index) != source_data.get_type()) {
            return false;
        }

        switch (source_data.get_type()) {
        case data_type::dt_boolean:
            return source_data.get_bool() == get_bool(index);
            break;
        case data_type::dt_int32:
            return source_data.get_int() == get_int(index);
            break;
        case data_type::dt_int64:
            return source_data.get_int64() == int64(index);
            break;
        case data_type::dt_uint32:
            return source_data.get_u_int() == u_int(index);
            break;
        case data_type::dt_uint64:
            return source_data.get_u_int64() == u_int64(index);
            break;
        case data_type::dt_float:
            return IS_FLOAT_ZERO(float, source_data.get_float() - get_float(index));
            break;
        case data_type::dt_double:
            return IS_FLOAT_ZERO(double, source_data.get_double() - get_double(index));
            break;
        case data_type::dt_string:
            return std::string(source_data.get_string()) == std::string(string(index));
            break;
        default:
            ARK_ASSERT_RET_VAL(0, false);
            break;
        }

        return true;
    }

    // operator <<
    inline data_list& operator<<(bool value) {
        bool success = add_bool(value);
        ARK_ASSERT_NO_EFFECT(success);
        return *this;
    }

    inline data_list& operator<<(int8_t value) {
        bool success = add_int(value);
        ARK_ASSERT_NO_EFFECT(success);
        return *this;
    }

    inline data_list& operator<<(uint8_t value) {
        bool success = add_int(value);
        ARK_ASSERT_NO_EFFECT(success);
        return *this;
    }

    inline data_list& operator<<(int16_t value) {
        bool success = add_int(value);
        ARK_ASSERT_NO_EFFECT(success);
        return *this;
    }

    inline data_list& operator<<(uint16_t value) {
        bool success = add_int(value);
        ARK_ASSERT_NO_EFFECT(success);
        return *this;
    }

    inline data_list& operator<<(int32_t value) {
        bool success = add_int(value);
        ARK_ASSERT_NO_EFFECT(success);
        return *this;
    }

    inline data_list& operator<<(uint32_t value) {
        bool success = add_u_int(value);
        ARK_ASSERT_NO_EFFECT(success);
        return *this;
    }

    inline data_list& operator<<(int64_t value) {
        bool success = add_int64(value);
        ARK_ASSERT_NO_EFFECT(success);
        return *this;
    }

    inline data_list& operator<<(uint64_t value) {
        bool success = add_u_int64(value);
        ARK_ASSERT_NO_EFFECT(success);
        return *this;
    }

    inline data_list& operator<<(float value) {
        bool success = add_float(value);
        ARK_ASSERT_NO_EFFECT(success);
        return *this;
    }

    inline data_list& operator<<(double value) {
        bool success = add_double(value);
        ARK_ASSERT_NO_EFFECT(success);
        return *this;
    }

    inline data_list& operator<<(const char* value) {
        bool success = add_string(value);
        ARK_ASSERT_NO_EFFECT(success);
        return *this;
    }

    inline data_list& operator<<(const std::string& value) {
        bool success = add_string(value.c_str());
        ARK_ASSERT_NO_EFFECT(success);
        return *this;
    }

    inline data_list& operator<<(const small_string<char, 128>& value) {
        bool success = add_string(value.c_str());
        ARK_ASSERT_NO_EFFECT(success);
        return *this;
    }

    inline data_list& operator<<(const data_list& value) {
        bool success = concat(value);
        ARK_ASSERT_NO_EFFECT(success);
        return *this;
    }
};

} // namespace ark
