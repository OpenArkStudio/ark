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
#include "ark/physic/physic_define.hpp"
#include "ark/physic/physic_module.hpp"
#include "ark/plugin_manager.hpp"
#include "ark/utility/guid_module.hpp"
#include "ark/utility/timer_module.hpp"

namespace ark {

class simulation_event_callback final : public physx::PxSimulationEventCallback {
public:
    void onContact(const physx::PxContactPairHeader& pair_header, const physx::PxContactPair* pairs,
        physx::PxU32 nb_pairs) override {};

    void onTrigger(physx::PxTriggerPair* pairs, physx::PxU32 count) override {
        for (physx::PxU32 i = 0; i < count; i++) {
            // ignore pairs when shapes have been deleted
            auto& one_pair = pairs[i];
            if (one_pair.flags &
                (physx::PxTriggerPairFlag::eREMOVED_SHAPE_TRIGGER | physx::PxTriggerPairFlag::eREMOVED_SHAPE_OTHER)) {
                continue;
            }

            auto actor_trigger = one_pair.triggerActor;
            if (!actor_trigger || !actor_trigger->userData) {
                continue;
            }

            auto px_scene = actor_trigger->getScene();
            if (!px_scene || !px_scene->userData) {
                continue;
            }

            auto scene_obj_id = reinterpret_cast<guid_t>(px_scene->userData);

            physic_trigger_pair trigger_pair;
            trigger_pair.trigger_obj_id = reinterpret_cast<guid_t>(actor_trigger->userData);
            if (one_pair.otherActor && one_pair.otherActor->userData) {
                trigger_pair.other_obj_id = reinterpret_cast<guid_t>(one_pair.otherActor->userData);
            }

            for (auto& iter : trigger_cb_list_) {
                iter(scene_obj_id, trigger_pair);
            }
        }
    }

    void onConstraintBreak(physx::PxConstraintInfo*, physx::PxU32) override {
    }

    void onWake(physx::PxActor**, physx::PxU32) override {
    }

    void onSleep(physx::PxActor**, physx::PxU32) override {
    }

    void onAdvance(const physx::PxRigidBody* const*, const physx::PxTransform*, const physx::PxU32) override {
    }

    void register_trigger_call_back(trigger_call_back_function&& cb) {
        trigger_cb_list_.push_back(std::forward<trigger_call_back_function>(cb));
    }

private:
    using trigger_cb_list = std::list<trigger_call_back_function>;
    trigger_cb_list trigger_cb_list_;
};

class file_output_stream : public physx::PxOutputStream {
public:
    file_output_stream(const std::string& name) {
        physic::open_file(&file_, name.c_str(), "wb");
    }

    virtual ~file_output_stream() {
        release();
    }

    physx::PxU32 write(const void* src, physx::PxU32 count) override {
        return file_ ? physx::PxU32(fwrite(src, 1, count, file_)) : 0;
    }

    bool valid() {
        return file_ != nullptr;
    }

    void release() {
        if (file_) {
            fclose(file_);
            file_ = nullptr;
        }
    }

private:
    PxFileHandle file_{nullptr};
};

///physic scene
class physic_scene_cooked {
public:
    ~physic_scene_cooked() {
        scene = nullptr;
        terrain_mesh = nullptr;
        colliders.clear();
        triangle_colliders.clear();
        convex_colliders.clear();
    }

    physic_scene* scene{nullptr};
    physx::PxTriangleMesh* terrain_mesh{nullptr};
    std::list<collider*> colliders;
    std::map<physx::PxTriangleMesh*, physx::PxVec3> triangle_colliders;
    std::map<physx::PxConvexMesh*, physx::PxVec3> convex_colliders;
};

class physic_module_impl final : public physic_module {
    ARK_DECLARE_MODULE_FUNCTIONS
public:
    bool init() override;
    bool shut() override;
    bool post_init() override;

    guid_t create_scene(const std::string& scene_id) override;
    bool destroy_scene(const guid_t& scene_obj_id) override;

    bool ray_cast(const guid_t& scene_obj_id, const physic3_d& origin, const physic3_d& direction, const float distance,
        physic_ray_hit& ray_hit) override;

    guid_t create_dynamic_box_actor(
        const guid_t& scene_obj_id, const physic3_d& position, const physic3_d& size) override;
    guid_t create_static_box_actor(
        const guid_t& scene_obj_id, const physic3_d& position, const physic3_d& size) override;
    guid_t create_dynamic_capsule_actor(
        const guid_t& scene_obj_id, const physic3_d& position, const float radius, const float height) override;
    guid_t create_static_capsule_actor(
        const guid_t& scene_obj_id, const physic3_d& position, const float radius, const float height) override;
    guid_t create_dynamic_sphere_actor(
        const guid_t& scene_obj_id, const physic3_d& position, const float radius) override;
    guid_t create_static_sphere_actor(
        const guid_t& scene_obj_id, const physic3_d& position, const float radius) override;

