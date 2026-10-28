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
#include "ark/time/timer.hpp"
#include "ark/utility/timer_module.hpp"
#include "utility_plugin.hpp"

namespace ark {

class timer_module_impl final : public module_base<timer_module>
{
public:
    bool init() override;
    bool update() override;
    bool pre_shut() override;

    bool remove_timer(const uint64_t timer_id) override;

protected:
    uint64_t add_single_timer(const guid_t& entity_id, const std::chrono::milliseconds interval, const uint32_t count,
        timer_functor&& cb) override;
    uint64_t add_forever_timer(
        const guid_t& entity_id, const std::chrono::milliseconds interval, timer_functor&& cb) override;

private:
    std::shared_ptr<time_wheel_manager> timer_manager_{nullptr};
};

bool timer_module_impl::init() {
    timer_manager_ = std::make_shared<time_wheel_manager>();
    timer_manager_->init(get_plugin_manager()->get_now_time());

    return timer_manager_->initialized();
}

bool timer_module_impl::pre_shut() {
    timer_manager_->shut();
    return true;
}

bool timer_module_impl::update() {
    timer_manager_->update(get_plugin_manager()->get_now_time());
    return true;
}

bool timer_module_impl::remove_timer(const uint64_t timer_id) {
    return timer_manager_->remove_timer(timer_id);
}

uint64_t timer_module_impl::add_single_timer(
    const guid_t& entity_id, const std::chrono::milliseconds interval, const uint32_t count, timer_functor&& cb) {
    return timer_manager_->add_timer(entity_id, count, interval, std::move(cb));
}

uint64_t timer_module_impl::add_forever_timer(
    const guid_t& entity_id, const std::chrono::milliseconds interval, timer_functor&& cb) {
    return timer_manager_->add_timer(entity_id, 0, interval, std::move(cb));
}

namespace detail {
std::unique_ptr<timer_module> make_timer_module() {
    return std::make_unique<timer_module_impl>();
}
} // namespace detail

} // namespace ark
