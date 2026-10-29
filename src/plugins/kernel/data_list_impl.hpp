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
#include <new>
#include <stdexcept>
#include <string>

#include "ark/kernel/data.hpp"
#include "ark/kernel/data_list.hpp"
#include "ark/misc.hpp"

namespace ark {

class data_list_alloc
{
public:
    void* alloc(size_t size) {
        ARK_NEW_ARRAY_RET(char, size);
    }

    void free(void* ptr, size_t size) {
        ARK_DELETE_ARRAY(char, ptr);
    }

    void swap(data_list_alloc& src) {
        // Do nothing
    }
};

template<size_t data_size, size_t buffer_size, typename allocator_type = data_list_alloc>
class base_data_list final : public data_list
{
private:
    using self_t = base_data_list<data_size, buffer_size, allocator_type>;

    struct dynamic_data_t
    {
        data_type type;

        union
        {
            bool bool_value;
            int int_value;
            uint32_t uint32_value;
            int64_t int64_value;
            uint64_t uint64_value;
            float float_value;
            double double_value;
            size_t string_offset;
            void* pointer_value;
            size_t user_data_offset;
            guid_t guid_value;
        };
    };

public:
    base_data_list() {
        assert(data_size > 0);
        assert(buffer_size > 0);

        data_ = inline_data_;
        data_capacity_ = data_size;
        data_size_ = 0;

        buffer_ = inline_buffer_;
        buffer_capacity_ = buffer_size;
        buffer_size_ = 0;
    }

    base_data_list(const char* source, int length, char separator) {
        assert(data_size > 0);
        assert(buffer_size > 0);

        data_ = inline_data_;
        data_capacity_ = data_size;
        data_size_ = 0;

        buffer_ = inline_buffer_;
        buffer_capacity_ = buffer_size;
        buffer_size_ = 0;

        int begin = 0;
        int end = 0;

        for (int i = 0; i < length; i++)
        {
            if (source[i] == separator)
            {
                end = i;

                if (!add_string(&source[begin], end - begin))
                {
                    ARK_ASSERT_NO_EFFECT(0);
                }

                if (i + 1 < length)
                {
                    begin = i + 1;
                }
            }
        }

        if (end < length)
        {
            end = length;

            if (!add_string(&source[begin], end - begin))
            {
                ARK_ASSERT_NO_EFFECT(0);
            }
        }
    }

    base_data_list(const self_t& src) {
        assert(data_size > 0);
        assert(buffer_size > 0);

        data_ = inline_data_;
        data_capacity_ = data_size;
        data_size_ = 0;

        buffer_ = inline_buffer_;
        buffer_capacity_ = buffer_size;
        buffer_size_ = 0;
        inner_append(src, 0, src.get_count());
    }

    base_data_list(const data_list& src) {
        assert(data_size > 0);
        assert(buffer_size > 0);

        data_ = inline_data_;
        data_capacity_ = data_size;
        data_size_ = 0;

        buffer_ = inline_buffer_;
        buffer_capacity_ = buffer_size;
        buffer_size_ = 0;
        inner_append(src, 0, src.get_count());
    }

    virtual ~base_data_list() {
        release();
    }

    self_t operator=(const self_t& src) {
        release();

        data_ = inline_data_;
        data_capacity_ = data_size;
        data_size_ = 0;

        buffer_ = inline_buffer_;
        buffer_capacity_ = buffer_size;
        buffer_size_ = 0;
        inner_append(src, 0, src.get_count());

        return *this;
    }

    void release() {
        if (data_capacity_ > data_size)
        {
            allocator_.free(data_, data_capacity_ * sizeof(dynamic_data_t));
        }

        if (buffer_capacity_ > buffer_size)
        {
            allocator_.free(buffer_, buffer_capacity_ * sizeof(char));
        }
    }

    bool concat(const data_list& src) override {
        inner_append(src, 0, src.get_count());
        return true;
    }

    bool split(const std::string& src, const std::string& split) override {
        clear();

        std::string tmp(src);
        if (tmp.empty())
        {
            return true;
        }

        std::string tmp_split(split);
        std::string::size_type pos;
        tmp += tmp_split;
        std::string::size_type size = tmp.length();

        for (std::string::size_type i = 0; i < size; ++i)
        {
            pos = int(tmp.find(tmp_split, i));
            if (pos < size)
            {
                std::string sub = tmp.substr(i, pos - i);
                add_string(sub.c_str());

                i = pos + tmp_split.size() - 1;
            }
        }

        return true;
    }

