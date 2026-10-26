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

#include <new>

#include "ark/core_def.hpp"
#include "ark/macros.hpp"

namespace ark {

template<typename element_type>
class string_traits
{
};

template<>
class string_traits<char>
{
public:
    static size_t hash(const char* value) {
        return get_hash_value(value);
    }

    static size_t length(const char* value) {
        return strlen(value);
    }

    static bool equal(const char* lvalue, const char* rvalue) {
        return (0 == strcmp(lvalue, rvalue));
    }

    static void copy(char* dst, const char* src, size_t len) {
        memcpy(dst, src, len);
    }
};

template<typename element_type>
class string_traits_no_case : public string_traits<element_type>
{
};

template<>
class string_traits_no_case<char> : public string_traits<char>
{
public:
    static size_t hash(const char* value) {
        return get_hash_value_no_case(value);
    }

    static bool equal(const char* lvalue, const char* rvalue) {
        return (0 == ARK_STRICMP(lvalue, rvalue));
    }
};

//////////////////////////////////////////////////////////////////////////
class pod_string_allocator
{
public:
    pod_string_allocator() = default;
    ~pod_string_allocator() = default;

    void* alloc(size_t size) {
        void* result = allocate_bytes(size);
        if (result == nullptr)
        {
            throw std::bad_alloc{};
        }
        return result;
    }

    void free(void* ptr, size_t size) {
        ARK_DELETE_ARRAY(char, ptr);
    }

    void swap(pod_string_allocator& src) {
        // Do nothing
    }

private:
    static void* allocate_bytes(size_t size) {
        ARK_NEW_ARRAY_RET(char, size);
    }
};

template<typename element_type, typename mapped_type>
class pod_string_node
{
public:
    pod_string_node* next;
    size_t hash;
    mapped_type data;
    element_type name[1]; // like TYPE* name /  char* name
};

//////////////////////////////////////////////////////////////////////////

// predeclared
template<typename element_type, typename mapped_type, typename traits_type = string_traits<element_type>,
    typename allocator_type = pod_string_allocator>
class pod_string;

template<typename element_type, typename mapped_type, typename traits_type = string_traits<element_type>,
    typename allocator_type = pod_string_allocator>
class pod_string_iterator
{
private:
    using hash_t = pod_string<element_type, mapped_type, traits_type, allocator_type>;
    using node_t = pod_string_node<element_type, mapped_type>;

public:
    pod_string_iterator() = delete;

    pod_string_iterator(const hash_t* self, node_t* node)
        : self_(self)
        , node_(node) {
    }

    pod_string_iterator& operator++() {
        node_t* next = node_->next;

        if (next != nullptr)
        {
            node_ = next;
        }
        else
        {
            node_ = self_->to_next(node_);
        }

        return *this;
    }

    pod_string_iterator operator++(int) {
        pod_string_iterator tmp(*this);
        ++(*this);
        return tmp;
    }

    bool operator==(const pod_string_iterator& other) const {
        return (node_ == other.node_);
    }

    bool operator!=(const pod_string_iterator& other) const {
        return (node_ != other.node_);
    }

    const element_type* get_key() const {
        return node_->name;
    }

    mapped_type* get_data() const {
        return &node_->data;
    }

    node_t* get_node() const {
        return node_;
    }

private:
    const hash_t* self_;
    node_t* node_;
};

template<typename element_type, typename mapped_type, typename traits_type, typename allocator_type>
class pod_string
{
private:
    using hash_t = pod_string<element_type, mapped_type, traits_type, allocator_type>;
    using node_t = pod_string_node<element_type, mapped_type>;

    using iterator = pod_string_iterator<element_type, mapped_type, traits_type, allocator_type>;
    using const_iterator = pod_string_iterator<element_type, mapped_type, traits_type, allocator_type>;

public:
    explicit pod_string(size_t size = 0)
        : size_(size) {
        count_ = 0;

        if (size > 0)
        {
            buckets_ = (node_t**)allocator_.alloc(sizeof(node_t*) * size);
            memset(buckets_, 0, sizeof(node_t*) * size);
        }
        else
        {
            buckets_ = NULL;
        }
    }

