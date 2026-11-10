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

#include "ark/platform.hpp"

namespace ark {

template<typename element_type>
class list {
public:
    using list_type = std::list<element_type>;
    using reference = element_type&;
    using const_reference = const element_type&;
    using iterator = typename list_type::iterator;
    using const_iterator = typename list_type::const_iterator;
    using reverse_iterator = typename list_type::reverse_iterator;
    using const_reverse_iterator = typename list_type::const_reverse_iterator;

    list() = default;
    ~list() = default;

    std::size_t size() const {
        return values_.size();
    }

    bool empty() const {
        return values_.empty();
    }

    iterator erase(const_iterator& it) {
        return values_.erase(it);
    }

    iterator emplace(iterator it, const_reference value) {
        return values_.emplace(it, value);
    }

    void emplace_front(const_reference value) {
        values_.emplace_front(value);
    }

    void emplace_back(const_reference value) {
        values_.emplace_back(value);
    }

    void pop_front() {
        values_.pop_front();
    }

    void pop_back() {
        values_.pop_back();
    }

    void push_front(const_reference value) {
        return values_.push_front(value);
    }

    void push_back(const_reference value) {
        return values_.push_back(value);
    }

    void clear() {
        values_.clear();
    }

    void remove(const_reference value) {
        values_.remove(value);
    }

    bool get_value(const std::size_t index, reference value) {
        if (index >= values_.size()) {
            return false;
        }

        auto iter = values_.begin();
        std::advance(iter, index);
        value = *iter;
        return true;
    }

    bool exist(const_reference value) {
        auto iter = std::find(values_.begin(), values_.end(), value);
        return (iter != values_.end());
    }

    iterator begin() {
        return values_.begin();
    }

    iterator end() {
        return values_.end();
    }

    reverse_iterator rbegin() {
        return values_.rbegin();
    }

    reverse_iterator rend() {
        return values_.rend();
    }

private:
    list_type values_;
};

} // namespace ark
