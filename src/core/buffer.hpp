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
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

#include "ark/macros.hpp"

namespace ark {

class buffer final
{
    struct data_deleter
    {
        void operator()(char* value) const noexcept {
            ARK_DELETE_ARRAY(char, value);
        }
    };

    using owned_data = std::unique_ptr<char[], data_deleter>;

    static char* allocate_data(size_t size) {
        return ARK_NEW_ARRAY(char, size);
    }

public:
    explicit buffer(size_t buffer_size = 1024 * 512) {
        data_.reset(allocate_data(buffer_size));
        if (data_ != nullptr)
        {
            data_size_ = buffer_size;
        }
    }

    ~buffer() = default;

    buffer(const buffer&) = delete;
    buffer& operator=(const buffer&) = delete;

    buffer(buffer&& other) noexcept
        : data_(std::move(other.data_))
        , data_size_(std::exchange(other.data_size_, 0))
        , write_pos_(std::exchange(other.write_pos_, 0))
        , read_pos_(std::exchange(other.read_pos_, 0)) {
    }

    buffer& operator=(buffer&& other) noexcept {
        if (this != &other)
        {
            data_ = std::move(other.data_);
            data_size_ = std::exchange(other.data_size_, 0);
            write_pos_ = std::exchange(other.write_pos_, 0);
            read_pos_ = std::exchange(other.read_pos_, 0);
        }
        return *this;
    }

    size_t get_capacity() const noexcept {
        return data_size_;
    }

    bool write(const char* data, size_t len) {
        if (len == 0)
        {
            return true;
        }
        if (data == nullptr)
        {
            return false;
        }
        if (get_write_valid_count() < len) [[unlikely]]
        {
            if (!prepare_write(len))
            {
                return false;
            }
        }
        std::memcpy(get_write_ptr(), data, len);
        add_write_pos(len);
        return true;
    }

    size_t get_length() {
        return write_pos_ - read_pos_;
    }

    char* get_data() {
        if (read_pos_ < data_size_)
        {
            return data_.get() + read_pos_;
        }
        else
        {
            return nullptr;
        }
    }

    void remove_data(size_t value) {
        if (value <= get_length())
        {
            read_pos_ += value;
        }
    }

private:
    bool prepare_write(size_t len) {
        const size_t left_len = data_size_ - get_length();
        if (left_len >= len)
        {
            adjust_to_head();
            return true;
        }
        return grow(len - left_len);
    }

    void adjust_to_head() {
        size_t len = 0;

        if (read_pos_ <= 0)
        {
            return;
        }

        len = get_length();

        if (len > 0)
        {
            std::memmove(data_.get(), data_.get() + read_pos_, len);
        }

        read_pos_ = 0;
        write_pos_ = len;
    }

    void init() {
        read_pos_ = 0;
        write_pos_ = 0;
    }

    size_t get_read_pos() {
        return read_pos_;
    }

    void add_write_pos(size_t value) {
        size_t temp = write_pos_ + value;

        if (temp <= data_size_)
        {
            write_pos_ = temp;
        }
    }

    size_t get_write_valid_count() {
        return data_size_ - write_pos_;
    }

    size_t get_size() {
        return data_size_;
    }

    char* get_write_ptr() {
        if (write_pos_ < data_size_)
        {
            return data_.get() + write_pos_;
        }
        else
        {
            return nullptr;
        }
    }

    bool grow(size_t len) {
        if (len > std::numeric_limits<size_t>::max() - data_size_)
        {
            throw std::length_error("Receive buffer size exceeds the addressable range");
        }
        const size_t required = data_size_ + len;
        const size_t doubled = data_size_ <= std::numeric_limits<size_t>::max() / 2 ? data_size_ * 2 : required;
        const size_t n = std::max(required, doubled);
        owned_data candidate(allocate_data(n));
        if (!candidate)
        {
            return false;
        }
        const size_t unread = get_length();
        if (unread > 0)
        {
            std::memcpy(candidate.get(), data_.get() + read_pos_, unread);
        }
        data_ = std::move(candidate);
        read_pos_ = 0;
        write_pos_ = unread;
        data_size_ = n;
        return true;
    }

    owned_data data_;
    size_t data_size_{0};

    size_t write_pos_{0};
    size_t read_pos_{0};
};

} // namespace ark
