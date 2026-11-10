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

#include <readerwriterqueue.h>

#include "ark/platform.hpp"

namespace ark {

template<typename element_type>
class lock_free_queue {
public:
    lock_free_queue() = default;
    ~lock_free_queue() = default;

    bool push(const element_type& object) {
        return queue_.enqueue(object);
    }

    bool pop(element_type& object) {
        return queue_.try_dequeue(object);
    }

    size_t count() {
        return queue_.size_approx();
    }

private:
    moodycamel::BlockingReaderWriterQueue<element_type> queue_;
};

} // namespace ark
