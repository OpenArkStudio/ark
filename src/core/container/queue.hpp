/*
 * This source file is part of ArkNX
 * For the latest info, see https://github.com/ArkNX
 *
 * Copyright (c) 2013-2020 QuadHex ArkNX.
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

#include <atomic>
#include <list>
#include <mutex>

#include "ark/noncopyable.hpp"

namespace ark {

class queue_lock : public noncopyable {
public:
    explicit queue_lock() {
        flag_.clear();
    }

    ~queue_lock() {
    }

    void lock() {
        while (flag_.test_and_set(std::memory_order_acquire)) {
        }
    }

    void unlock() {
        flag_.clear(std::memory_order_release);
    }

private:
    mutable std::atomic_flag flag_;
};

using lock = queue_lock;

template<typename element_type>
class queue : public lock {
public:
    queue() {
    }

    virtual ~queue() {
    }

    bool push(const element_type& object) {
        std::lock_guard<queue_lock> guard(*this);
        list_.push_back(object);
        return true;
    }

    bool pop(element_type& object) {
        std::lock_guard<queue_lock> guard(*this);
        if (list_.empty()) {
            return false;
        }

        object = list_.front();
        list_.pop_front();
        return true;
    }

    int count() {
        std::lock_guard<queue_lock> guard(*this);
        return list_.size();
    }

private:
    std::list<element_type> list_;
};

} // namespace ark
