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

#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>

#include "ark/module_base.hpp"
#include "ark/utility/schedule_module.hpp"
#include "ark/utility/scheduler.hpp"
#include "utility_plugin.hpp"

namespace ark {

namespace {

struct startup_schedule_state
{
    std::mutex mutex;
    bool exposed{};
    bool sealed{};
    std::future<void> dispatch;
    std::unique_ptr<scheduler> worker;
};

} // namespace

class schedule_module_impl final : public module_base<schedule_module>
{
    friend struct schedule_startup_test_access;

public:
    bool init() override;
    bool ready_to_shut() const override;
    bool pre_shut() override;

protected:
    void add_interval_schedule(const std::chrono::system_clock::duration interval, scheduler_functor&& cb) override;

    void add_at_schedule(const std::string& target_time, scheduler_functor&& cb) override;
    void add_cron_schedule(const std::string& cron_expression, scheduler_functor&& cb) override;

private:
    static startup_cleanup_result release_startup(void* context);
    scheduler* begin_registration();
    std::shared_ptr<startup_schedule_state> startup_;
};

bool schedule_module_impl::init() {
    auto state = std::make_shared<startup_schedule_state>();
    auto* manager = get_plugin_manager();
    if (manager && !manager->prepare_startup_cleanup("Schedule", "ark_utility_plugin", state, release_startup))
    {
        return false;
    }
    startup_ = std::move(state);
    startup_->worker.reset(new scheduler(16, &startup_->dispatch));
    return true;
}

startup_cleanup_result schedule_module_impl::release_startup(void* context) {
    auto& state = *static_cast<startup_schedule_state*>(context);
    {
        std::lock_guard lock(state.mutex);
        if (state.exposed)
        {
            return startup_cleanup_result::unsafe;
        }
        state.sealed = true;
    }
    if (!state.worker)
    {
        return startup_cleanup_result::done;
    }

    // No user registration has begun. Only the internal dispatcher can run;
    // native waiting here must not depend on module updates or user callbacks.
    state.worker->close();
    state.dispatch.wait();
    state.worker->threads_.stop(true);
    if (!state.worker->ready_to_shut())
    {
        // A failed internal dispatcher cannot certify ordinary readiness. Keep
        // the owner and any stored exception instead of entering its destructor.
        return startup_cleanup_result::failed;
    }
    state.worker.reset();
    return startup_cleanup_result::done;
}

bool schedule_module_impl::ready_to_shut() const {
    if (startup_ && startup_->worker)
    {
        startup_->worker->close();
    }
    return !startup_ || !startup_->worker || startup_->worker->ready_to_shut();
}

bool schedule_module_impl::pre_shut() {
    return ready_to_shut() && (!startup_ || !startup_->worker || startup_->worker->pre_shut());
}

scheduler* schedule_module_impl::begin_registration() {
    if (!startup_)
    {
        throw std::logic_error("Schedule module is not initialized");
    }
    std::lock_guard lock(startup_->mutex);
    if (startup_->sealed)
    {
        return nullptr;
    }
    if (!startup_->worker)
    {
        throw std::logic_error("Schedule worker is not available");
    }
    // A failed allocation or callback-registration attempt may already have
    // exposed borrowed state. Never infer safety from a later empty queue.
    startup_->exposed = true;
    return startup_->worker.get();
}

void schedule_module_impl::add_cron_schedule(const std::string& cron_expression, scheduler_functor&& cb) {
    if (auto* worker = begin_registration())
    {
        worker->cron(cron_expression, std::move(cb));
    }
}

void schedule_module_impl::add_interval_schedule(
    const std::chrono::system_clock::duration interval, scheduler_functor&& cb) {
    if (auto* worker = begin_registration())
    {
        worker->interval(interval, std::move(cb));
    }
}

void schedule_module_impl::add_at_schedule(const std::string& target_time, scheduler_functor&& cb) {
    if (auto* worker = begin_registration())
    {
        worker->at(target_time, std::move(cb));
    }
}

namespace detail {
std::unique_ptr<schedule_module> make_schedule_module() {
    return std::make_unique<schedule_module_impl>();
}
} // namespace detail

} // namespace ark
