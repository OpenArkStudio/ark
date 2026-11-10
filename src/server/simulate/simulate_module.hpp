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
#include "ark/physic/physic_module.hpp"
#include "simulate_define.hpp"
#include "simulate_msg_define.hpp"

namespace ark {

class simulate_module : public module {
public:
    virtual bool create_scene(const std::string& scene_id, const int32_t index) = 0;
    virtual bool destroy_scene(const std::string& scene_id, const int32_t index) = 0;

    virtual bool create_box(const std::string& scene_id, const int32_t index, const simulate_net_id obj_id,
        const physic3_d& position, const physic3_d& size, bool dynamic) = 0;
    virtual bool create_capsule(const std::string& scene_id, const int32_t index, const simulate_net_id obj_id,
        const physic3_d& position, const float raius, const float height, bool dynamic) = 0;
    virtual bool create_sphere(const std::string& scene_id, const int32_t index, const simulate_net_id obj_id,
        const physic3_d& position, const float raius, bool dynamic) = 0;

    virtual bool update_actor(
        const std::string& scene_id, const int32_t index, const simulate_net_id obj_id, const physic3_d& position) = 0;
    virtual bool destroy_actor(const std::string& scene_id, const int32_t index, const simulate_net_id obj_id) = 0;

    virtual bool ray_cast(const std::string& scene_id, const int32_t index, const physic3_d& start,
        const physic3_d& direction, const float distance, simulate::ack_ray_cast& ack_ray_cast) = 0;

    template<typename base_type>
    bool register_trigger_call_back(base_type* base_ptr, void (base_type::*handler)(const std::string& scene_id,
                                                             const int32_t index, const simulate::trigger_pair& pair)) {
        auto functor =
            std::bind(handler, base_ptr, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
        return register_trigger_call_back(std::move(functor));
    }

private:
    virtual bool register_trigger_call_back(simulate_trigger_cb_function&& cb) = 0;
};

} // namespace ark
