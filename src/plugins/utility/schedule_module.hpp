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

class schedule_module : public module {
public:
    template<typename receiver_type>
    void add_schedule(const std::string& cron_expression, receiver_type* receiver, void (receiver_type::*handler)()) {
        scheduler_functor functor = std::bind_front(handler, receiver);
        add_cron_schedule(cron_expression, std::move(functor));
    }

    // every day
    template<typename receiver_type>
    void add_daily_schedule(receiver_type* receiver, void (receiver_type::*handler)()) {
        scheduler_functor functor = std::bind_front(handler, receiver);
        add_cron_schedule("0 0 * * * ", std::move(functor));
    }

    // every Monday
    template<typename receiver_type>
    void add_weekly_schedule(receiver_type* receiver, void (receiver_type::*handler)()) {
        scheduler_functor functor = std::bind_front(handler, receiver);
        add_cron_schedule("0 0 * * 1", std::move(functor));
    }

    // every month
    template<typename receiver_type>
    void add_monthly_schedule(receiver_type* receiver, void (receiver_type::*handler)()) {
        scheduler_functor functor = std::bind_front(handler, receiver);
        add_cron_schedule("0 0 1 * * ", std::move(functor));
    }

    template<typename receiver_type>
    void add_interval_schedule(
        const std::chrono::system_clock::duration interval, receiver_type* receiver, void (receiver_type::*handler)()) {
        scheduler_functor functor = std::bind_front(handler, receiver);
        add_interval_schedule(interval, std::move(functor));
    }

    template<typename receiver_type>
    void add_at_schedule(const std::string& target_time, receiver_type* receiver, void (receiver_type::*handler)()) {
        scheduler_functor functor = std::bind_front(handler, receiver);
        add_at_schedule(target_time, std::move(functor));
    }

protected:
    virtual void add_interval_schedule(const std::chrono::system_clock::duration interval, scheduler_functor&& cb) = 0;
    virtual void add_at_schedule(const std::string& target_time, scheduler_functor&& cb) = 0;
    virtual void add_cron_schedule(const std::string& cron_expression, scheduler_functor&& cb) = 0;
};

} // namespace ark
