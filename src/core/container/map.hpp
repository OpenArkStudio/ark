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

#include "ark/macros.hpp"

namespace ark {

template<typename key_type, typename mapped_type, bool use_hash_map = false>
class object_map_base
{
public:
    using k_type = key_type;
    using v_type = mapped_type;
    using map_type = std::conditional_t<use_hash_map, std::unordered_map<key_type, v_type>, std::map<key_type, v_type>>;
    using value_type = typename map_type::value_type;
    using reference = value_type&;
    using const_reference = const value_type&;
    using iterator = typename map_type::iterator;
    using const_iterator = typename map_type::const_iterator;

private:
    map_type nodes_;

public:
    object_map_base() = default;

    object_map_base(const object_map_base&)
        requires(!std::is_pointer_v<v_type>)
    = default;
    object_map_base(const object_map_base&)
        requires(std::is_pointer_v<v_type>)
    = delete;
    object_map_base& operator=(const object_map_base&)
        requires(!std::is_pointer_v<v_type>)
    = default;
    object_map_base& operator=(const object_map_base&)
        requires(std::is_pointer_v<v_type>)
    = delete;

    object_map_base(object_map_base&& other) noexcept(std::is_nothrow_move_constructible_v<map_type>)
        : nodes_(std::move(other.nodes_)) {
        other.nodes_.clear();
    }

    object_map_base& operator=(object_map_base&& other) noexcept(std::is_nothrow_move_assignable_v<map_type>) {
        if (this != &other)
        {
            clear();
            nodes_ = std::move(other.nodes_);
            other.nodes_.clear();
        }
        return *this;
    }

    ~object_map_base() {
        clear();
    }

    std::size_t size() const {
        return nodes_.size();
    }

    bool empty() const {
        return nodes_.empty();
    }

    std::pair<iterator, bool> insert(const k_type& key, const v_type& value) {
        return nodes_.insert(value_type(key, value));
    }

    // will delete allocated memory
    iterator erase(iterator it) {
        if (it != nodes_.end())
        {
            delete_value(it->second);
            return nodes_.erase(it);
        }

        return it;
    }

    // will delete allocated memory
    bool erase(const k_type& key) {
        auto iter = nodes_.find(key);
        if (iter == nodes_.end())
        {
            return false;
        }

        delete_value(iter->second);

        erase(iter);
        return true;
    }

    iterator find(const k_type& key) {
        return nodes_.find(key);
    }

    iterator begin() noexcept {
        return nodes_.begin();
    }

    iterator end() noexcept {
        return nodes_.end();
    }

    const_iterator begin() const noexcept {
        return nodes_.cbegin();
    }

    const_iterator end() const noexcept {
        return nodes_.cend();
    }

    const_iterator cbegin() const noexcept {
        return nodes_.cbegin();
    }

    const_iterator cend() const noexcept {
        return nodes_.cend();
    }

    // will delete allocated memory
    void clear() {
        for (auto iter : nodes_)
        {
            delete_value(iter.second);
        }

        nodes_.clear();
    }

    v_type find_value(const k_type& key) {
        auto iter = find(key);
        if (iter != end())
        {
            return iter->second;
        }

        return nullptr;
    }

    const_iterator find(const k_type& key) const {
        return nodes_.find(key);
    }

    v_type find_value(const k_type& key) const {
        auto iter = find(key);
        if (iter != end())
        {
            return iter->second;
        }

        return nullptr;
    }

private:
    template<typename stored_type>
    void delete_value(stored_type& value) {
        if constexpr (std::is_pointer_v<stored_type>)
        {
            delete value;
            value = nullptr;
        }
    }
};

template<typename key_type, typename mapped_type>
using object_map = object_map_base<key_type, mapped_type*>;

template<typename key_type, typename mapped_type>
using shared_object_map = object_map_base<key_type, std::shared_ptr<mapped_type>>;

template<typename key_type, typename mapped_type>
using object_hashmap = object_map_base<key_type, mapped_type*, true>;

template<typename key_type, typename mapped_type>
using shared_object_hashmap = object_map_base<key_type, std::shared_ptr<mapped_type>, true>;

} // namespace ark
