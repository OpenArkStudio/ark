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
#include "ark/physic/physic_module.hpp"
#include "simulate_module.hpp"

namespace ark {

class simulate_module_impl final : public module_base<simulate_module> {
public:
    bool init() override;

    bool create_scene(const std::string& scene_id, const int32_t index) override;
    bool destroy_scene(const std::string& scene_id, const int32_t index) override;

    bool create_box(const std::string& scene_id, const int32_t index, const simulate_net_id obj_id,
        const physic3_d& position, const physic3_d& size, bool dynamic) override;
    bool create_capsule(const std::string& scene_id, const int32_t index, const simulate_net_id obj_id,
        const physic3_d& position, const float raius, const float height, bool dynamic) override;
    bool create_sphere(const std::string& scene_id, const int32_t index, const simulate_net_id obj_id,
        const physic3_d& position, const float raius, bool dynamic) override;

    bool update_actor(const std::string& scene_id, const int32_t index, const simulate_net_id obj_id,
        const physic3_d& position) override;
    bool destroy_actor(const std::string& scene_id, const int32_t index, const simulate_net_id obj_id) override;

    bool ray_cast(const std::string& scene_id, const int32_t index, const physic3_d& start, const physic3_d& direction,
        const float distance, simulate::ack_ray_cast& ack_ray_cast) override;

private:
    bool register_trigger_call_back(simulate_trigger_cb_function&& cb) override;

    void insert_scene_obj_actors(const guid_t& scene_obj_id, const std::string& scene_id, const int32_t index);
    void remove_scene_obj_actors(const std::string& scene_id, const int32_t index);
    simulate_scene* find_scene_obj_actors(const std::string& scene_id, const int32_t index);

    simulate::simulate_vec3 to_simulation_vector(const physic3_d& vec3);

    void trigger_call_back(const guid_t& scene_obj_id, const physic_trigger_pair& trigger_pair);

private:
    scene_index_scene_map scene_index_scene_list_;
    scene_index_list scene_index_pair_list_;

    simulate_trigger_cb_function trigger_call_back_function_{nullptr};

    physic_module* physic_module_{nullptr};
};

} // namespace ark
