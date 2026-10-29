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

#include "ark/module_base.hpp"
#include "ark/utility/guid_module.hpp"
#include "ark/utility/schedule_module.hpp"
#include "ark/utility/timer_module.hpp"

namespace ark {

class timers_module final : public module_base<module, timer_module, guid_module>
{
public:
    bool init() override;
    bool post_init() override;
    bool update() override;
    bool pre_shut() override;
    bool shut() override;

protected:
    void test_oss_log();
    void test_cron_scheduler();
    void timer_test(uint64_t timer_id, const guid_t& entity_id);

protected:
    timer_module* timer_module_;
    guid_module* guid_module_;
    schedule_module* schedule_module_;
};

} // namespace ark
