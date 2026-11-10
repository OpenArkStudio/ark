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

#include "ark/physic/physic_config_module.hpp"
#include "ark/plugin_manager.hpp"

namespace ark {

class physic_config_module_impl final : public physic_config_module {
    ARK_DECLARE_MODULE_FUNCTIONS
public:
    bool init() override;
    bool shut() override;

    bool get_physic_scene(const std::string& scene_id, physic_scene& scene) const override;
    const physic_scene_list& get_physic_scene_list() const override;
    const physic_config& get_physic_config() const override;

private:
    bool load_scene();
    bool load_terrain(physic_scene& scene, const std::string& file_path);
    bool load_collider(physic_scene& scene, const std::string& file_path);
    bool get_scene_file_list(const std::string& father_path, const std::string& path,
        std::multimap<std::string, std::string>& sub_path_list);
    bool load_config();

    std::string gen_cooked_file_path(const std::string& file_path);
    void create_file_path(const std::string& file_path);

protected:
    physic_scene_list scene_list_;
    physic_config config_;
};

} // namespace ark
