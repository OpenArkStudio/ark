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

class spin_lock {
public:
    spin_lock()
        : atomic_lock_(false) {
    }

    spin_lock(spin_lock const&) = default;
    spin_lock(spin_lock&&) = default;
    spin_lock& operator=(spin_lock const&) = default;
    spin_lock& operator=(spin_lock&&) = default;

    ~spin_lock(void) = default;

    inline void lock() {
        while (atomic_lock_.exchange(true))
            ;
    }

    inline void unlock() {
        atomic_lock_ = false;
    }

    inline bool is_locked() const {
        return atomic_lock_;
    }

private:
    std::atomic_bool atomic_lock_;
};

} // namespace ark