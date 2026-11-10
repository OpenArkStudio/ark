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

#include <shared_mutex>

#include "ark/noncopyable.hpp"
#include "ark/platform.hpp"

namespace ark {

class reader_writer_lock : public noncopyable {
    mutable std::shared_mutex mtx_;

public:
    void read_lock() {
        mtx_.lock_shared();
    }

    void read_unlock() {
        mtx_.unlock_shared();
    }

    void write_lock() {
        mtx_.lock();
    }

    void write_unlock() {
        mtx_.unlock();
    }
};

class scope_r_lock : public noncopyable {
public:
    explicit scope_r_lock(reader_writer_lock& lock)
        : rwlock_(lock) {
        rwlock_.read_lock();
    }

    ~scope_r_lock() {
        rwlock_.read_unlock();
    }

private:
    reader_writer_lock& rwlock_;
};

class scope_w_lock : public noncopyable {
public:
    explicit scope_w_lock(reader_writer_lock& lock)
        : rwlock_(lock) {
        rwlock_.write_lock();
    }

    ~scope_w_lock() {
        rwlock_.write_unlock();
    }

private:
    reader_writer_lock& rwlock_;
};

} // namespace ark
