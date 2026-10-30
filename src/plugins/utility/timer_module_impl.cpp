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

namespace {

struct startup_timer_capsule
{
    std::unique_ptr<time_wheel_manager> wheel;
};

startup_cleanup_result release_startup_timer(void* context) {
    auto& capsule = *static_cast<startup_timer_capsule*>(context);
    if (capsule.wheel && capsule.wheel->has_retained_callbacks())
    {
        return startup_cleanup_result::unsafe;
    }
    if (capsule.wheel)
    {
        capsule.wheel->shut();
        capsule.wheel.reset();
    }
    return startup_cleanup_result::done;
}

} // namespace

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
    std::shared_ptr<startup_timer_capsule> timer_capsule_;
};

bool timer_module_impl::init() {
    // The manager owns this capsule independently of the module. Enroll before
    // acquiring wheel storage; cleanup never captures an incomplete module.
    auto capsule = std::make_shared<startup_timer_capsule>();
    if (!get_plugin_manager()->prepare_startup_cleanup("Timer", "ark_utility_plugin", capsule, release_startup_timer))
    {
        return false;
    }
    timer_capsule_ = std::move(capsule);
    timer_capsule_->wheel = std::make_unique<time_wheel_manager>();
    timer_capsule_->wheel->init(get_plugin_manager()->get_now_time());

    return timer_capsule_->wheel->initialized();
}

bool timer_module_impl::pre_shut() {
    if (timer_capsule_ && timer_capsule_->wheel)
    {
        timer_capsule_->wheel->shut();
    }
    return true;
}

bool timer_module_impl::update() {
    timer_capsule_->wheel->update(get_plugin_manager()->get_now_time());
    return true;
}

bool timer_module_impl::remove_timer(const uint64_t timer_id) {
    return timer_capsule_->wheel->remove_timer(timer_id);
}

uint64_t timer_module_impl::add_single_timer(
    const guid_t& entity_id, const std::chrono::milliseconds interval, const uint32_t count, timer_functor&& cb) {
    return timer_capsule_->wheel->add_timer(entity_id, count, interval, std::move(cb));
}

uint64_t timer_module_impl::add_forever_timer(
    const guid_t& entity_id, const std::chrono::milliseconds interval, timer_functor&& cb) {
    return timer_capsule_->wheel->add_timer(entity_id, 0, interval, std::move(cb));
}

namespace detail {
std::unique_ptr<timer_module> make_timer_module() {
    return std::make_unique<timer_module_impl>();
}
} // namespace detail

} // namespace ark
