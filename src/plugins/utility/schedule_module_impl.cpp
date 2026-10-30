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

#include "ark/module_base.hpp"
#include "ark/utility/schedule_module.hpp"
#include "ark/utility/scheduler.hpp"
#include "utility_plugin.hpp"

namespace ark {

class schedule_module_impl final : public module_base<schedule_module>
{
public:
    bool init() override;
    bool stop_accepting() override;
    bool ready_to_shut() const override;
    bool pre_shut() override;

protected:
    void add_interval_schedule(const std::chrono::system_clock::duration interval, scheduler_functor&& cb) override;

    void add_at_schedule(const std::string& target_time, scheduler_functor&& cb) override;
    void add_cron_schedule(const std::string& cron_expression, scheduler_functor&& cb) override;

private:
    bool is_continuation() const;

    const std::thread::id owner_ = std::this_thread::get_id();
    std::shared_ptr<scheduler> scheduler_{nullptr};
    // Manager readiness runs after accepted tasks have drained. Until then,
    // leave the dispatcher available for their one-shot continuations.
    mutable std::unique_ptr<scheduler::operation> accepted_tasks_;
};

bool schedule_module_impl::init() {
    scheduler_ = std::make_shared<scheduler>(16);
    accepted_tasks_ = std::make_unique<scheduler::operation>(*scheduler_);
    return true;
}

bool schedule_module_impl::stop_accepting() {
    if (scheduler_)
    {
        scheduler_->stop_accepting();
    }
    return true;
}

bool schedule_module_impl::ready_to_shut() const {
    if (scheduler_ && scheduler_->stopping_ && std::this_thread::get_id() == owner_)
    {
        auto* manager = get_plugin_manager();
        if (!manager || (!manager->is_polling_tasks() && manager->pending_tasks() == 0))
        {
            accepted_tasks_.reset();
        }
    }
    return !scheduler_ || scheduler_->ready_to_shut();
}

bool schedule_module_impl::pre_shut() {
    return ready_to_shut() && (!scheduler_ || scheduler_->pre_shut());
}

void schedule_module_impl::add_cron_schedule(const std::string& cron_expression, scheduler_functor&& cb) {
    scheduler_->cron(cron_expression, std::move(cb));
}

void schedule_module_impl::add_interval_schedule(
    const std::chrono::system_clock::duration interval, scheduler_functor&& cb) {
    scheduler_->interval(interval, std::move(cb));
}

void schedule_module_impl::add_at_schedule(const std::string& target_time, scheduler_functor&& cb) {
    if (is_continuation())
    {
        scheduler::callback_context context(*scheduler_);
        scheduler_->at(target_time, std::move(cb));
        return;
    }
    scheduler_->at(target_time, std::move(cb));
}

bool schedule_module_impl::is_continuation() const {
    auto* manager = get_plugin_manager();
    return manager && std::this_thread::get_id() == owner_ && manager->is_polling_tasks();
}

namespace detail {
std::unique_ptr<schedule_module> make_schedule_module() {
    return std::make_unique<schedule_module_impl>();
}
} // namespace detail

} // namespace ark
