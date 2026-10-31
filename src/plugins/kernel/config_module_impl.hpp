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

#include "ark/kernel/class_meta_module.hpp"
#include "ark/kernel/config_module.hpp"
#include "ark/module_base.hpp"

namespace ark {

class config_module_impl final : public module_base<config_module, class_meta_module>
{
public:
    bool init() override;
    bool post_init() override;
    bool shut() override;

    bool load() override;
    bool save() override;
    bool clear() override;

    std::shared_ptr<static_entity> find_static_entity(const id_type config_id) override;

protected:
    bool load_config(std::shared_ptr<class_meta> class_meta_ptr);

private:
    bool loaded_{false};

    class_meta_module* class_meta_module_{nullptr};
    static_entity_manager* static_entity_manager_{nullptr};
};

} // namespace ark
