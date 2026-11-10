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

#include "ark/core_def.hpp"

namespace ark {

template<typename char_type>
class char_traits {};

template<>
class char_traits<char> {
public:
    static size_t length(const char* value) {
        return strlen(value);
    }

    static int compare(const char* lvalue, const char* rvalue) {
        return strcmp(lvalue, rvalue);
    }

    static void copy(char* dst, const char* src, size_t len) {
        memcpy(dst, src, len);
    }

    static void put(char* dst, char value) {
        *dst = value;
    }

    static const char* empty_value() {
        return "";
    }

    static size_t find(const char* dst, const char* find, size_t begin = 0) {
        const char* pos = strstr(&dst[begin], find);

        if (pos == nullptr) {
            return -1;
        }

        return size_t(pos - dst);
    }

    static size_t n_pos() {
        return -1;
    }
};

//////////////////////////////////////////////////////////////////////////

class string_allocator {
public:
    string_allocator() {
    }

    ~string_allocator() {
    }

    void* alloc(size_t size) {
        return new char[size];
    }

    void free(void* ptr, size_t size) {
        delete[] (char*)ptr;
    }

    void swap(string_allocator& src) {
        // Do nothing
    }
};

template<typename char_type, size_t inline_capacity, typename traits_type = char_traits<char_type>,
    typename allocator_type = string_allocator>
class small_string {
    using self_type = small_string<char_type, inline_capacity, traits_type, allocator_type>;

public:
    small_string() {
        inline_buffer_[0] = 0;
        data_ = inline_buffer_;
        capacity_ = inline_capacity;
        size_ = 0;
    }

    ~small_string() {
        if (capacity_ > inline_capacity) {
            allocator_.free(data_, capacity_ * sizeof(char_type));
        }
    }

    small_string(const self_type& src) {
        init(src.c_str(), src.length());
    }

    explicit small_string(const char_type* src) {
        init(src, traits_type::length(src));
    }

    small_string(const char_type* src, size_t len) {
        init(src, len);
    }

    small_string(const char_type* value1, const char_type* value2) {
        init_with_two(value1, traits_type::length(value1), value2, traits_type::length(value2));
    }

    small_string(const char_type* value1, size_t len1, const char_type* value2, size_t len2) {
        init_with_two(value1, len1, value2, len2);
    }

    template<size_t new_size>
    explicit small_string(const small_string<char_type, new_size, traits_type, allocator_type>& src) {
        init(src.c_str(), src.length());
    }

    void swap(self_type& src) noexcept {
        size_t tmp_size = src.size_;
        size_t tmp_capacity = src.capacity_;
        char_type* tmp_data = src.data_;
        char_type tmp_stack[inline_capacity];

        if (tmp_capacity <= inline_capacity) {
            traits_type::copy(tmp_stack, src.inline_buffer_, tmp_size + 1);
        }

        src.size_ = this->size_;
        src.capacity_ = this->capacity_;

        if (this->capacity_ <= inline_capacity) {
            traits_type::copy(src.inline_buffer_, this->inline_buffer_, this->size_ + 1);
            src.data_ = src.inline_buffer_;
        } else {
            src.data_ = this->data_;
        }

        this->size_ = tmp_size;
        this->capacity_ = tmp_capacity;

        if (tmp_capacity <= inline_capacity) {
            traits_type::copy(this->inline_buffer_, tmp_stack, tmp_size + 1);
            this->data_ = this->inline_buffer_;
        } else {
            data_ = tmp_data;
        }

        allocator_.swap(src.allocator_);
    }

    self_type& operator=(const self_type& src) {
        if (this != &src) {
            inner_assign(src.c_str(), src.length());
        }

        return *this;
    }

    template<size_t new_size>
    self_type& operator=(const small_string<char_type, new_size, traits_type, allocator_type>& src) {
        return inner_assign(src.c_str(), src.length());
    }

    self_type& operator=(const char_type* src) {
        return inner_assign(src, traits_type::length(src));
    }

    self_type& operator=(char_type value) {
        return inner_assign(&value, 1);
    }

    self_type& operator+=(const self_type& src) {
        return inner_append(src.c_str(), src.length());
    }

    self_type& operator+=(const char_type* src) {
        return inner_append(src, traits_type::length(src));
    }

    self_type& operator+=(char_type value) {
        return inner_append(&value, 1);
    }

    size_t length() const {
        return size_;
    }

    size_t size() const {
        return size_;
    }

    bool empty() const {
        return (0 == size_);
    }

    size_t capacity() const {
        return capacity_;
    }

    const char_type* c_str() const {
        return data_;
    }

    void clear() {
        if (data_ != inline_buffer_) {
            allocator_.free(data_, capacity_ * sizeof(char_type));
        }

        inline_buffer_[0] = 0;
        data_ = inline_buffer_;
        capacity_ = inline_capacity;
        size_ = 0;
    }

    bool compare(const self_type& src) const {
        return (traits_type::compare(this->c_str(), src.c_str()) == 0);
    }

    bool compare(const char_type* src) const {
        return (traits_type::compare(this->c_str(), src) == 0);
    }

    self_type& append(const self_type& src) {
        return inner_append(src.c_str(), src.length());
    }

    self_type& append(const char_type* src, const size_t len) {
        return inner_append(src, len);
    }