    bool append(const data& data) override {
        inner_append(data);
        return true;
    }

    bool append(const data_list& src, size_t start, size_t count) override {
        if (start >= src.get_count())
        {
            return false;
        }

        size_t end = start + count;

        if (end > src.get_count())
        {
            return false;
        }

        inner_append(src, start, end);
        return true;
    }

    void clear() override {
        data_size_ = 0;
        buffer_size_ = 0;
    }

    bool empty() const override {
        return (0 == data_size_);
    }

    size_t get_count() const override {
        return data_size_;
    }

    data_type get_type(size_t index) const override {
        if (index >= data_size_)
        {
            return data_type::dt_empty;
        }

        return data_[index].type;
    }

    // add data
    bool add_bool(bool value) override {
        dynamic_data_t* p = add_dynamic_data();
        p->type = data_type::dt_boolean;
        p->bool_value = value;
        return true;
    }

    bool add_int(int value) override {
        dynamic_data_t* p = add_dynamic_data();
        p->type = data_type::dt_int32;
        p->int_value = value;
        return true;
    }

    bool add_int64(int64_t value) override {
        dynamic_data_t* p = add_dynamic_data();
        p->type = data_type::dt_int64;
        p->int64_value = value;
        return true;
    }

    bool add_u_int(uint32_t value) override {
        dynamic_data_t* p = add_dynamic_data();
        p->type = data_type::dt_uint32;
        p->uint32_value = value;
        return true;
    }

    bool add_u_int64(uint64_t value) override {
        dynamic_data_t* p = add_dynamic_data();
        p->type = data_type::dt_uint64;
        p->uint64_value = value;
        return true;
    }

    bool add_float(float value) override {
        dynamic_data_t* p = add_dynamic_data();
        p->type = data_type::dt_float;
        p->float_value = value;
        return true;
    }

    bool add_double(double value) override {
        dynamic_data_t* p = add_dynamic_data();
        p->type = data_type::dt_double;
        p->double_value = value;
        return true;
    }

    bool add_string(const char* value) override {
        assert(value != nullptr);
        const size_t length = strlen(value);
        if (length == std::numeric_limits<size_t>::max())
        {
            throw std::length_error("Kernel data list string exceeds its storage length");
        }
        const size_t value_size = length + 1;
        buffer_capacity_for(value_size);
        reserve_dynamic_data();
        const size_t offset = buffer_size_;
        char* data = add_buffer(value_size);
        memcpy(data, value, value_size);

        dynamic_data_t* p = data_ + data_size_;
        p->type = data_type::dt_string;
        p->string_offset = offset;
        ++data_size_;
        return true;
    }

    virtual bool add_string(const char* value, const int length) {
        if (length <= 0)
        {
            return false;
        }

        assert(value != nullptr);
        size_t value_size = strlen(value);

        if (value_size > (size_t)length)
        {
            value_size = (size_t)length;
        }

        value_size += 1;
        buffer_capacity_for(value_size);
        reserve_dynamic_data();
        const size_t offset = buffer_size_;
        char* data = add_buffer(value_size);
        memcpy(data, value, value_size);

        dynamic_data_t* p = data_ + data_size_;
        p->type = data_type::dt_string;
        p->string_offset = offset;
        ++data_size_;
        return true;
    }

    bool add_pointer(void* value) override {
        dynamic_data_t* p = add_dynamic_data();
        p->type = data_type::dt_pointer;
        p->pointer_value = value;
        return true;
    }

    bool add_user_data(const void* data_ptr, size_t size) override {
        assert(data_ptr != nullptr);

        const size_t value_size = data::get_raw_user_data_size(size);
        buffer_capacity_for(value_size);
        reserve_dynamic_data();
        const size_t offset = buffer_size_;
        char* value = add_buffer(value_size);
        data::init_raw_user_data(value, data_ptr, size);

        dynamic_data_t* p = data_ + data_size_;
        p->type = data_type::dt_userdata;
        p->user_data_offset = offset;
        ++data_size_;
        return true;
    }

