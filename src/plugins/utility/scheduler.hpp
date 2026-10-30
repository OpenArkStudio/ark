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

#include <cstdio>
#include <iomanip>
#include <map>

#include "ark/utility/ctpl_stl.h"

#include "ark/cron.hpp"
#include "ark/platform.hpp"

namespace ark {

// this file is based on https://github.com/Bosma/Scheduler.
// under the MIT license.
class interruptable_sleep final
{
    using clock = std::chrono::system_clock;

    // InterruptableSleep offers a sleep that can be interrupted by any thread.
    // It can be interrupted multiple times
    // and be interrupted before any sleep is called (the sleep will immediately complete)
    // Has same interface as condition_variables and futures, except with sleep instead of wait.
    // For a given object, sleep can be called on multiple threads safely, but is not recommended as behaviour is undefined.

public:
    interruptable_sleep()
        : interrupted_(false) {
    }

    interruptable_sleep(const interruptable_sleep&) = delete;
    interruptable_sleep(interruptable_sleep&&) noexcept = delete;

    ~interruptable_sleep() noexcept = default;

    interruptable_sleep& operator=(const interruptable_sleep&) noexcept = delete;
    interruptable_sleep& operator=(interruptable_sleep&&) noexcept = delete;

    void sleep_for(clock::duration duration) {
        std::unique_lock<std::mutex> ul(mutex_);
        condition_.wait_for(ul, duration, [this] { return interrupted_; });
        interrupted_ = false;
    }

    void sleep_until(clock::time_point time) {
        std::unique_lock<std::mutex> ul(mutex_);
        condition_.wait_until(ul, time, [this] { return interrupted_; });
        interrupted_ = false;
    }

    void sleep() {
        std::unique_lock<std::mutex> ul(mutex_);
        condition_.wait(ul, [this] { return interrupted_; });
        interrupted_ = false;
    }

    void interrupt() {
        std::lock_guard<std::mutex> lg(mutex_);
        interrupted_ = true;
        condition_.notify_one();
    }

private:
    bool interrupted_;
    std::mutex mutex_;
    std::condition_variable condition_;
};

class scheduler_task
{
public:
    using clock = std::chrono::system_clock;

    explicit scheduler_task(std::function<void()>&& f, bool recur = false, bool interval = false)
        : function(std::move(f))
        , recurring(recur)
        , interval(interval) {
    }

    virtual clock::time_point get_new_time() const = 0;

    std::function<void()> function;

    bool recurring;
    bool interval;
};

class in_task final : public scheduler_task
{
public:
    using clock = std::chrono::system_clock;

    explicit in_task(std::function<void()>&& f)
        : scheduler_task(std::move(f)) {
    }

    // dummy time_point because it's not used
    clock::time_point get_new_time() const override {
        return clock::time_point(clock::duration(0));
    }
};

class every_task final : public scheduler_task
{
public:
    using clock = std::chrono::system_clock;

    every_task(clock::duration time, std::function<void()>&& f, bool interval = false)
        : scheduler_task(std::move(f), true, interval)
        , interval_duration(time) {
    }

    clock::time_point get_new_time() const override {
        return clock::now() + interval_duration;
    };

    clock::duration interval_duration;
};

class cron_task final : public scheduler_task
{
public:
    cron_task(const std::string& expression, std::function<void()>&& f)
        : scheduler_task(std::move(f), true)
        , cron_expression(expression) {
    }

    clock::time_point get_new_time() const override {
        return cron_expression.cron_to_next();
    };

    ark::cron cron_expression;
};

class scheduler final
{
    friend struct scheduler_test_access;
    friend class schedule_module_impl;

public:
    using clock = std::chrono::system_clock;

    explicit scheduler(unsigned int max_n_tasks = 4)
        : stopping_(false)
        , threads_(max_n_tasks + 1) {
        threads_.push([this](int) { dispatch(); });
    }

    scheduler(const scheduler&) = delete;
    scheduler(scheduler&&) noexcept = delete;
    scheduler& operator=(const scheduler&) = delete;
    scheduler& operator=(scheduler&&) noexcept = delete;

    ~scheduler() {
        stop_accepting();
        // Owner-only fallback: joins accepted work without a deadline. A callback
        // must never destroy its own scheduler or wait for this owner to join it.
        // Keep idle workers alive while accepted future one-shots can still be
        // submitted by the dispatcher or by accepted callback continuations.
        while (!dispatcher_retired_.load())
        {
            dispatcher_retired_.wait(false);
        }
        threads_.stop(true);
    }

    void stop_accepting() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        sleeper_.interrupt();
    }

    bool ready_to_shut() const {
        // No business owners or further dispatch remain. pre_shut() still joins
        // the framework's final notification/return path before module teardown.
        return dispatcher_retired_.load() && outstanding_.load() == 0;
    }

    // Owner-only; never join workers while they can still need owner progress.
    bool pre_shut() {
        if (!ready_to_shut())
        {
            return false;
        }
        threads_.stop(true);
        return true;
    }