    pod_string(const hash_t& src) {
        const size_t size = src.size_;
        count_ = 0;
        size_ = size;

        if (size > 0)
        {
            buckets_ = (node_t**)allocator_.alloc(sizeof(node_t*) * size);
            memset(buckets_, 0, sizeof(node_t*) * size);

            for (size_t i = 0; i < size; ++i)
            {
                node_t* p = src.buckets_[i];

                while (p)
                {
                    if (!add(p->name, p->data))
                    {
                        ARK_ASSERT(0, "Add fail ", __FILE__, __FUNCTION__);
                    }

                    p = p->next;
                }
            }
        }
        else
        {
            buckets_ = NULL;
        }
    }

    ~pod_string() {
        clear();

        if (buckets_)
        {
            allocator_.free(buckets_, sizeof(node_t*) * size_);
        }
    }

    void swap(hash_t& src) {
        node_t** tmp_buckets = src.buckets_;
        size_t tmp_size = src.size_;
        size_t tmp_count = src.count_;

        src.buckets_ = this->buckets_;
        src.size_ = this->size_;
        src.count_ = this->count_;
        this->buckets_ = tmp_buckets;
        this->size_ = tmp_size;
        this->count_ = tmp_count;
        allocator_.swap(src.allocator_);
    }

    void clear() {
        for (size_t i = 0; i < size_; ++i)
        {
            node_t* p = buckets_[i];

            while (p)
            {
                node_t* next = p->next;
                delete_node(p);
                p = next;
            }

            buckets_[i] = NULL;
        }
    }

    size_t get_count() const {
        return count_;
    }

    bool set(const element_type* name, const mapped_type& data) {
        node_t* node = find_node(name);

        if (node == nullptr)
        {
            return add(name, data);
        }

        node->data = data;
        return true;
    }

    bool add(const element_type* name, const mapped_type& data) {
        assert(name != nullptr);

        if (count_ == size_)
        {
            expand();
        }

        size_t hash = traits_type::hash(name);
        size_t bucket = get_bucket(hash);
        node_t* p = new_node(name);

        p->next = buckets_[bucket];
        p->hash = hash;
        p->data = data;

        buckets_[bucket] = p;
        ++count_;
        return true;
    }

    bool add(const element_type* name, const mapped_type& data, element_type*& p_name) {
        assert(name != nullptr);

        if (count_ == size_)
        {
            expand();
        }

        size_t hash = traits_type::hash(name);
        size_t bucket = get_bucket(hash);
        node_t* p = new_node(name);

        p->next = buckets_[bucket];
        p->hash = hash;
        p->data = data;

        buckets_[bucket] = p;
        ++count_;

        p_name = p->name;
        return true;
    }

    bool remove(const element_type* name) {
        assert(name != nullptr);

        if (size_ == 0)
        {
            return false;
        }

        size_t hash = traits_type::hash(name);
        size_t bucket = get_bucket(hash);
        node_t* p = buckets_[bucket];

        while (p)
        {
            if ((p->hash == hash) && traits_type::equal(p->name, name))
            {
                erase_node(bucket, p);
                delete_node(p);
                --count_;
                return true;
            }

            p = p->next;
        }

        return false;
    }

    bool remove_data(const element_type* name, const mapped_type& data) {
        assert(name != nullptr);

        if (size_ == 0)
        {
            return false;
        }

        size_t hash = traits_type::hash(name);
        size_t bucket = get_bucket(hash);
        node_t* p = buckets_[bucket];

        while (p)
        {
            if ((p->hash == hash) && traits_type::equal(p->name, name) && (p->data == data))
            {
                erase_node(bucket, p);
                delete_node(p);
                --count_;
                return true;
            }

            p = p->next;
        }

        return false;
    }

    bool exists(const element_type* name) const {
        return (find_node(name) != nullptr);
    }

    iterator find(const element_type* name) {
        return iterator(this, find_node(name));
    }

    const_iterator find(const element_type* name) const {
        return const_iterator(this, find_node(name));
    }

