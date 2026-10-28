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

#include <string>

#include "ark/core_def.hpp"
#include "ark/kernel/data.hpp"
#include "ark/misc.hpp"
#include "ark/platform.hpp"

namespace ark {

class data_alloc_new_type
{
public:
    void* alloc(size_t size) {
        ARK_NEW_ARRAY_RET(char, size);
    }

    void free(void* ptr, size_t size) {
        ARK_DELETE_ARRAY(char, ptr);
    }

    void swap(data_alloc_new_type& src) {
        // do nothing
    }
};

template<size_t buffer_size, typename allocator_type = data_alloc_new_type>
class data_base final : public data
{
public:
    using self_t = data_base<buffer_size, allocator_type>;

    static self_t from_guid(const guid_t& value) {
        return self_t(guid_tag{}, value);
    }

    data_base() {
        type_ = data_type::dt_empty;
        int64_value_ = 0;
    }

    ~data_base() override {
        release();
    }

    data_base(const self_t& src) {
        this->type_ = src.type_;

        switch (this->type_)
        {
        case data_type::dt_boolean:
            bool_value_ = src.bool_value_;
            break;
        case data_type::dt_int32:
            int_value_ = src.int_value_;
            break;
        case data_type::dt_int64:
            int64_value_ = src.int64_value_;
            break;
        case data_type::dt_guid:
            set_guid_storage(read_guid(src));
            break;
        case data_type::dt_uint32:
            uint32_value_ = src.uint32_value_;
            break;
        case data_type::dt_uint64:
            uint64_value_ = src.uint64_value_;
            break;
        case data_type::dt_float:
            float_value_ = src.float_value_;
            break;
        case data_type::dt_double:
            double_value_ = src.double_value_;
            break;
        case data_type::dt_string:
            inner_set_string(src.get_string());
            break;
        case data_type::dt_pointer:
            pointer_value_ = src.pointer_value_;
            break;
        case data_type::dt_userdata:
            set_user_data(src);
            break;
        default:
            ARK_ASSERT_NO_EFFECT(0);
            break;
        }
    }

    explicit data_base(const data& src) {
        this->type_ = src.get_type();

        switch (this->type_)
        {
        case data_type::dt_boolean:
            bool_value_ = src.get_bool();
            break;
        case data_type::dt_int32:
            int_value_ = src.get_int();
            break;
        case data_type::dt_int64:
            int64_value_ = src.get_int64();
            break;
        case data_type::dt_guid:
            set_guid_storage(read_guid(src));
            break;
        case data_type::dt_uint32:
            uint32_value_ = src.get_u_int();
            break;
        case data_type::dt_uint64:
            uint64_value_ = src.get_u_int64();
            break;
        case data_type::dt_float:
            float_value_ = src.get_float();
            break;
        case data_type::dt_double:
            double_value_ = src.get_double();
            break;
        case data_type::dt_string:
            inner_set_string(src.get_string());
            break;
        case data_type::dt_pointer:
            pointer_value_ = src.get_pointer();
            break;
        case data_type::dt_userdata:
            set_user_data(src);
            break;
        default:
            ARK_ASSERT_NO_EFFECT(0);
            break;
        }
    }

    bool from(const node* data_ptr) override {
        if (data_ptr == nullptr)
        {
            return false;
        }

        self_t replacement;
        replacement.type_ = data_ptr->get_type();

        switch (replacement.type_)
        {
        case data_type::dt_boolean:
            replacement.bool_value_ = data_ptr->get_bool();
            break;
        case data_type::dt_int32:
            replacement.int_value_ = data_ptr->get_int32();
            break;
        case data_type::dt_int64:
            replacement.int64_value_ = data_ptr->get_int64();
            break;
        case data_type::dt_guid:
            replacement.set_guid_storage(data_ptr->get_object());
            break;
        case data_type::dt_uint32:
            replacement.uint32_value_ = data_ptr->get_u_int32();
            break;
        case data_type::dt_uint64:
            replacement.uint64_value_ = data_ptr->get_u_int64();
            break;
        case data_type::dt_float:
            replacement.float_value_ = data_ptr->get_float();
            break;
        case data_type::dt_double:
            replacement.double_value_ = data_ptr->get_double();
            break;
        case data_type::dt_string:
            replacement.inner_set_string(data_ptr->get_string().c_str());
            break;
        default:
            ARK_ASSERT_NO_EFFECT(0);
            break;
        }

        swap(replacement);
        return true;
    }

