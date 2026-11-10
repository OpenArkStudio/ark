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

namespace ark {

///3d position
class base_vec3_d {
public:
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};

    base_vec3_d() = default;

    base_vec3_d(float a, float b, float c)
        : x(a)
        , y(b)
        , z(c) {
    }

    base_vec3_d(const base_vec3_d& rhs) {
        x = rhs.x;
        y = rhs.y;
        z = rhs.z;
    }

    const base_vec3_d& operator=(const base_vec3_d& rhs) {
        if (this != &rhs) {
            x = rhs.x;
            y = rhs.y;
            z = rhs.z;
        }
        return *this;
    }
};

using physic3_d = base_vec3_d;

///physic ray hit
struct physic_ray_hit {
    bool is_hit{false};
    float hit_distance{0.0F};
    guid_t hit_obj_id{0U};
    physic3_d hit_pos;
};

///physic trigger pair
struct physic_trigger_pair {
    guid_t trigger_obj_id{0U};
    guid_t other_obj_id{0U};
};

///call back function
using trigger_call_back_function = std::function<void(const guid_t&, const physic_trigger_pair&)>;

class physic_module : public module {
public:
    ///create a new scene
    virtual guid_t create_scene(const std::string& scene_id) = 0;

    ///destroy a scene
    virtual bool destroy_scene(const guid_t& scene_obj_id) = 0;

    ///raycast
    virtual bool ray_cast(const guid_t& scene_obj_id, const physic3_d& origin, const physic3_d& direction,
        const float distance, physic_ray_hit& ray_hit) = 0;

    ///create a box actor
    virtual guid_t create_dynamic_box_actor(
        const guid_t& scene_obj_id, const physic3_d& position, const physic3_d& size) = 0;
    virtual guid_t create_static_box_actor(
        const guid_t& scene_obj_id, const physic3_d& position, const physic3_d& size) = 0;

    ///create a capsule actor
    virtual guid_t create_dynamic_capsule_actor(
        const guid_t& scene_obj_id, const physic3_d& position, const float radius, const float height) = 0;
    virtual guid_t create_static_capsule_actor(
        const guid_t& scene_obj_id, const physic3_d& position, const float radius, const float height) = 0;

    ///create a sphere actor
    virtual guid_t create_dynamic_sphere_actor(
        const guid_t& scene_obj_id, const physic3_d& position, const float radius) = 0;
    virtual guid_t create_static_sphere_actor(
        const guid_t& scene_obj_id, const physic3_d& position, const float radius) = 0;

    ///update actor
    virtual bool update_actor(const guid_t& actor_obj_id, const physic3_d& position) = 0;

    ///destroy an actor
    virtual bool destroy_actor(const guid_t& actor_obj_id) = 0;

    ///get actor position
    virtual bool get_actor_position(const guid_t& actor_obj_id, physic3_d& position) = 0;

    ///add a child actor
    //virtual bool AddChild(physx::PxActor* parentActor, const physx::PxTransform& localParent,
    //    physx::PxActor* childActor, const physx::PxTransform& localChild) = 0;

    ///remove a child actor
    //virtual bool RemoveChild(
    //    physx::PxActor* parentActor, physx::PxActor* childActor, const physx::PxTransform& childTransform) = 0;

    ///register trigger call back
    template<typename base_type>
    bool register_trigger_call_back(
        base_type* base_ptr, void (base_type::*handler)(const guid_t&, const physic_trigger_pair&)) {
        auto functor = std::bind(handler, base_ptr, std::placeholders::_1, std::placeholders::_2);
        return register_trigger_call_back(std::move(functor));
    }

    ///ray get point
    virtual physic3_d ray_get_point(const physic3_d& origin, const physic3_d& direction, const float distance) = 0;

    ///get height
    virtual float get_height(const guid_t& scene_obj_id, const physic3_d& position) = 0;

private:
    virtual bool register_trigger_call_back(trigger_call_back_function&& cb) = 0;
};

} // namespace ark