    template<typename callable_type, typename... args_type>
    void in(const clock::time_point time, callable_type&& f, args_type&&... args) {
        operation registration(*this);
        if (!registration.accepted)
        {
            return;
        }

        std::shared_ptr<scheduler_task> t =
            std::make_shared<in_task>(std::bind(std::forward<callable_type>(f), std::forward<args_type>(args)...));
        add_task(time, std::move(t));
    }

    template<typename callable_type, typename... args_type>
    void in(const clock::duration time, callable_type&& f, args_type&&... args) {
        in(clock::now() + time, std::forward<callable_type>(f), std::forward<args_type>(args)...);
    }

    template<typename callable_type, typename... args_type>
    void at(const std::string& time, callable_type&& f, args_type&&... args) {
        operation registration(*this);
        if (!registration.accepted)
        {
            return;
        }

        // get current time as a tm object
        auto time_now = clock::to_time_t(clock::now());
        std::tm tm = *std::localtime(&time_now);

        // our final time as a time_point
        clock::time_point tp;

        if (try_parse(tm, time, "%H:%M:%S"))
        {
            // convert tm back to time_t, then to a time_point and assign to final
            tp = clock::from_time_t(std::mktime(&tm));

            // if we've already passed this time, the user will mean next day, so add a day.
            if (clock::now() >= tp)
            {
                tp += std::chrono::hours(24);
            }
        }
        else if (try_parse(tm, time, "%Y-%m-%d %H:%M:%S"))
        {
            tp = clock::from_time_t(std::mktime(&tm));
        }
        else if (try_parse(tm, time, "%Y/%m/%d %H:%M:%S"))
        {
            tp = clock::from_time_t(std::mktime(&tm));
        }
        else
        {
            // could not parse time
            throw std::runtime_error("Cannot parse time string: " + time);
        }

        std::shared_ptr<scheduler_task> task =
            std::make_shared<in_task>(std::bind(std::forward<callable_type>(f), std::forward<args_type>(args)...));
        add_task(tp, std::move(task));
    }

    template<typename callable_type, typename... args_type>
    void every(const clock::duration time, callable_type&& f, args_type&&... args) {
        operation registration(*this, true);
        if (!registration.accepted)
        {
            return;
        }

        std::shared_ptr<scheduler_task> t = std::make_shared<every_task>(
            time, std::bind(std::forward<callable_type>(f), std::forward<args_type>(args)...));
        auto next_time = t->get_new_time();
        add_task(next_time, std::move(t));
    }

    // expression format:
    // from https://en.wikipedia.org/wiki/Cron#Overview
    //    ┌───────────── minute (0 - 59)
    //    │ ┌───────────── hour (0 - 23)
    //    │ │ ┌───────────── day of month (1 - 31)
    //    │ │ │ ┌───────────── month (1 - 12)
    //    │ │ │ │ ┌───────────── day of week (0 - 6) (Sunday to Saturday)
    //    │ │ │ │ │
    //    │ │ │ │ │
    //    * * * * *
    template<typename callable_type, typename... args_type>
    void cron(const std::string& expression, callable_type&& f, args_type&&... args) {
        operation registration(*this, true);
        if (!registration.accepted)
        {
            return;
        }

        std::shared_ptr<scheduler_task> t = std::make_shared<cron_task>(
            expression, std::bind(std::forward<callable_type>(f), std::forward<args_type>(args)...));
        auto next_time = t->get_new_time();
        add_task(next_time, std::move(t));
    }

    template<typename callable_type, typename... args_type>
    void interval(const clock::duration time, callable_type&& f, args_type&&... args) {
        operation registration(*this, true);
        if (!registration.accepted)
        {
            return;
        }

        std::shared_ptr<scheduler_task> t = std::make_shared<every_task>(
            time, std::bind(std::forward<callable_type>(f), std::forward<args_type>(args)...), true);
        add_task(clock::now(), std::move(t));
    }

private:
    std::atomic<bool> stopping_;
    std::atomic<bool> dispatcher_retired_{false};
    std::atomic<std::size_t> outstanding_{0};

    interruptable_sleep sleeper_;

    std::multimap<clock::time_point, std::shared_ptr<scheduler_task>> tasks_;
    std::mutex mutex_;
    ctpl::thread_pool threads_;

    struct callback_context;
    callback_context* callback_contexts_{nullptr};

    // Intrusive stack records keep continuation authority on this instance,
    // including when the callback and registration code live in different DSOs.
    struct callback_context
    {
        explicit callback_context(scheduler& scheduler)
            : owner(&scheduler)
            , thread(std::this_thread::get_id()) {
            std::lock_guard<std::mutex> lock(owner->mutex_);
            next = owner->callback_contexts_;
            owner->callback_contexts_ = this;
        }

        callback_context(const callback_context&) = delete;
        callback_context& operator=(const callback_context&) = delete;

        ~callback_context() {
            retire();
        }

