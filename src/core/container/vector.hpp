/*
 * This source file is part of ARK
 * For the latest info, see https://github.com/ArkNX
 *
 * Copyright (c) 2013-2020 ArkNX authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
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

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include "ark/macros.hpp"

namespace ark {

template<typename element_type, bool is_smart_ptr>
class vector_value_type
{
public:
    using value_type = element_type*;
};

template<typename element_type>
class vector_value_type<element_type, false>
{
public:
    using value_type = element_type*;
};

template<typename element_type>
class vector_value_type<element_type, true>
{
public:
    using value_type = std::shared_ptr<element_type>;
};

template<typename element_type, bool is_smart_ptr>
class object_vector_base
{
public:
    using value_type = typename vector_value_type<element_type, is_smart_ptr>::value_type;
    using vector_type = std::vector<value_type>;
    using reference = value_type&;
    using const_reference = const value_type&;
    using iterator = typename vector_type::iterator;
    using const_iterator = typename vector_type::const_iterator;
    using reverse_iterator = typename vector_type::reverse_iterator;
    using const_reverse_iterator = typename vector_type::const_reverse_iterator;

    object_vector_base() = default;

    object_vector_base(const object_vector_base&)
        requires(is_smart_ptr)
    = default;
    object_vector_base(const object_vector_base&)
        requires(!is_smart_ptr)
    = delete;
    object_vector_base& operator=(const object_vector_base&)
        requires(is_smart_ptr)
    = default;
    object_vector_base& operator=(const object_vector_base&)
        requires(!is_smart_ptr)
    = delete;

    object_vector_base(object_vector_base&& other) noexcept
        : datas_(std::move(other.datas_)) {
        other.datas_.clear();
    }

    object_vector_base& operator=(object_vector_base&& other) noexcept {
        if (this != &other)
        {
            clear();
            datas_ = std::move(other.datas_);
            other.datas_.clear();
        }
        return *this;
    }

    ~object_vector_base() {
        clear();
    }

    std::size_t size() const {
        return datas_.size();
    }

    bool empty() const {
        return datas_.empty();
    }

    void clear() {
        if constexpr (!is_smart_ptr)
        {
            for (auto data : datas_)
            {
                delete data;
            }
        }

        datas_.clear();
    }

    value_type at(std::size_t index) {
        if (index >= size())
        {
            return nullptr;
        }

        return datas_[index];
    }

    value_type operator[](std::size_t index) {
        ARK_ASSERT_RET_VAL(index < size(), nullptr);
        return datas_[index];
    }

    void resize(std::size_t new_size) {
        if (new_size <= size())
        {
            return;
        }

        datas_.resize(new_size, nullptr);
    }

    std::size_t first_empty() {
        std::size_t index = 0;
        auto size = this->size();
        for (; index < size; index++)
        {
            if (datas_[index] == nullptr)
            {
                return index;
            }
        }

        return index;
    }

    void erase(std::size_t index) {
        ARK_ASSERT_RET_NONE(index < size());
        if constexpr (!is_smart_ptr)
        {
            delete datas_[index];
        }
        datas_[index] = nullptr;
    }

    void push_back(value_type value) {
        std::size_t index = first_empty();
        push_back(index, std::move(value));
    }

    void push_back(std::size_t index, value_type value) {
        if (index < size())
        {
            if constexpr (!is_smart_ptr)
            {
                if (datas_[index] != value)
                {
                    delete datas_[index];
                    datas_[index] = nullptr;
                }
            }
            datas_[index] = std::move(value);
        }
        else
        {
            datas_.emplace_back(std::move(value));
        }
    }

    iterator begin() noexcept {
        return datas_.begin();
    }

    iterator end() noexcept {
        return datas_.end();
    }

    const_iterator cbegin() const noexcept {
        return datas_.cbegin();
    }

    const_iterator cend() const noexcept {
        return datas_.cend();
    }

    reverse_iterator rbegin() noexcept {
        return datas_.rbegin();
    }

    reverse_iterator rend() noexcept {
        return datas_.rend();
    }

    const_reverse_iterator crbegin() const noexcept {
        return datas_.crbegin();
    }

    const_reverse_iterator crend() const noexcept {
        return datas_.crend();
    }

private:
    vector_type datas_;
};

template<typename element_type>
using value_vector = std::vector<element_type>;

template<typename element_type>
using pointer_vector = object_vector_base<element_type, false>;

template<typename element_type>
using shared_vector = object_vector_base<element_type, true>;

} // namespace ark
