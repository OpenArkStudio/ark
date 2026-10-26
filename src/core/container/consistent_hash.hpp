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

#include "ark/dep.hpp"
#include "ark/hash/crc.hpp"
#include "ark/hash/murmur_hash.hpp"
#include "ark/macros.hpp"

namespace ark {

class v_node
{
public:
    v_node(int server_id, const std::string& ip, std::size_t port, std::size_t id)
        : bus_id(server_id)
        , vnode_ip(ip)
        , vnode_port(port)
        , vnode_id(id) {
    }

    v_node(int server_id, const char* ip, std::size_t port, std::size_t id)
        : bus_id(server_id)
        , vnode_ip(ip)
        , vnode_port(port)
        , vnode_id(id) {
    }

    std::string to_str() const {
        return ARK_FORMAT("{}-{}-{}-{}", bus_id, vnode_ip, vnode_port, vnode_id);
    }

    bus_id_t bus_id{0};        // server bus id
    std::string vnode_ip;      // server ip
    std::size_t vnode_port{0}; // server port
    std::size_t vnode_id;      // node id
};

template<typename element_type, typename hasher_type,
    typename allocator_param_type = std::allocator<std::pair<const typename hasher_type::result_type, element_type>>>
class consistent_hashmap
{
public:
    using size_type = typename hasher_type::result_type;
    using map_type = std::map<size_type, element_type, std::less<size_type>, allocator_param_type>;
    using value_type = typename map_type::value_type;
    using reference = value_type&;
    using const_reference = const value_type&;
    using iterator = typename map_type::iterator;
    using reverse_iterator = typename map_type::reverse_iterator;
    using allocator_type = allocator_param_type;

    consistent_hashmap() = default;
    ~consistent_hashmap() = default;

    void set_vnode_count(const std::size_t count) {
        vnode_count_ = count;
    }

    void insert(bus_id_t bus_id, const std::string& ip, uint16_t port) {
        v_node vnode(bus_id, ip, port, 0);
        for (std::size_t i = 0; i < vnode_count_; ++i)
        {
            vnode.vnode_id = i;
            insert(vnode);
        }
    }

    std::size_t size() const {
        return nodes_.size();
    }

    bool empty() const {
        return nodes_.empty();
    }

    iterator find(const std::string& key) {
        return find(hasher_.hash(key));
    }

    std::size_t erase(const element_type& node) {
        size_type hash = hasher_(node);
        return nodes_.erase(hash);
    }

    iterator begin() {
        return nodes_.begin();
    }

    iterator end() {
        return nodes_.end();
    }

    reverse_iterator rbegin() {
        return nodes_.rbegin();
    }

    reverse_iterator rend() {
        return nodes_.rend();
    }

protected:
    std::pair<iterator, bool> insert(const element_type& node) {
        size_type hash = hasher_(node);
        return nodes_.insert(value_type(hash, node));
    }

    iterator find(size_type hash) {
        if (nodes_.empty())
        {
            return nodes_.end();
        }

        iterator it = nodes_.lower_bound(hash);

        if (it == nodes_.end())
        {
            it = nodes_.begin();
        }

        return it;
    }

    void erase(iterator it) {
        nodes_.erase(it);
    }

private:
    hasher_type hasher_;
    map_type nodes_;
    std::size_t vnode_count_{100}; // default virtual node count is 100
};

class crc_hasher
{
public:
    using result_type = uint32_t;

    static result_type hash(const std::string& key) {
        result_type ret = 0;
        return crc::crc32(key);
    }

    result_type operator()(const v_node& node) {
        return hash(node.to_str());
    }
};

class murmur_hasher
{
public:
    using result_type = uint32_t;

    static result_type hash(const std::string& key) {
        result_type ret = 0;
        murmur_hash::murmur_hash2(key.c_str(), key.size(), &ret);
        return ret;
    }

    result_type operator()(const v_node& node) {
        return hash(node.to_str());
    }
};

using consistent_hashmap_type = consistent_hashmap<v_node, murmur_hasher>;

} // namespace ark
