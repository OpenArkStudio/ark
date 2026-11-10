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
#include "ark/physic/physic_module.hpp"
#include "ark/plugin_manager.hpp"
#include "ark/random/random_xoshiro.hpp"

namespace ark {

using sample_actor_direction_list = std::vector<physic3_d>;

struct sample_actor_direction {
    int move_count{0};
    sample_actor_direction_list direction_list;
};

using move_count_actor_direction_list = std::map<int, sample_actor_direction>;

struct sample_move_step {
    float move_step{0.0F};
    move_count_actor_direction_list count_actor_move_list;
};

using step_move_count_actor_move_list = std::map<int, sample_move_step>;

class sample_scene {
public:
    bool is_in_range(const physic3_d& pos) const {
        if (pos.x < min_pos.x || pos.z < min_pos.z) {
            return false;
        }

        if (pos.x > max_pos.x || pos.z > max_pos.z) {
            return false;
        }

        return true;
    }

    std::string scene_id{""};
    physic3_d min_pos;
    physic3_d max_pos;

    guid_t scene_obj_id{0U};
    std::vector<guid_t> actor_list;
};

class physics_scene_module final : public module {
    ARK_DECLARE_MODULE_FUNCTIONS
public:
    explicit physics_scene_module() = default;

    bool init() override;
    bool post_init() override;
    bool pre_shut() override;
    bool shut() override;

protected:
    void init_base();
    void init_move();
    void init_move_step(const int step);
    void init_actor_direction(const int move_count, sample_move_step& sample_move_step);
    void create_scene();
    void create_actor();
    void move_actor();
    void move_actor(const sample_scene& scene);
    void move_actor(const sample_scene& scene, sample_actor_direction& actor_move, const float distance);
    physic3_d random_move_direction();
    void log_ray_cast(const sample_scene& scene, const int hit, float distance, const physic3_d& hit_pos);
    void re_random_position(const sample_scene& scene);
    physic3_d random_position(const sample_scene& scene);
    void test_raycast();

protected:
    physic_config config_;
    xoroshiro128_plus generator_;

    std::vector<sample_scene> sample_scene_list_;
    step_move_count_actor_move_list sample_move_list_;

protected:
    physic_module* physic_module_;
    physic_config_module* physic_config_module_;
};

} // namespace ark