    self_type& append(const char_type* src) {
        return inner_append(src, traits_type::length(src));
    }

    self_type& append(char_type value) {
        return inner_append(&value, 1);
    }

    size_t find(const char_type* src, const size_t begin = 0) {
        return traits_type::find(data_, src, begin);
    }

    size_t find_last_of(char_type value) {
        for (int i = int(size_ - 1); i >= 0; --i) {
            if (data_[i] == value) {
                return i;
            }
        }

        return npos();
    }

    size_t npos() {
        return traits_type::n_pos();
    }

    self_type substr(size_t begin, size_t len) {
        return self_type(&data_[begin], len);
    }

    size_t get_mem_usage() const {
        size_t size = sizeof(self_type);

        if (capacity_ > inline_capacity) {
            size += capacity_ * sizeof(char_type);
        }

        return size;
    }

private:
    void init(const char_type* src, size_t len) {
        size_ = len;

        if (size_ < inline_capacity) {
            capacity_ = inline_capacity;
            data_ = inline_buffer_;
        } else {
            capacity_ = (size_ + 1) * 2;
            data_ = (char_type*)allocator_.alloc(capacity_ * sizeof(char_type));
        }

        traits_type::copy(data_, src, size_);
        traits_type::put(data_ + size_, 0);
    }

    void init_with_two(const char_type* value1, size_t size1, const char_type* value2, size_t size2) {
        size_ = size1 + size2;

        if (size_ < inline_capacity) {
            capacity_ = inline_capacity;
            data_ = inline_buffer_;
        } else {
            capacity_ = (size_ + 1) * 2;
            data_ = (char_type*)allocator_.alloc(capacity_ * sizeof(char_type));
        }

        traits_type::copy(data_, value1, size1);
        traits_type::copy(data_ + size1, value2, size2);
        traits_type::put(data_ + size1 + size2, 0);
    }

    self_type& inner_assign(const char_type* src, size_t len) {
        if (len < capacity_) {
            traits_type::copy(data_, src, len);
            traits_type::put(data_ + len, 0);
            size_ = len;
        } else {
            self_type tmp(src, len);
            this->swap(tmp);
        }

        return (*this);
    }

    self_type& inner_append(const char_type* src, size_t len) {
        const size_t new_size = this->size_ + len;

        if (new_size < capacity_) {
            traits_type::copy(data_ + size_, src, len);
            traits_type::put(data_ + size_ + len, 0);
            size_ = new_size;
        } else {
            self_type tmp(this->c_str(), this->length(), src, len);
            this->swap(tmp);
        }

        return (*this);
    }

    allocator_type allocator_;
    char_type inline_buffer_[inline_capacity];
    char_type* data_;
    size_t capacity_;
    size_t size_;
};

typedef small_string<char, 16, char_traits<char>, core_allocator> data_node_name;
typedef small_string<char, 16, char_traits<char>, core_allocator> data_table_name;

template<typename element_type>
class hash {};

template<>
class hash<data_node_name> {
public:
    size_t operator()(data_node_name const& pro) const {
        const char* value = pro.c_str();
        return get_hash_value(value);
    }
};

template<typename char_type, size_t inline_capacity, typename traits_type, typename allocator_type>
inline bool operator==(const small_string<char_type, inline_capacity, traits_type, allocator_type>& s1,
    const small_string<char_type, inline_capacity, traits_type, allocator_type>& s2) {
    return s1.compare(s2);
}

template<typename char_type, size_t inline_capacity, typename traits_type, typename allocator_type>
inline bool operator==(
    const small_string<char_type, inline_capacity, traits_type, allocator_type>& s1, const char_type* s2) {
    return s1.compare(s2);
}

template<typename char_type, size_t inline_capacity, typename traits_type, typename allocator_type>
inline small_string<char_type, inline_capacity, traits_type, allocator_type> operator+(
    const small_string<char_type, inline_capacity, traits_type, allocator_type>& s1,
    const small_string<char_type, inline_capacity, traits_type, allocator_type>& s2) {
    return small_string<char_type, inline_capacity, traits_type, allocator_type>(s1).append(s2);
}

template<typename char_type, size_t inline_capacity, typename traits_type, typename allocator_type>
inline small_string<char_type, inline_capacity, traits_type, allocator_type> operator+(
    const small_string<char_type, inline_capacity, traits_type, allocator_type>& s1, const char_type* s2) {
    return small_string<char_type, inline_capacity, traits_type, allocator_type>(s1).append(s2);
}

template<typename char_type, size_t inline_capacity, typename traits_type, typename allocator_type>
inline small_string<char_type, inline_capacity, traits_type, allocator_type> operator+(
    const small_string<char_type, inline_capacity, traits_type, allocator_type>& s1, char_type ch) {
    return small_string<char_type, inline_capacity, traits_type, allocator_type>(s1).append(ch);
}

template<typename char_type, size_t inline_capacity, typename traits_type, typename allocator_type>
inline small_string<char_type, inline_capacity, traits_type, allocator_type> operator+(
    const char_type* s1, const small_string<char_type, inline_capacity, traits_type, allocator_type>& s2) {
    return small_string<char_type, inline_capacity, traits_type, allocator_type>(s1).append(s2);
}

} // namespace ark