        void retire() {
            if (!owner)
            {
                return;
            }
            auto* scheduler = owner;
            {
                std::lock_guard<std::mutex> lock(scheduler->mutex_);
                auto** entry = &scheduler->callback_contexts_;
                while (*entry != this)
                {
                    entry = &(*entry)->next;
                }
                *entry = next;
                owner = nullptr;
            }
            scheduler->sleeper_.interrupt();
        }

        scheduler* owner;
        std::thread::id thread;
        callback_context* next{nullptr};
    };

    bool in_callback() const {
        for (auto* context = callback_contexts_; context; context = context->next)
        {
            if (context->thread == std::this_thread::get_id())
            {
                return true;
            }
        }
        return false;
    }

    struct operation
    {
        explicit operation(scheduler& owner, bool recurring = false)
            : owner(owner) {
            std::lock_guard<std::mutex> lock(owner.mutex_);
            accepted = !owner.dispatcher_retired_ && (!owner.stopping_ || (!recurring && owner.in_callback()));
            if (accepted)
            {
                ++owner.outstanding_;
            }
        }

        ~operation() {
            if (accepted)
            {
                --owner.outstanding_;
                owner.sleeper_.interrupt();
            }
        }

        scheduler& owner;
        bool accepted{false};
    };

    struct dispatched_task
    {
        dispatched_task(scheduler& scheduler, std::shared_ptr<scheduler_task> task)
            : owner(scheduler)
            , context(scheduler)
            , task(std::move(task)) {
        }

        scheduler& owner;
        callback_context context;
        std::shared_ptr<scheduler_task> task;

        ~dispatched_task() {
            // Destruct user captures before publishing completion, outside the
            // scheduler mutex. Destructors may reenter stop/readiness methods.
            task.reset();
            context.retire();
            --owner.outstanding_;
            owner.sleeper_.interrupt();
        }
    };

    void add_task(const clock::time_point time, std::shared_ptr<scheduler_task> t) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!stopping_ || !t->recurring)
            {
                tasks_.emplace(time, std::move(t));
            }
        }
        sleeper_.interrupt();
    }

    void dispatch() {
        try
        {
            while (true)
            {
                decltype(tasks_) retired;
                clock::time_point next;
                bool empty;
                bool finished;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (stopping_)
                    {
                        for (auto i = tasks_.begin(); i != tasks_.end();)
                        {
                            if (i->second->recurring)
                            {
                                retired.insert(tasks_.extract(i++));
                            }
                            else
                            {
                                ++i;
                            }
                        }
                    }
                    empty = tasks_.empty();
                    finished = stopping_ && empty && outstanding_ == 0 && !callback_contexts_;
                    if (!empty)
                    {
                        next = tasks_.begin()->first;
                    }
                }
                retired.clear();
                if (finished)
                {
                    dispatcher_retired_ = true;
                    dispatcher_retired_.notify_all();
                    return;
                }
                if (empty)
                {
                    sleeper_.sleep();
                }
                else
                {
                    sleeper_.sleep_until(next);
                }
                manage_tasks();
            }
        }
        catch (...)
        {
            // A failed dispatcher cannot promise completion of accepted work.
            // Keep readiness closed and preserve owners for the owner's fallback.
            stop_accepting();
            std::fputs("Scheduler dispatch failed; shutdown readiness remains closed.\n", stderr);
            throw;
        }
    }

    void manage_tasks() {
        // Keep extracted owners alive until after releasing the dispatch lock:
        // a fast worker can otherwise make map erasure run user destructors here.
        decltype(tasks_) dispatched;
        std::lock_guard<std::mutex> lock(mutex_);
        auto end = tasks_.upper_bound(clock::now());
        for (auto i = tasks_.begin(); i != end;)
        {
            dispatched.insert(tasks_.extract(i++));
        }
        for (auto& entry : dispatched)
        {
            auto& task = entry.second;
            if (stopping_ && task->recurring)
            {
                continue;
            }
            ++outstanding_;
            try
            {
                threads_.push([this, task](int) mutable {
                    dispatched_task work{*this, std::move(task)};
                    try
                    {
                        work.task->function();
                        if (work.task->interval && !stopping_)
                        {
                            add_task(work.task->get_new_time(), work.task);
                        }
                    }
                    catch (...)
                    {
                        // The discarded pool future previously contained this
                        // exception. Destroy it here before retiring business
                        // owners, and report it instead of retaining DSO code.
                        std::fputs("Scheduler callback failed.\n", stderr);
                    }
                });
            }
            catch (...)
            {
                --outstanding_;
                throw;
            }
            if (task->recurring && !task->interval)
            {
                tasks_.emplace(task->get_new_time(), task);
            }
            // The worker cannot enter its context until this lock is released.
            // Hand its dispatched owner over completely now, so a fast callback
            // retires its last capture with that callback's continuation authority.
            task.reset();
        }
    }

    inline bool try_parse(std::tm& tm, const std::string& expression, const std::string& format) {
        std::stringstream ss(expression);
        return !(ss >> std::get_time(&tm, format.c_str())).fail();
    }
};

} // namespace ark