    data_base(data_type type, bool value) {
        assert(type == data_type::dt_boolean);

        type_ = data_type::dt_boolean;
        bool_value_ = value;
    }

    data_base(data_type type, int value) {
        assert(type == data_type::dt_int32);

        type_ = data_type::dt_int32;
        int_value_ = value;
    }

    data_base(data_type type, uint32_t value) {
        assert(type == data_type::dt_uint32);

        type_ = data_type::dt_uint32;
        int_value_ = value;
    }

    data_base(data_type type, int64_t value) {
        assert(type == data_type::dt_int64);

        type_ = data_type::dt_int64;
        int64_value_ = value;
    }

    data_base(data_type type, uint64_t value) {
        assert(type == data_type::dt_uint64);

        type_ = data_type::dt_uint64;
        int64_value_ = value;
    }

    data_base(data_type type, float value) {
        assert(type == data_type::dt_float);

        type_ = data_type::dt_float;
        float_value_ = value;
    }

    data_base(data_type type, double value) {
        assert(type == data_type::dt_double);

        type_ = data_type::dt_double;
        double_value_ = value;
    }

    data_base(data_type type, const char* value) {
        assert(type == data_type::dt_string);

        type_ = data_type::dt_string;
        inner_set_string(value);
    }

    data_base(data_type type, const void* value, size_t size) {
        assert(type == data_type::dt_userdata);

        type_ = data_type::dt_userdata;
        inner_set_user_data(value, size);
    }

    self_t& operator=(const self_t& src) {
        self_t tmp(src);
        swap(tmp);
        return *this;
    }

    self_t& operator=(const data& src) {
        self_t tmp(src);
        swap(tmp);
        return *this;
    }

    void swap(self_t& src) {
        data_type tmp_type = this->type_;
        int64_t tmp_value = this->int64_value_;
        // guid_t tmp_guid = this->mxGUID;
        uint32_t tmp_alloc_len = this->allocated_length_;
        char tmp_buffer[buffer_size] = {0};
        bool tmp_use_buffer = (tmp_type == data_type::dt_string) && (this->string_value_ == this->inline_buffer_);

        if (tmp_use_buffer)
        {
            memcpy(tmp_buffer, this->inline_buffer_, buffer_size);
        }

        if ((src.type_ == data_type::dt_string) && (src.string_value_ == src.inline_buffer_))
        {
            memcpy(this->inline_buffer_, src.inline_buffer_, buffer_size);
            string_value_ = inline_buffer_;
        }
        else
        {
            this->int64_value_ = src.int64_value_;
            allocated_length_ = src.allocated_length_;
        }

        this->type_ = src.type_;

        if (tmp_use_buffer)
        {
            memcpy(src.inline_buffer_, tmp_buffer, buffer_size);
            src.string_value_ = src.inline_buffer_;
        }
        else
        {
            src.int64_value_ = tmp_value;
            src.allocated_length_ = tmp_alloc_len;
        }

        src.type_ = tmp_type;
        allocator_.swap(src.allocator_);
    }

    //////////////////////////////////////////////////////////////////////////

    data_type get_type() const override {
        return type_;
    }

    void set_default_value(data_type type) override {
        switch (type)
        {
        case data_type::dt_boolean:
            set_bool(null_boolean);
            break;
        case data_type::dt_int32:
            set_int(null_int);
            break;
        case data_type::dt_int64:
            set_int64(null_int64);
            break;
        case data_type::dt_uint32:
            set_u_int(null_int);
            break;
        case data_type::dt_uint64:
            set_u_int64(null_int64);
            break;
        case data_type::dt_float:
            set_float(null_float);
            break;
        case data_type::dt_double:
            set_double(null_double);
            break;
        case data_type::dt_string:
            set_string(null_str.c_str());
            break;
        default:
            ARK_ASSERT_NO_EFFECT(0);
            break;
        }
    }