    bool add_raw_user_data(void* value) override {
        return add_user_data(data::get_user_data(value), data::get_user_data_size(value));
    }

    // get data
    bool get_bool(size_t index) const override {
        if (index > data_size_)
        {
            return null_boolean;
        }

        if (data_[index].type == data_type::dt_boolean)
        {
            return data_[index].bool_value;
        }
        else
        {
            return null_boolean;
        }
    }

    int get_int(size_t index) const override {
        if (index > data_size_)
        {
            return null_int;
        }

        if (data_[index].type == data_type::dt_int32)
        {
            return data_[index].int_value;
        }
        else
        {
            return null_int;
        }
    }

    int64_t int64(size_t index) const override {
        if (index > data_size_)
        {
            return null_int64;
        }

        if (data_[index].type == data_type::dt_int64)
        {
            return data_[index].int64_value;
        }
        else
        {
            return null_int64;
        }
    }

    uint32_t u_int(size_t index) const override {
        if (index > data_size_)
        {
            return null_int;
        }

        if (data_[index].type == data_type::dt_uint32)
        {
            return data_[index].uint32_value;
        }
        else
        {
            return null_int;
        }
    }

    uint64_t u_int64(size_t index) const override {
        if (index > data_size_)
        {
            return null_int64;
        }

        if (data_[index].type == data_type::dt_uint64)
        {
            return data_[index].uint64_value;
        }
        else
        {
            return null_int64;
        }
    }

    float get_float(size_t index) const override {
        if (index > data_size_)
        {
            return null_float;
        }

        if (data_[index].type == data_type::dt_float)
        {
            return data_[index].float_value;
        }
        else
        {
            return null_float;
        }
    }

    double get_double(size_t index) const override {
        if (index > data_size_)
        {
            return null_double;
        }

        if (data_[index].type == data_type::dt_double)
        {
            return data_[index].double_value;
        }
        else
        {
            return null_double;
        }
    }

    const char* string(size_t index) const override {
        if (index > data_size_)
        {
            return null_str.c_str();
        }

        if (data_[index].type == data_type::dt_string)
        {
            return buffer_ + data_[index].string_offset;
        }
        else
        {
            return null_str.c_str();
        }
    }

    void* pointer(size_t index) const override {
        if (index > data_size_)
        {
            return nullptr;
        }

        if (data_[index].type == data_type::dt_pointer)
        {
            return data_[index].pointer_value;
        }
        else
        {
            return nullptr;
        }
    }

    const void* user_data(size_t index, size_t& size) const override {
        if (index > data_size_)
        {
            size = 0;
            return nullptr;
        }

        if (data_[index].type == data_type::dt_userdata)
        {
            char* p = buffer_ + data_[index].user_data_offset;
            size = data::get_user_data_size(p);
            return data::get_user_data(p);
        }
        else
        {
            size = 0;
            return nullptr;
        }
    }

    void* raw_user_data(size_t index) const override {
        if (index > data_size_)
        {
            return nullptr;
        }

        if (data_[index].type == data_type::dt_userdata)
        {
            return buffer_ + data_[index].user_data_offset;
        }
        else
        {
            return nullptr;
        }
    }

    virtual bool set_bool(size_t index, bool value) {
        if (index >= data_size_)
        {
            return false;
        }

        if (data_[index].type != data_type::dt_boolean)
        {
            return false;
        }
        else
        {
            data_[index].bool_value = value;
            return true;
        }
    }

    virtual bool set_int(size_t index, int value) {
        if (index >= data_size_)
        {
            return false;
        }

        if (data_[index].type != data_type::dt_int32)
        {
            return false;
        }
        else
        {
            data_[index].int_value = value;
            return true;
        }
    }

    virtual bool set_int64(size_t index, int64_t value) {
        if (index >= data_size_)
        {
            return false;
        }

        if (data_[index].type != data_type::dt_int64)
        {
            return false;
        }
        else
        {
            data_[index].int64_value = value;
            return true;
        }
    }

    virtual bool set_u_int(size_t index, uint32_t value) {
        if (index >= data_size_)
        {
            return false;
        }

        if (data_[index].type != data_type::dt_uint32)
        {
            return false;
        }
        else
        {
            data_[index].uint32_value = value;
            return true;
        }
    }