    bool get_data(const element_type* name, mapped_type& data) const {
        node_t* p = find_node(name);

        if (p == nullptr)
        {
            return false;
        }

        data = p->data;
        return true;
    }

    iterator begin() {
        for (size_t i = 0; i < size_; ++i)
        {
            if (buckets_[i])
            {
                return iterator(this, buckets_[i]);
            }
        }

        return end();
    }

    iterator end() {
        return iterator(this, NULL);
    }

    const_iterator begin() const {
        for (size_t i = 0; i < size_; ++i)
        {
            if (buckets_[i])
            {
                return const_iterator(this, buckets_[i]);
            }
        }

        return end();
    }

    const_iterator end() const {
        return const_iterator(this, NULL);
    }

    iterator erase(iterator iter) {
        iterator tmp(iter);
        ++tmp;
        node_t* p = iter.get_node();
        erase_node(get_bucket(p->hash), p);
        delete_node(p);
        --count_;

        return tmp;
    }

    size_t get_mem_usage() const {
        size_t size = sizeof(hash_t);

        for (size_t i = 0; i < size_; ++i)
        {
            node_t* p = buckets_[i];

            while (p)
            {
                size += sizeof(node_t) + traits_type::length(p->name) * sizeof(element_type);
                p = p->next;
            }
        }

        size += sizeof(node_t) * size_;
        return size;
    }

protected:
    size_t get_bucket(size_t hash) const {
        return (hash % size_);
    }

    node_t* new_node(const element_type* name) {
        const size_t length = traits_type::length(name);
        const size_t size = sizeof(node_t) + length * sizeof(element_type);
        node_t* p = (node_t*)allocator_.alloc(size);
        traits_type::copy(p->name, name, length + 1);
        return p;
    }

    void delete_node(node_t* p) {
        allocator_.free(p, sizeof(node_t) + traits_type::length(p->name) * sizeof(element_type));
    }

    void erase_node(size_t bucket, node_t* p) {
        assert(bucket < size_);
        assert(p != nullptr);

        node_t* node = buckets_[bucket];

        if (node == p)
        {
            buckets_[bucket] = p->next;
            return;
        }

        while (node)
        {
            if (node->next == p)
            {
                node->next = p->next;
                return;
            }

            node = node->next;
        }
    }

    node_t* find_node(const element_type* name) const {
        assert(name != nullptr);

        if (0 == size_)
        {
            return nullptr;
        }

        size_t hash = traits_type::hash(name);
        size_t bucket = get_bucket(hash);
        node_t* node = buckets_[bucket];

        while (node)
        {
            if ((node->hash == hash) && traits_type::equal(node->name, name))
            {
                return node;
            }

            node = node->next;
        }

        return nullptr;
    }

    void expand() {
        size_t new_size = size_ * 2 + 1; // hash bucket
        node_t** new_buckets = (node_t**)allocator_.alloc(sizeof(node_t*) * new_size);
        memset(new_buckets, 0, sizeof(node_t*) * new_size);

        for (size_t i = 0; i < size_; ++i)
        {
            if (nullptr != buckets_)
            {
                node_t* p = buckets_[i];

                while (p)
                {
                    node_t* next = p->next;
                    size_t bucket = size_t(p->hash) % new_size;

                    p->next = new_buckets[bucket];
                    new_buckets[bucket] = p;
                    p = next;
                }
            }
        }

        if (buckets_)
        {
            allocator_.free(buckets_, sizeof(node_t*) * size_);
        }

        buckets_ = new_buckets;
        size_ = new_size;
    }

    node_t* to_next(node_t* node) const {
        assert(node != nullptr);

        for (size_t i = get_bucket(node->hash) + 1; i < size_; ++i)
        {
            if (buckets_[i])
            {
                return buckets_[i];
            }
        }

        return nullptr;
    }

private:
    friend class pod_string_iterator<element_type, mapped_type, traits_type, allocator_type>;
    allocator_type allocator_;
    node_t** buckets_;
    size_t size_;
    size_t count_;
};

} // namespace ark