    bool update_actor(const guid_t& actor_obj_id, const physic3_d& position) override;
    bool destroy_actor(const guid_t& actor_obj_id) override;
    bool get_actor_position(const guid_t& actor_obj_id, physic3_d& position) override;

    //bool AddChild(physx::PxActor* parentActor, const physx::PxTransform& localParent, physx::PxActor* childActor,
    //    const physx::PxTransform& localChild) override;
    //bool RemoveChild(
    //    physx::PxActor* parentActor, physx::PxActor* childActor, const physx::PxTransform& childTransform) override;

    physic3_d ray_get_point(const physic3_d& origin, const physic3_d& direction, const float distance) override;

    float get_height(const guid_t& scene_obj_id, const physic3_d& position) override;

private:
    ///inner functions
    bool ray_cast(const guid_t& scene_obj_id, const physx::PxVec3& origin, const physx::PxVec3& direction,
        const float distance, physx::PxRaycastBuffer& ray_hit);

    bool register_trigger_call_back(trigger_call_back_function&& cb) override;

    bool cook_scene_mesh(const physic_scene& scene);
    bool find_physic_scene(const std::string& scene_id, physic_scene_cooked& scene_cooked);

    bool init_physic();
    void create_pvd_connection();
    bool init_scene(physx::PxScene* px_scene, const physic_scene_cooked& scene_cooked);

    physx::PxRigidStatic* create_static(collider* collider);
    physx::PxRigidStatic* create_static(const physx::PxTransform& transform, const physx::PxGeometry& geometry,
        physx::PxMaterial& material, const physx::PxTransform& shape_offset = physx::PxTransform(physx::PxIdentity));
    physx::PxRigidDynamic* create_dynamic(const physx::PxTransform& transform, const physx::PxGeometry& geometry,
        physx::PxMaterial& material, float density = default_density,
        const physx::PxTransform& shape_offset = physx::PxTransform(physx::PxIdentity));

    guid_t create_dynamic_actor(
        physx::PxScene* px_scene, const physx::PxTransform& transform, const physx::PxGeometry& geometry);
    guid_t create_static_actor(
        physx::PxScene* px_scene, const physx::PxTransform& transform, const physx::PxGeometry& geometry);

    ///not for use
    physx::PxTriangleMesh* create_triangle_mesh(const physic_mesh_file& mesh_file);

    ///create mesh then store its pointer for reuse
    physx::PxTriangleMesh* create_triangle_mesh(const physic_scene& scene_config);
    physx::PxTriangleMesh* create_triangle_mesh(mesh_collider* collider);
    physx::PxConvexMesh* create_convex_mesh(mesh_collider* collider);

    void scene_pvd_setting(physx::PxScene* px_scene);
    void quat_roate(physx::PxTransform& transform);

    void on_simulate(uint64_t timer_id, const guid_t& guid);

    physx::PxScene* find_scene(const guid_t& scene_obj_id);
    physx::PxActor* find_actor(const guid_t& actor_obj_id);

    physx::PxVec3 to_physx_vector(const physic3_d& vec);
    physic3_d from_physx_vector(const physx::PxVec3& vec);

protected:
    physx::PxDefaultAllocator default_allocator_callback_;
    physx::PxDefaultErrorCallback default_error_callback_;
    simulation_event_callback simulation_event_callback_;
    physx::PxPlane plane_;

    physx::PxMaterial* material_{nullptr};

    physx::PxDefaultCpuDispatcher* cpu_dispatcher_{nullptr};
    physx::PxFoundation* foundation_{nullptr};
    physx::PxPhysics* physics_{nullptr};
    physx::PxCooking* cooking_{nullptr};
    physx::PxPvd* pvd_{nullptr};
    physx::PxPvdTransport* pvd_transport_{nullptr};

    ///scene list
    std::map<guid_t, physx::PxScene*> scene_list_;

    ///actor list
    std::map<guid_t, physx::PxActor*> actor_list_;

    ///cooked scene list
    using cooked_scene_list = std::unordered_map<std::string, physic_scene_cooked>;
    cooked_scene_list cooked_scene_list_;

    ///physic config
    physic_config config_;

    physic_config_module* physic_config_module_{nullptr};
    timer_module* timer_module_{nullptr};
    guid_module* guid_module_{nullptr};
};

} // namespace ark