    virtual bool set_u_int64(size_t index, uint64_t value) {
        if (index >= data_size_)
        {
            return false;
        }

        if (data_[index].type != data_type::dt_uint64)
        {
            return false;
        }
        else
        {
            data_[index].uint64_value = value;
            return true;
        }
    }

    virtual bool set_float(size_t index, float value) {
        if (index >= data_size_)
        {
            return false;
        }

        if (data_[index].type != data_type::dt_float)
        {
            return false;
        }
        else
        {
            data_[index].float_value = value;
            return true;
        }
    }

    virtual bool set_double(size_t index, double value) {
        if (index >= data_size_)
        {
            return false;
        }

        if (data_[index].type != data_type::dt_double)
        {
            return false;
        }
        else
        {
            data_[index].double_value = value;
            return true;
        }
    }

    virtual bool set_string(size_t index, const char* value) {
        assert(value != nullptr);

        if (index >= data_size_)
        {
            return false;
        }

        if (data_[index].type != data_type::dt_string)
        {
            return false;
        }

        char* p = buffer_ + data_[index].string_offset;
        const size_t length = strlen(value);
        if (length == std::numeric_limits<size_t>::max())
        {
            throw std::length_error("Kernel data list string exceeds its storage length");
        }
        const size_t size1 = length + 1;

        if (size1 <= (strlen(p) + 1))
        {
            ARK_STRNCPY(p, value, size1);
            return true;
        }

        const size_t offset = buffer_size_;
        char* v = add_buffer(size1);
        memcpy(v, value, size1);
        data_[index].string_offset = offset;

        return true;
    }

    virtual bool set_pointer(size_t index, void* value) {
        if (index >= data_size_)
        {
            return false;
        }

        if (data_[index].type != data_type::dt_pointer)
        {
            return false;
        }
        else
        {
            data_[index].pointer_value = value;
            return true;
        }
    }

    const std::string to_string(size_t index) override {
        if (index > data_size_)
        {
            return null_str;
        }

        std::string data;
        switch (data_[index].type)
        {
        case data_type::dt_boolean:
            data = std::to_string(data_[index].bool_value);
            break;
        case data_type::dt_int32:
            data = std::to_string(data_[index].int_value);
            break;
        case data_type::dt_int64:
            data = std::to_string(data_[index].int64_value);
            break;
        case data_type::dt_uint32:
            data = std::to_string(data_[index].uint32_value);
            break;
        case data_type::dt_uint64:
            data = std::to_string(data_[index].uint64_value);
            break;
        case data_type::dt_float:
            data = std::to_string(data_[index].float_value);
            break;
        case data_type::dt_double:
            data = std::to_string(data_[index].double_value);
            break;
        case data_type::dt_string:
            data = string(index);
            break;
        case data_type::dt_pointer:
            break;
        case data_type::dt_userdata:
            break;
        default:
            assert(0);
            break;
        }

        return data;
    }

    size_t get_mem_usage() const override {
        size_t size = sizeof(self_t);

        if (data_capacity_ > data_size)
        {
            size += sizeof(dynamic_data_t) * data_capacity_;
        }

        if (buffer_capacity_ > buffer_size)
        {
            size += sizeof(char) * buffer_capacity_;
        }

        return size;
    }

protected:
    void reserve_dynamic_data() {
        if (data_size_ >= data_capacity_)
        {
            constexpr size_t maximum = std::numeric_limits<size_t>::max() / sizeof(dynamic_data_t);
            if (data_capacity_ > maximum / 2)
            {
                throw std::length_error("Kernel data list exceeds its index storage");
            }
            size_t new_size = data_capacity_ * 2;
            dynamic_data_t* p = (dynamic_data_t*)allocator_.alloc(new_size * sizeof(dynamic_data_t));
            if (p == nullptr)
            {
                throw std::bad_alloc{};
            }
            memcpy(p, data_, data_size_ * sizeof(dynamic_data_t));

            if (data_capacity_ > data_size)
            {
                allocator_.free(data_, data_capacity_ * sizeof(dynamic_data_t));
            }

            data_ = p;
            data_capacity_ = new_size;
        }
    }

    dynamic_data_t* add_dynamic_data() {
        reserve_dynamic_data();
        return data_ + data_size_++;
    }