    bool is_null_value() const override {
        switch (get_type())
        {
        case data_type::dt_boolean:
            return bool_value_ == null_boolean;
            break;
        case data_type::dt_int32:
            return int_value_ == null_int;
            break;
        case data_type::dt_int64:
            return int64_value_ == null_int64;
            break;
        case data_type::dt_uint32:
            return uint32_value_ == null_int;
            break;
        case data_type::dt_uint64:
            return uint64_value_ == null_int64;
            break;
        case data_type::dt_float:
            return IS_FLOAT_ZERO(float, float_value_);
            break;
        case data_type::dt_double:
            return IS_FLOAT_ZERO(double, float_value_);
            break;
        case data_type::dt_string:
            return string_value_ == null_str.c_str();
            break;
        default:
            ARK_ASSERT_NO_EFFECT(0);
            break;
        }

        return false;
    }

    // Get data
    bool get_bool() const override {
        ARK_ASSERT_RET_VAL(type_ == data_type::dt_boolean, null_boolean);
        return bool_value_;
    }

    int get_int() const override {
#if defined(ARK_USE_GUID_32)
        if (type_ == data_type::dt_guid)
        {
            return int_value_;
        }
#endif
        ARK_ASSERT_RET_VAL(type_ == data_type::dt_int32, null_int);
        return int_value_;
    }

    int64_t get_int64() const override {
#if !defined(ARK_USE_GUID_32)
        if (type_ == data_type::dt_guid)
        {
            return int64_value_;
        }
#endif
        ARK_ASSERT_RET_VAL(type_ == data_type::dt_int64, null_int64);
        return int64_value_;
    }

    float get_float() const override {
        ARK_ASSERT_RET_VAL(type_ == data_type::dt_float, null_float);
        return float_value_;
    }

    double get_double() const override {
        ARK_ASSERT_RET_VAL(type_ == data_type::dt_double, null_double);
        return double_value_;
    }

    const char* get_string() const override {
        ARK_ASSERT_RET_VAL(type_ == data_type::dt_string, null_str.c_str());
        return string_value_;
    }

    uint32_t get_u_int() const override {
        ARK_ASSERT_RET_VAL(type_ == data_type::dt_uint32, null_int);
        return uint32_value_;
    }

    uint64_t get_u_int64() const override {
        ARK_ASSERT_RET_VAL(type_ == data_type::dt_uint64, null_int64);
        return uint64_value_;
    }

    void* get_pointer() const override {
        ARK_ASSERT_RET_VAL(type_ == data_type::dt_pointer, nullptr);
        return pointer_value_;
    }

    const void* get_user_data(size_t& size) const override {
        assert(type_ == data_type::dt_userdata);

        if (type_ != data_type::dt_userdata)
        {
            size = 0;
            return nullptr;
        }

        size = data::get_user_data_size(user_data_);
        return data::get_user_data(user_data_);
    }

    void* get_raw_user_data() const override {
        ARK_ASSERT_RET_VAL(type_ == data_type::dt_userdata, nullptr);
        return user_data_;
    }

    // Set data
    void set_unknown() override {
        release();
        type_ = data_type::dt_empty;
        int64_value_ = 0;
    }

    void set_bool(bool value) override {
        release();
        type_ = data_type::dt_boolean;
        bool_value_ = value;
    }

    void set_int(int value) override {
        release();
        type_ = data_type::dt_int32;
        int_value_ = value;
    }

    void set_int64(int64_t value) override {
        release();
        type_ = data_type::dt_int64;
        int64_value_ = value;
    }

    void set_float(float value) override {
        release();
        type_ = data_type::dt_float;
        float_value_ = value;
    }

    void set_double(double value) override {
        release();
        type_ = data_type::dt_double;
        double_value_ = value;
    }

    void set_string(const char* value) override {
        release();
        type_ = data_type::dt_string;
        inner_set_string(value);
    }

    void set_u_int(uint32_t value) override {
        release();
        type_ = data_type::dt_uint32;
        uint32_value_ = value;
    }

    void set_u_int64(uint64_t value) override {
        release();
        type_ = data_type::dt_uint64;
        uint64_value_ = value;
    }

    void set_pointer(void* value) override {
        release();
        type_ = data_type::dt_pointer;
        pointer_value_ = value;
    }

    void set_user_data(const void* value, size_t size) override {
        release();
        type_ = data_type::dt_userdata;
        inner_set_user_data(value, size);
    }

