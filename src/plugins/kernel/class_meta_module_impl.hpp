/*
 * This source file is part of ArkNX
 * For the latest info, see https://github.com/ArkNX
 *
 * Copyright (c) 2013-2020 ArkNX authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
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

#include "ark/container/map.hpp"
#include "ark/kernel/class_meta.hpp"
#include "ark/kernel/class_meta_manager.hpp"
#include "ark/kernel/class_meta_module.hpp"
#include "ark/module_base.hpp"

namespace ark {

class class_meta_module_impl final : public module_base<class_meta_module> {
public:
    class_meta_module_impl() = default;
    ~class_meta_module_impl() override = default;

    bool init() override;
    bool shut() override;

    bool load() override;

    bool add_class_call_back(const std::string& class_name, class_event_functor&& cb, const int32_t prio) override;

    bool do_class_event(const guid_t& id, const std::string& class_name, const entity_event class_event,
        const data_list& args) override;

    std::shared_ptr<class_meta> find_meta(const std::string& class_name) const override;
    const class_meta_manager::class_meta_list& get_meta_list() const override;

    std::shared_ptr<class_meta> find_static_meta(const std::string& class_name) const override;
    const class_meta_manager::class_meta_list& get_static_meta_list() const override;

private:
    class_meta_manager* class_meta_manager_{nullptr};
};

} // namespace ark
