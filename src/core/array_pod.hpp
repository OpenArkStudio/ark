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
#include <limits>
#include <new>
#include <stdexcept>

#include "ark/macros.hpp"
#include "ark/platform.hpp"

namespace ark {

class pod_array_allocator
{
public:
    pod_array_allocator() = default;
    ~pod_array_allocator() = default;

    void* alloc(size_t size) {
        ARK_NEW_ARRAY_RET(char, size);
    }

    void free(void* ptr, size_t size) {
        ARK_DELETE_ARRAY(char, ptr);
    }

    void swap(pod_array_allocator& src) {
        // do nothing
    }
};

// predeclared
template<typename element_type, size_t inline_capacity, typename allocator_type = pod_array_allocator>
class pod_array;

template<typename element_type, size_t inline_capacity, typename allocator_type>
class pod_array
{
public:
    using self_t = pod_array<element_type, inline_capacity, allocator_type>;

    pod_array() noexcept {
        data_ = inline_buffer_;
        capacity_ = inline_capacity;
        size_ = 0;
    }

    pod_array(const self_t& src) {
        size_ = src.size_;

        if (size_ <= inline_capacity)
        {
            data_ = inline_buffer_;
            capacity_ = inline_capacity;
        }
        else
        {
            capacity_ = src.capacity_;
            data_ = allocate(capacity_);
        }

        memcpy(data_, src.data_, size_ * sizeof(element_type));
    }

    ~pod_array() {
        if (capacity_ > inline_capacity)
        {
            allocator_.free(data_, capacity_ * sizeof(element_type));
        }
    }

    self_t& operator=(const self_t& rhs) {
        self_t tmp(rhs);
        swap(tmp);
        return *this;
    }

    void swap(self_t& src) noexcept {
        size_t tmp_size = src.size_;
        size_t tmp_capacity = src.capacity_;
        element_type* tmp_data = src.data_;
        element_type tmp_stack[inline_capacity];

        if (tmp_capacity <= inline_capacity)
        {
            memcpy(tmp_stack, src.inline_buffer_, tmp_size * sizeof(element_type));
        }

        src.size_ = this->size_;
        src.capacity_ = this->capacity_;

        if (this->capacity_ <= inline_capacity)
        {
            memcpy(src.inline_buffer_, this->inline_buffer_, size_ * sizeof(element_type));
            src.data_ = src.inline_buffer_;
        }
        else
        {
            src.data_ = this->data_;
        }

        //////////////////////////////////////////////////////////////////////////
        this->size_ = tmp_size;
        this->capacity_ = tmp_capacity;

        if (tmp_capacity <= inline_capacity)
        {
            memcpy(this->inline_buffer_, tmp_stack, tmp_size * sizeof(element_type));
            this->data_ = this->inline_buffer_;
        }
        else
        {
            this->data_ = tmp_data;
        }

        this->allocator_.swap(src.allocator_);
    }

    bool empty() {
        return (size_ == 0);
    }

    size_t size() const {
        return size_;
    }

    const element_type* data() const {
        return data_;
    }

    void push_back(const element_type& data) {
        const size_t new_size = incremented_size();
        const element_type value = data;
        if (new_size > capacity_)
        {
            reserve(grown_capacity(new_size));
        }

        data_[size_] = value;
        size_ = new_size;
    }

    void pop_back() {
        assert(size_ > 0);
        --size_;
    }

    element_type& back() {
        assert(size_ > 0);
        return data_[size_ - 1];
    }

    const element_type& back() const {
        assert(size_ > 0);
        return data_[size_ - 1];
    }

    element_type& operator[](size_t index) {
        assert(index < size_);
        return data_[index];
    }

    const element_type& operator[](size_t index) const {
        assert(index < size_);
        return data_[index];
    }

    void reserve(size_t size) {
        if (size > capacity_)
        {
            element_type* p = allocate(size);
            memcpy(p, data_, size_ * sizeof(element_type));

            if (capacity_ > inline_capacity)
            {
                allocator_.free(data_, capacity_ * sizeof(element_type));
            }

            data_ = p;
            capacity_ = size;
        }
    }

    void resize(size_t size) {
        if (size > capacity_)
        {
            reserve(grown_capacity(size));
        }

        size_ = size;
    }

    void resize(size_t size, const element_type& value) {
        if (size <= size_)
        {
            size_ = size;
            return;
        }
        const element_type saved_value = value;
        const size_t old_size = size_;
        resize(size);
        for (size_t i = old_size; i < size; ++i)
        {
            data_[i] = saved_value;
        }
    }

    void insert(size_t index, const element_type& data) {
        assert(index <= size_);

        const size_t new_size = incremented_size();
        const element_type value = data;
        resize(new_size);
        element_type* p = data_ + index;
        memmove(p + 1, p, (size_ - index - 1) * sizeof(element_type));
        *p = value;
    }

    void remove(size_t index) {
        assert(index <= size_);

        element_type* p = data_ + index;
        memmove(p, p + 1, (size_ - index - 1) * sizeof(element_type));
        --size_;
    }

    void remove_some(size_t start, size_t count) {
        assert((start <= size_) && ((start + count) <= size_));
        element_type* p = data_ + start;
        memmove(p, p + count, (size_ - start - count) * sizeof(element_type));
        size_ -= count;
    }

    void clear() {
        size_ = 0;
    }

    size_t get_mem_usage() const {
        size_t size = sizeof(self_t);

        if (capacity_ > size)
        {
            size += capacity_ * sizeof(element_type);
        }

        return size;
    }

private:
    static constexpr size_t max_capacity() {
        return std::numeric_limits<size_t>::max() / sizeof(element_type);
    }

    static void check_capacity(size_t size) {
        if (size > max_capacity())
        {
            throw std::length_error("pod_array capacity exceeds addressable storage");
        }
    }

    size_t incremented_size() const {
        if (size_ == max_capacity())
        {
            throw std::length_error("pod_array size exceeds addressable storage");
        }
        return size_ + 1;
    }

    size_t grown_capacity(size_t size) const {
        check_capacity(size);
        const size_t doubled = capacity_ > max_capacity() / 2 ? max_capacity() : capacity_ * 2;
        return std::max(size, doubled);
    }

    element_type* allocate(size_t size) {
        check_capacity(size);
        auto* result = static_cast<element_type*>(allocator_.alloc(size * sizeof(element_type)));
        if (result == nullptr)
        {
            throw std::bad_alloc{};
        }
        return result;
    }

    allocator_type allocator_;
    element_type inline_buffer_[inline_capacity];
    element_type* data_;
    size_t capacity_;
    size_t size_;
};

} // namespace ark
