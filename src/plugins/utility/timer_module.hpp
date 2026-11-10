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

#include <functional>

#include "ark/define.hpp"
#include "ark/module.hpp"

namespace ark {

class timer_module : public module {
public:
    template<typename receiver_type>
    uint64_t add_single_timer(const guid_t& entity_id, const std::chrono::milliseconds interval, const uint32_t count,
        receiver_type* receiver, void (receiver_type::*handler)(uint64_t, const guid_t&)) {
        timer_functor functor = std::bind_front(handler, receiver);
        return add_single_timer(entity_id, interval, count, std::move(functor));
    }

    template<typename receiver_type>
    uint64_t add_forever_timer(const guid_t& entity_id, const std::chrono::milliseconds interval,
        receiver_type* receiver, void (receiver_type::*handler)(uint64_t, const guid_t&)) {
        timer_functor functor = std::bind_front(handler, receiver);
        return add_forever_timer(entity_id, interval, std::move(functor));
    }

    virtual bool remove_timer(const uint64_t timer_id) = 0;
    //virtual bool RemoveTimer(const std::string& name, const guid_t& entity_id) = 0;

protected:
    virtual uint64_t add_single_timer(const guid_t& entity_id, const std::chrono::milliseconds interval,
        const uint32_t count, timer_functor&& cb) = 0;
    virtual uint64_t add_forever_timer(
        const guid_t& entity_id, const std::chrono::milliseconds interval, timer_functor&& cb) = 0;
};

} // namespace ark