    void set_raw_user_data(void* value) override {
        set_user_data(data::get_user_data(value), data::get_user_data_size(value));
    }

    void assign(const data& src) override {
        self_t tmp(src);
        swap(tmp);
    }

    size_t get_mem_usage() const override {
        size_t size = sizeof(self_t);

        switch (type_)
        {
        case data_type::dt_string:
        {
            if (string_value_ != nullptr)
            {
                size += strlen(string_value_) + 1;
            }
        }
        break;
        case data_type::dt_userdata:
        {
            if (user_data_ != nullptr)
            {
                size += data::get_user_data_size(user_data_);
            }
        }
        break;
        default:
            break;
        }

        return size;
    }

    std::string to_string() const override {
        switch (get_type())
        {
        case data_type::dt_boolean:
            return std::to_string(this->bool_value_);
            break;
        case data_type::dt_int32:
            return std::to_string(this->int_value_);
            break;
        case data_type::dt_int64:
            return std::to_string(this->int64_value_);
            break;
        case data_type::dt_uint32:
            return std::to_string(this->uint32_value_);
            break;
        case data_type::dt_uint64:
            return std::to_string(this->uint64_value_);
            break;
        case data_type::dt_float:
            return std::to_string(this->float_value_);
            break;
        case data_type::dt_double:
            return std::to_string(this->double_value_);
            break;
        case data_type::dt_string:
            return this->string_value_;
            break;
        default:
            ARK_ASSERT_NO_EFFECT(0);
            break;
        }

        return null_str;
    }

    void release() {
        switch (type_)
        {
        case data_type::dt_string:
        {
            if (string_value_ != inline_buffer_)
            {
                allocator_.free(string_value_, allocated_length_);
                string_value_ = nullptr;
            }
        }
        break;
        case data_type::dt_userdata:
        {
            if (user_data_ != nullptr)
            {
                allocator_.free(user_data_, allocated_length_);
                user_data_ = nullptr;
            }
        }
        break;
        default:
            break;
        }
    }

protected:
    void inner_set_string(const char* value) {
        const size_t value_size = strlen(value) + 1;
        char* p = nullptr;

        if (value_size > buffer_size)
        {
            p = (char*)allocator_.alloc(value_size);
            allocated_length_ = (uint32_t)value_size;
        }
        else
        {
            p = inline_buffer_;
        }

        memcpy(p, value, value_size);
        string_value_ = p;
    }

    void inner_set_user_data(const void* data, size_t size) {
        size_t value_size = get_raw_user_data_size(size);
        char* p = (char*)allocator_.alloc(value_size);
        init_raw_user_data(p, data, size);
        user_data_ = p;
        allocated_length_ = (uint32_t)value_size;
    }

    void set_user_data(const self_t& src) {
        size_t size;
        const void* data_ptr = src.get_user_data(size);
        inner_set_user_data(data_ptr, size);
    }

    void set_user_data(const data& src) {
        size_t size;
        const void* data_ptr = src.get_user_data(size);
        inner_set_user_data(data_ptr, size);
    }

private:
    struct guid_tag
    {
    };

    data_base(guid_tag, const guid_t& value)
        : type_(data_type::dt_guid)
        , int64_value_(0) {
        set_guid_storage(value);
    }

    static guid_t read_guid(const data& value) {
#if defined(ARK_USE_GUID_32)
        return value.get_int();
#else
        return value.get_int64();
#endif
    }

    void set_guid_storage(const guid_t& value) {
#if defined(ARK_USE_GUID_32)
        int64_value_ = 0;
        int_value_ = value;
#else
        int64_value_ = value;
#endif
    }

    allocator_type allocator_;
    data_type type_;

    // This union size is 8 bytes
    union
    {
        bool bool_value_;
        int int_value_;
        uint32_t uint32_value_;
        int64_t int64_value_;
        uint64_t uint64_value_;
        float float_value_;
        double double_value_;
        char* string_value_;
        void* pointer_value_;
        char* user_data_;
    };

    // buffer
    union
    {
        uint32_t allocated_length_{0};
        char inline_buffer_[buffer_size];
    };
};

// special
using data_impl = data_base<4, core_allocator>;

} // namespace ark