    size_t buffer_capacity_for(size_t need_size) const {
        constexpr size_t maximum = std::numeric_limits<size_t>::max();
        if (need_size > maximum - buffer_size_)
        {
            throw std::length_error("Kernel data list exceeds its buffer storage");
        }
        const size_t new_used = buffer_size_ + need_size;
        if (new_used <= buffer_capacity_)
        {
            return buffer_capacity_;
        }
        if (buffer_capacity_ > maximum / 2)
        {
            throw std::length_error("Kernel data list buffer growth exceeds its storage length");
        }
        size_t new_size = buffer_capacity_ * 2;
        if (new_used > new_size)
        {
            if (new_used > maximum / 2)
            {
                throw std::length_error("Kernel data list buffer growth exceeds its storage length");
            }
            new_size = new_used * 2;
        }
        return new_size;
    }

    char* add_buffer(size_t need_size) {
        const size_t new_size = buffer_capacity_for(need_size);
        const size_t new_used = buffer_size_ + need_size;
        if (new_size > buffer_capacity_)
        {
            char* p = (char*)allocator_.alloc(new_size);
            if (p == nullptr)
            {
                throw std::bad_alloc{};
            }
            memcpy(p, buffer_, buffer_size_);

            if (buffer_capacity_ > buffer_size)
            {
                allocator_.free(buffer_, buffer_capacity_);
            }

            buffer_ = p;
            buffer_capacity_ = new_size;
        }

        char* ret = buffer_ + buffer_size_;
        buffer_size_ = new_used;
        return ret;
    }

    void inner_append(const data& data) {
        bool success(false);

        switch (data.get_type())
        {
        case data_type::dt_boolean:
            success = add_bool(data.get_bool());
            break;
        case data_type::dt_int32:
            success = add_int(data.get_int());
            break;
        case data_type::dt_int64:
            success = add_int64(data.get_int64());
            break;
        case data_type::dt_uint32:
            success = add_u_int(data.get_u_int());
            break;
        case data_type::dt_uint64:
            success = add_u_int64(data.get_u_int64());
            break;
        case data_type::dt_float:
            success = add_float(data.get_float());
            break;
        case data_type::dt_double:
            success = add_double(data.get_double());
            break;
        case data_type::dt_string:
            success = add_string(data.get_string());
            break;
        case data_type::dt_pointer:
            success = add_pointer(data.get_pointer());
            break;
        case data_type::dt_userdata:
        {
            size_t size;
            const void* data_ptr = data.get_user_data(size);
            success = add_user_data(data_ptr, size);
        }
        break;
        default:
            ARK_ASSERT_NO_EFFECT(0);
            break;
        }

        ARK_ASSERT_NO_EFFECT(success);
    }

    bool inner_append(const data_list& src, size_t start, size_t end) {
        bool success(false);

        for (size_t i = start; i < end; ++i)
        {
            switch (src.get_type(i))
            {
            case data_type::dt_boolean:
                success = add_bool(src.get_bool(i));
                break;
            case data_type::dt_int32:
                success = add_int(src.get_int(i));
                break;
            case data_type::dt_int64:
                success = add_int64(src.int64(i));
                break;
            case data_type::dt_uint32:
                success = add_u_int(src.u_int(i));
                break;
            case data_type::dt_uint64:
                success = add_u_int64(src.u_int64(i));
                break;
            case data_type::dt_float:
                success = add_float(src.get_float(i));
                break;
            case data_type::dt_double:
                success = add_double(src.get_double(i));
                break;
            case data_type::dt_string:
                success = add_string(src.string(i));
                break;
            case data_type::dt_pointer:
                success = add_pointer(src.pointer(i));
                break;
            case data_type::dt_userdata:
            {
                size_t size;
                const void* data_ptr = src.user_data(i, size);
                success = add_user_data(data_ptr, size);
            }
            break;
            default:
                ARK_ASSERT_NO_EFFECT(0);
                break;
            }
        }

        return success;
    }

private:
    allocator_type allocator_;
    dynamic_data_t inline_data_[data_size];
    dynamic_data_t* data_;
    size_t data_capacity_;
    size_t data_size_;
    char inline_buffer_[buffer_size];
    char* buffer_;
    size_t buffer_capacity_;
    size_t buffer_size_;
};

using data_list_impl = base_data_list<8, 128>;

} // namespace ark
