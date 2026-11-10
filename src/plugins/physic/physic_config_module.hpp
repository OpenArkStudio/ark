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

#include "ark/module.hpp"
#include "ark/physic/physic_define.hpp"

namespace ark {

class physic_config_module : public module {
public:
    ///get scene by config id
    virtual bool get_physic_scene(const std::string& scene_id, physic_scene& scene) const = 0;
    virtual const physic_scene_list& get_physic_scene_list() const = 0;
    virtual const physic_config& get_physic_config() const = 0;
};

} // namespace ark
