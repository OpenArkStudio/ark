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

#include "ark/physic/physic_define.hpp"
#include "ark/physic/physic_module_impl.hpp"
#include "ark/physic/wave_front.hpp"

namespace ark {

#define SAFE_RELEASE(x)                                                                                                \
    if (x) {                                                                                                           \
        x->release();                                                                                                  \
        x = NULL;                                                                                                      \
    }

#define SCENE_WRITE_LOCK(x) physx::PxSceneWriteLock write_lock(*x);

bool physic_module_impl::init() {
    physic_config_module_ = find_module<physic_config_module>();
    timer_module_ = find_module<timer_module>();
    guid_module_ = find_module<guid_module>();

    return true;
}

bool physic_module_impl::shut() {
    cooked_scene_list_.clear();

    for (auto& iter : scene_list_) {
        SAFE_RELEASE(iter.second);
    }
    scene_list_.clear();

    PxCloseExtensions();

    SAFE_RELEASE(cooking_);
    SAFE_RELEASE(physics_);
    SAFE_RELEASE(pvd_);
    SAFE_RELEASE(pvd_transport_);
    SAFE_RELEASE(cpu_dispatcher_);
    SAFE_RELEASE(foundation_);

    return true;
}

bool physic_module_impl::post_init() {
    ///store physic config
    config_ = physic_config_module_->get_physic_config();
    if (config_.step <= 0.0001) {
        ARK_LOG_ERROR("physic simulate step <= 0");
        exit(0);
    }

    ///init physic
    if (!init_physic()) {
        ARK_LOG_ERROR("physic init failed");
        exit(0);
    }

    ///cook mesh
    cooked_scene_list_.clear();
    auto& scene_list = physic_config_module_->get_physic_scene_list();
    for (auto& iter : scene_list) {
        auto& scene = iter.second;
        if (!cook_scene_mesh(scene)) {
            ARK_LOG_ERROR("cook physic scene failed scene id = {}", scene.scene_id);
            exit(0);
        }
    }

    ///add simulate timer
    if (config_.simulate) {
        std::chrono::milliseconds interval((int)(config_.step * 1000));
        timer_module_->add_forever_timer(guid_t(), interval, this, &physic_module_impl::on_simulate);
    }

    return true;
}

guid_t physic_module_impl::create_scene(const std::string& scene_id) {
    physic_scene_cooked scene_cooked;
    if (!find_physic_scene(scene_id, scene_cooked)) {
        ARK_LOG_ERROR("create scene failed scene id = {}", scene_id);
        return null_guid;
    }

    ///generate guid
    auto guid = guid_module_->create_guid();
    if (guid == null_guid) {
        ARK_LOG_ERROR("create scene failed scene id = {}", scene_id);
        return null_guid;
    }

    ///create physic scene
    physx::PxSceneDesc scene_desc(physics_->getTolerancesScale());
    scene_desc.simulationEventCallback = &simulation_event_callback_;
    scene_desc.gravity = physx::PxVec3(0.0f, -9.81f, 0.0f);
    scene_desc.cpuDispatcher = cpu_dispatcher_;
    scene_desc.filterShader = physx::PxDefaultSimulationFilterShader;

    auto px_scene = physics_->createScene(scene_desc);
    if (px_scene == nullptr) {
        ARK_LOG_ERROR("create scene failed");
        return null_guid;
    }

    if (!init_scene(px_scene, scene_cooked)) {
        ARK_LOG_ERROR("init scene failed scene");
        SAFE_RELEASE(px_scene)
        return null_guid;
    }

    ///for pvd
    scene_pvd_setting(px_scene);

    ///save to list
    px_scene->userData = (void*)guid_t(guid);
    scene_list_.insert(std::make_pair(guid, px_scene));
    return guid;
}

bool physic_module_impl::destroy_scene(const guid_t& scene_obj_id) {
    ///check whether the particular scene exists
    auto px_scene = find_scene(scene_obj_id);
    if (px_scene == nullptr) {
        ARK_LOG_ERROR("scene does not exist");
        return false;
    }

    SAFE_RELEASE(px_scene);

    scene_list_.erase(scene_obj_id);
    return true;
}

bool physic_module_impl::ray_cast(const guid_t& scene_obj_id, const physic3_d& origin, const physic3_d& direction,
    const float distance, physic_ray_hit& ray_hit) {
    auto px_origin = to_physx_vector(origin);
    auto px_direction = to_physx_vector(direction);

    physx::PxRaycastBuffer px_ray_hit;
    auto result = ray_cast(scene_obj_id, px_origin, px_direction, distance, px_ray_hit);
    if (result) {
        auto hit_actor = px_ray_hit.block.actor;
        if (hit_actor && hit_actor->userData) {
            ray_hit.hit_obj_id = reinterpret_cast<guid_t>(hit_actor->userData);
        }

        ray_hit.is_hit = result;
        ray_hit.hit_distance = px_ray_hit.block.distance;
        ray_hit.hit_pos = from_physx_vector(px_ray_hit.block.position);
    }

    return result;
}

bool physic_module_impl::ray_cast(const guid_t& scene_obj_id, const physx::PxVec3& origin,
    const physx::PxVec3& direction, const float distance, physx::PxRaycastBuffer& ray_hit) {
    auto px_scene = find_scene(scene_obj_id);
    if (px_scene == nullptr) {
        ARK_LOG_ERROR("this scene does not exist");
        return false;
    }

    auto normal_dir = direction;
    if (!normal_dir.isNormalized()) {
        normal_dir = normal_dir.getNormalized();
    }

    return px_scene->raycast(origin, normal_dir, distance, ray_hit);
}

guid_t physic_module_impl::create_dynamic_box_actor(
    const guid_t& scene_obj_id, const physic3_d& position, const physic3_d& size) {
    auto px_scene = find_scene(scene_obj_id);
    if (px_scene == nullptr) {
        ARK_LOG_ERROR("this scene does not exist");
        return null_guid;
    }

    auto px_size = to_physx_vector(size);
    auto px_position = to_physx_vector(position);

    physx::PxBoxGeometry geometry(px_size * 0.5F);
    auto transform = physx::PxTransform(px_position);
    return create_dynamic_actor(px_scene, transform, geometry);
}

guid_t physic_module_impl::create_static_box_actor(
    const guid_t& scene_obj_id, const physic3_d& position, const physic3_d& size) {
    auto px_scene = find_scene(scene_obj_id);
    if (px_scene == nullptr) {
        ARK_LOG_ERROR("this scene does not exist");
        return null_guid;
    }

    auto px_size = to_physx_vector(size);
    auto px_position = to_physx_vector(position);

    physx::PxBoxGeometry geometry(px_size * 0.5F);
    auto transform = physx::PxTransform(px_position);
    return create_static_actor(px_scene, transform, geometry);
}

guid_t physic_module_impl::create_dynamic_capsule_actor(
    const guid_t& scene_obj_id, const physic3_d& position, const float radius, const float height) {
    auto px_scene = find_scene(scene_obj_id);
    if (px_scene == nullptr) {
        ARK_LOG_ERROR("this scene does not exist");
        return null_guid;
    }

    physx::PxCapsuleGeometry geometry(radius, height * 0.5F);

    auto px_position = to_physx_vector(position);
    auto transform = physx::PxTransform(px_position);
    quat_roate(transform);
    return create_dynamic_actor(px_scene, transform, geometry);
}

guid_t physic_module_impl::create_static_capsule_actor(
    const guid_t& scene_obj_id, const physic3_d& position, const float radius, const float height) {
    auto px_scene = find_scene(scene_obj_id);
    if (px_scene == nullptr) {
        ARK_LOG_ERROR("this scene does not exist scene id={} index={}");
        return null_guid;
    }

    physx::PxCapsuleGeometry geometry(radius, height * 0.5F);

    auto px_position = to_physx_vector(position);
    auto transform = physx::PxTransform(px_position);
    quat_roate(transform);
    return create_static_actor(px_scene, transform, geometry);
}

guid_t physic_module_impl::create_dynamic_sphere_actor(
    const guid_t& scene_obj_id, const physic3_d& position, const float radius) {
    auto px_scene = find_scene(scene_obj_id);
    if (px_scene == nullptr) {
        ARK_LOG_ERROR("this scene does not exist");
        return null_guid;
    }

    physx::PxSphereGeometry geometry(radius);

    auto px_position = to_physx_vector(position);
    auto transform = physx::PxTransform(px_position);
    return create_dynamic_actor(px_scene, transform, geometry);
}

guid_t physic_module_impl::create_static_sphere_actor(
    const guid_t& scene_obj_id, const physic3_d& position, const float radius) {
    auto px_scene = find_scene(scene_obj_id);
    if (px_scene == nullptr) {
        ARK_LOG_ERROR("this scene does not exist");
        return null_guid;
    }

    physx::PxSphereGeometry geometry(radius);

    auto px_position = to_physx_vector(position);
    auto transform = physx::PxTransform(px_position);
    return create_static_actor(px_scene, transform, geometry);
}

guid_t physic_module_impl::create_dynamic_actor(
    physx::PxScene* px_scene, const physx::PxTransform& transform, const physx::PxGeometry& geometry) {
    SCENE_WRITE_LOCK(px_scene);

    auto actor = create_dynamic(transform, geometry, *material_);
    if (actor == nullptr) {
        ARK_LOG_ERROR("create actor failed");
        return null_guid;
    }

    px_scene->addActor(*actor);

    ///generate guid
    auto guid = guid_module_->create_guid();
    actor->userData = (void*)guid_t(guid);
    actor_list_.insert(std::make_pair(guid, actor));
    return guid;
}

guid_t physic_module_impl::create_static_actor(
    physx::PxScene* px_scene, const physx::PxTransform& transform, const physx::PxGeometry& geometry) {
    SCENE_WRITE_LOCK(px_scene);

    auto actor = create_static(transform, geometry, *material_);
    if (actor == nullptr) {
        ARK_LOG_ERROR("create actor failed");
        return null_guid;
    }

    px_scene->addActor(*actor);

    ///generate guid
    auto guid = guid_module_->create_guid();
    actor->userData = (void*)guid_t(guid);
    actor_list_.insert(std::make_pair(guid, actor));
    return guid;
}

bool physic_module_impl::update_actor(const guid_t& actor_obj_id, const physic3_d& position) {
    auto actor = find_actor(actor_obj_id);
    if (actor == nullptr) {
        ARK_LOG_ERROR("actor does not exist actor id = {}", actor_obj_id);
        return false;
    }

    auto rigid_dynamic = actor->is<physx::PxRigidDynamic>();
    if (rigid_dynamic == nullptr) {
        ARK_LOG_ERROR("actor can not be updated");
        return false;
    }

    auto px_scene = actor->getScene();
    if (!px_scene) {
        ARK_LOG_ERROR("scene is null");
        return false;
    }

    SCENE_WRITE_LOCK(px_scene);

    physx::PxTransform transform = rigid_dynamic->getGlobalPose();

    auto px_position = to_physx_vector(position);
    transform.p = px_position;
    if (config_.simulate) {
        rigid_dynamic->setKinematicTarget(transform);
    } else {
        rigid_dynamic->setGlobalPose(transform);
    }

    return true;
}

bool physic_module_impl::destroy_actor(const guid_t& actor_obj_id) {
    auto actor = find_actor(actor_obj_id);
    if (actor == nullptr) {
        ARK_LOG_ERROR("actor does not exist actor id = {}", actor_obj_id);
        return false;
    }

    auto px_scene = actor->getScene();
    if (!px_scene) {
        ARK_LOG_ERROR("scene is null");
        return false;
    }

    SCENE_WRITE_LOCK(px_scene);

    actor->release();

    actor_list_.erase(actor_obj_id);
    return true;
}

bool physic_module_impl::get_actor_position(const guid_t& actor_obj_id, physic3_d& position) {
    auto actor = find_actor(actor_obj_id);
    if (actor == nullptr) {
        ARK_LOG_ERROR("actor does not exist actor id = {}", actor_obj_id);
        return false;
    }

    auto rigid_actor = actor->is<physx::PxRigidActor>();
    if (rigid_actor == nullptr) {
        ARK_LOG_ERROR("actor does not exist actor id = {}", actor_obj_id);
        return false;
    }

    position = from_physx_vector(rigid_actor->getGlobalPose().p);
    return true;
}

//bool AFCPhysicModule::AddChild(physx::PxActor* parentActor, const physx::PxTransform& localParent,
//    physx::PxActor* childActor, const physx::PxTransform& localChild)
//{
//    if (!parentActor || !childActor)
//    {
//        ARK_LOG_ERROR("actor is null");
//        return false;
//    }
//
//    if (parentActor->getScene() != childActor->getScene())
//    {
//        ARK_LOG_ERROR("actors are not in the same scene");
//        return false;
//    }
//
//    auto rigidParent = parentActor->is<physx::PxRigidActor>();
//    auto rigidChild = childActor->is<physx::PxRigidActor>();
//    if (!rigidParent || !rigidChild)
//    {
//        ARK_LOG_ERROR("actor is not rigid actor");
//        return false;
//    }
//
//    auto numShapesChild = rigidChild->getNbShapes();
//    auto shapesChild = ARK_NEW_ARRAY(physx::PxShape*, numShapesChild);
//    for (physx::PxU32 i = 0; i < numShapesChild; i++)
//    {
//        //
//    }
//    ARK_DELETE_ARRAY(physx::PxShape*, shapesChild);
//
//    return true;
//}
//
//bool AFCPhysicModule::RemoveChild(
//    physx::PxActor* parentActor, physx::PxActor* childActor, const physx::PxTransform& childTransform)
//{
//    return false;
//}

physic3_d physic_module_impl::ray_get_point(const physic3_d& origin, const physic3_d& direction, const float distance) {
    auto px_origin = to_physx_vector(origin);
    auto px_direction = to_physx_vector(direction);

    auto normal_dir = px_direction;
    if (!px_direction.isNormalized()) {
        normal_dir = px_direction.getNormalized();
    }

    auto dest_point = px_origin + normal_dir * distance;
    return from_physx_vector(dest_point);
}

float physic_module_impl::get_height(const guid_t& scene_obj_id, const physic3_d& position) {
    float distance = 100.0F;

    ///firstly ray cast down
    auto px_position = to_physx_vector(position);
    physx::PxVec3 dir(0.0F, -1.0F, 0.0F);
    physx::PxRaycastBuffer ray_hit;
    auto result = ray_cast(scene_obj_id, px_position, dir, distance, ray_hit);
    if (result) {
        return ray_hit.block.position.y;
    }

    ///then ray cast up
    dir.y = -dir.y;
    result = ray_cast(scene_obj_id, px_position, dir, distance, ray_hit);
    if (result) {
        return ray_hit.block.position.y;
    }

    return position.y;
}

bool physic_module_impl::register_trigger_call_back(trigger_call_back_function&& cb) {
    simulation_event_callback_.register_trigger_call_back(std::forward<trigger_call_back_function>(cb));
    return true;
}

bool physic_module_impl::cook_scene_mesh(const physic_scene& scene) {
    physic_scene_cooked scene_cooked;

    ///cook terrain mesh
    auto terrain_mesh = create_triangle_mesh(scene);
    if (terrain_mesh == nullptr) {
        ARK_LOG_ERROR("mesh is null");
        return false;
    }

    scene_cooked.terrain_mesh = terrain_mesh;

    for (auto& iter_collider : scene.colliders) {
        auto collider = iter_collider;
        if (collider->is_mesh_collider()) {
            ///cook scene object mesh
            auto mesh_collider = dynamic_cast<mesh_collider*>(collider);
            if (!mesh_collider) {
                ARK_LOG_ERROR("mesh is null");
                return false;
            }

            if (mesh_collider->convex) {
                auto collider_mesh = create_convex_mesh(mesh_collider);
                if (!collider_mesh) {
                    ARK_LOG_ERROR("mesh is null");
                    return false;
                }

                scene_cooked.convex_colliders.insert(std::make_pair(collider_mesh, mesh_collider->position));
            } else {
                auto collider_mesh = create_triangle_mesh(mesh_collider);
                if (!collider_mesh) {
                    ARK_LOG_ERROR("mesh is null");
                    return false;
                }

                scene_cooked.triangle_colliders.insert(std::make_pair(collider_mesh, mesh_collider->position));
            }

            continue;
        }

        scene_cooked.colliders.push_back(collider);
    }

    scene_cooked.scene = &const_cast<physic_scene&>(scene);
    cooked_scene_list_.insert(std::make_pair(scene_cooked.scene->scene_id, scene_cooked));

    ARK_LOG_DEBUG("scene cooked scene id = {}", scene.scene_id);

    return true;
}

bool physic_module_impl::find_physic_scene(const std::string& scene_id, physic_scene_cooked& scene_cooked) {
    auto iter = cooked_scene_list_.find(scene_id);
    if (iter == cooked_scene_list_.end()) {
        return false;
    }

    scene_cooked = iter->second;
    return true;
}

bool physic_module_impl::init_physic() {
    default_allocator_callback_ = physx::PxDefaultAllocator();
    default_error_callback_ = physx::PxDefaultErrorCallback();
    physx::PxAllocatorCallback* allocator = &default_allocator_callback_;
    foundation_ = PxCreateFoundation(PX_FOUNDATION_VERSION, *allocator, default_error_callback_);
    ARK_ASSERT_RET_VAL(foundation_, false);

    ///create pvd for debug
    create_pvd_connection();

    physx::PxTolerancesScale scale;

#if PX_SUPPORT_PVD
    bool record_memory_allocations = true;
#else
    bool record_memory_allocations = false;
#endif

    ///create physx
    physics_ = PxCreateBasePhysics(PX_PHYSICS_VERSION, *foundation_, scale, record_memory_allocations, pvd_);
    ARK_ASSERT_RET_VAL(physics_, false);

    ///register particles
    PxRegisterParticles(*physics_);

    bool result = PxInitExtensions(*physics_, pvd_);
    ARK_ASSERT_RET_VAL(result, false);

    ///create cooking
    physx::PxCookingParams params(scale);
    params.meshWeldTolerance = 0.001f;
    params.meshPreprocessParams = physx::PxMeshPreprocessingFlags(physx::PxMeshPreprocessingFlag::eWELD_VERTICES);
    params.buildGPUData = false;
    cooking_ = PxCreateCooking(PX_PHYSICS_VERSION, *foundation_, params);
    ARK_ASSERT_RET_VAL(cooking_, false);

    ///create dispatcher
    cpu_dispatcher_ = physx::PxDefaultCpuDispatcherCreate(config_.dispatcher_threads);
    ARK_ASSERT_RET_VAL(cpu_dispatcher_, false);

    ///create default material
    material_ = physics_->createMaterial(0.5f, 0.5f, 0.5f);

    simulation_event_callback_ = simulation_event_callback();

    ///create plane
    plane_ = physx::PxPlane(0, 1, 0, 0);

    return true;
}

void physic_module_impl::create_pvd_connection() {
#if PX_SUPPORT_PVD
    pvd_transport_ =
        physx::PxDefaultPvdSocketTransportCreate(config_.pvd_host.c_str(), config_.pvd_port, config_.pvd_timeout);
    if (pvd_transport_ == nullptr) {
        ARK_LOG_ERROR("create pvd socket failed");
        return;
    }

    pvd_ = physx::PxCreatePvd(*foundation_);
    pvd_->connect(*pvd_transport_, physx::PxPvdInstrumentationFlag::eALL);
#endif
}

bool physic_module_impl::init_scene(physx::PxScene* px_scene, const physic_scene_cooked& scene_cooked) {
    auto& scene = *scene_cooked.scene;
    if (!scene_cooked.terrain_mesh) {
        ARK_LOG_ERROR("scene terrain mesh do not exist scene id = {}", scene.scene_id);
        return false;
    }

    ///create plane
    auto plane = physx::PxCreatePlane(*physics_, plane_, *material_);
    px_scene->addActor(*plane);

    ///create terrain
    auto terrain_geometry = physx::PxTriangleMeshGeometry(scene_cooked.terrain_mesh);
    auto terrain_transform = physx::PxTransform(physx::PxIdentity);
    terrain_transform.q = physx::PxQuat(physx::PxHalfPi, physx::PxVec3(0, 1, 0)); ///different from unity
    auto terrain_actor = create_static(terrain_transform, terrain_geometry, *material_);
    px_scene->addActor(*terrain_actor);

    ///create actors
    for (const auto& iter : scene_cooked.colliders) {
        const auto collider = iter;
        auto actor = create_static(collider);
        if (!actor) {
            ARK_LOG_ERROR("create actor failed scene id={} file={}", scene.scene_id, scene.mesh_file.path);
            continue;
        }

        if (collider->trigger) {
            physx::PxShape* shape = nullptr;
            actor->getShapes(&shape, 1);
            if (shape) {
                shape->setFlag(physx::PxShapeFlag::eSIMULATION_SHAPE, false);
                shape->setFlag(physx::PxShapeFlag::eTRIGGER_SHAPE, true);
            }
        }

        px_scene->addActor(*actor);
    }

    ///create triangle actors
    for (const auto& iter : scene_cooked.triangle_colliders) {
        auto mesh = iter.first;
        auto& position = iter.second;
        if (!mesh) {
            ARK_LOG_ERROR("create actor failed scene id={}", scene.scene_id);
            continue;
        }

        auto geometry = physx::PxTriangleMeshGeometry(mesh);
        auto actor = create_static(physx::PxTransform(position), geometry, *material_);
        if (!actor) {
            ARK_LOG_ERROR("create actor failed scene id={} file={}", scene.scene_id);
            continue;
        }

        px_scene->addActor(*actor);
    }

    ///create convex actors
    for (const auto& iter : scene_cooked.convex_colliders) {
        auto mesh = iter.first;
        auto& position = iter.second;
        if (!mesh) {
            ARK_LOG_ERROR("create actor failed scene id={}", scene.scene_id);
            continue;
        }

        auto geometry = physx::PxConvexMeshGeometry(mesh);
        auto actor = create_static(physx::PxTransform(position), geometry, *material_);
        if (!actor) {
            ARK_LOG_ERROR("create actor failed scene id={} file={}", scene.scene_id);
            continue;
        }

        px_scene->addActor(*actor);
    }

    return true;
}

physx::PxRigidStatic* physic_module_impl::create_static(collider* collider) {
    if (collider == nullptr) {
        ARK_LOG_ERROR("collider is null");
        return nullptr;
    }

    if (collider->is_box_collider()) {
        auto real_collider = dynamic_cast<box_collider*>(collider);
        if (real_collider == nullptr) {
            ARK_LOG_ERROR("collider to BoxCollider failed");
            return nullptr;
        }

        auto half = real_collider->size * 0.5F;
        physx::PxBoxGeometry geometry(half);
        auto transform = physx::PxTransform(real_collider->position);
        return create_static(transform, geometry, *material_);
    } else if (collider->is_capsule_collider()) {
        auto real_collider = dynamic_cast<capsule_collider*>(collider);
        if (real_collider == nullptr) {
            ARK_LOG_ERROR("collider to CapsuleCollider failed");
            return nullptr;
        }

        auto transform = physx::PxTransform(real_collider->position);
        quat_roate(transform);

        physx::PxCapsuleGeometry geometry(real_collider->radius, real_collider->height * 0.5F);
        return create_static(transform, geometry, *material_);
    } else if (collider->is_sphere_collider()) {
        auto real_collider = dynamic_cast<sphere_collider*>(collider);
        if (real_collider == nullptr) {
            ARK_LOG_ERROR("collider to SphereCollider failed");
            return nullptr;
        }

        physx::PxSphereGeometry geometry(real_collider->radius);
        auto transform = physx::PxTransform(real_collider->position);
        return create_static(transform, geometry, *material_);
    }

    return nullptr;
}

physx::PxRigidStatic* physic_module_impl::create_static(const physx::PxTransform& transform,
    const physx::PxGeometry& geometry, physx::PxMaterial& material, const physx::PxTransform& shape_offset) {
    if (!transform.isValid() || !shape_offset.isValid()) {
        ARK_LOG_ERROR("transform invalid");
        return nullptr;
    }

    auto shape = physics_->createShape(geometry, material, true);
    if (!shape) {
        ARK_LOG_ERROR("createa shape failed");
        return nullptr;
    }

    shape->setLocalPose(shape_offset);

    auto actor = physics_->createRigidStatic(transform);
    if (actor) {
        actor->attachShape(*shape);
    }

    shape->release();
    return actor;
}

physx::PxRigidDynamic* physic_module_impl::create_dynamic(const physx::PxTransform& transform,
    const physx::PxGeometry& geometry, physx::PxMaterial& material, float density,
    const physx::PxTransform& shape_offset) {
    if (!transform.isValid() || !shape_offset.isValid()) {
        ARK_LOG_ERROR("transform invalid");
        return nullptr;
    }

    auto shape = physics_->createShape(geometry, material, true);
    if (!shape) {
        ARK_LOG_ERROR("createa shape failed");
        return nullptr;
    }

    shape->setLocalPose(shape_offset);

    auto actor = physics_->createRigidDynamic(transform);
    if (actor) {
        actor->attachShape(*shape);
        physx::PxRigidBodyExt::updateMassAndInertia(*actor, density);

        if (config_.simulate) {
            actor->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, true);
        }
    }

    shape->release();
    return actor;
}

physx::PxTriangleMesh* physic_module_impl::create_triangle_mesh(const physic_mesh_file& mesh_file) {
    physx::PxTriangleMesh* mesh = nullptr;

    physx::PxDefaultFileInputData file(mesh_file.cooked_path.c_str());
    if (file.isValid()) {
        mesh = physics_->createTriangleMesh(file);
    }

    if (!mesh) {
        ///need cook
        wave_front_obj wfo;
        if (wfo.LoadObj(mesh_file.path.c_str(), false) == 0) {
            ARK_LOG_ERROR("load file failed file={}", mesh_file.path);
            return nullptr;
        }

        physx::PxTriangleMeshDesc mesh_desc;
        mesh_desc.points.count = wfo.mVertexCount;
        mesh_desc.triangles.count = wfo.mTriCount;
        mesh_desc.points.stride = sizeof(float) * 3;
        mesh_desc.triangles.stride = sizeof(int) * 3;
        mesh_desc.points.data = wfo.mVertices;
        mesh_desc.triangles.data = wfo.mIndices;

        file_output_stream out_file(mesh_file.cooked_path.c_str());
        if (!out_file.valid()) {
            ARK_LOG_ERROR("load file failed file={}", mesh_file.cooked_path);
            return nullptr;
        }

        bool result = cooking_->cookTriangleMesh(mesh_desc, out_file);
        if (result) {
            out_file.release();

            physx::PxDefaultFileInputData cooked_file(mesh_file.cooked_path.c_str());
            if (!cooked_file.isValid()) {
                ARK_LOG_ERROR("load file failed file={}", mesh_file.cooked_path);
                return nullptr;
            }

            mesh = physics_->createTriangleMesh(cooked_file);
        }
    }

    return mesh;
}

physx::PxTriangleMesh* physic_module_impl::create_triangle_mesh(const physic_scene& scene_config) {
    auto& mesh_file = scene_config.mesh_file;

    wave_front_obj wfo;
    if (wfo.LoadObj(mesh_file.path.c_str(), false) == 0) {
        ARK_LOG_ERROR("load file failed file={}", mesh_file.path);
        return nullptr;
    }

    physx::PxTriangleMeshDesc mesh_desc;
    mesh_desc.points.count = wfo.mVertexCount;
    mesh_desc.triangles.count = wfo.mTriCount;
    mesh_desc.points.stride = sizeof(float) * 3;
    mesh_desc.triangles.stride = sizeof(int) * 3;
    mesh_desc.points.data = wfo.mVertices;
    mesh_desc.triangles.data = wfo.mIndices;

    physx::PxTriangleMeshCookingResult::Enum result{physx::PxTriangleMeshCookingResult::eFAILURE};
    auto mesh = cooking_->createTriangleMesh(mesh_desc, physics_->getPhysicsInsertionCallback(), &result);
    if (result != physx::PxTriangleMeshCookingResult::eSUCCESS) {
        ARK_LOG_WARN("triangle mesh is too big scene ={}", mesh_file.path);
    }

    return mesh;
}

physx::PxTriangleMesh* physic_module_impl::create_triangle_mesh(mesh_collider* collider) {
    if (collider->vertex_count == 0 || !collider->vertices || collider->triangle_count == 0 || !collider->triangles) {
        ARK_LOG_ERROR("mesh collider do not have vertices");
        return nullptr;
    }

    physx::PxTriangleMeshDesc mesh_desc;
    mesh_desc.points.count = collider->vertex_count;
    mesh_desc.triangles.count = collider->triangle_count / 3;
    mesh_desc.points.stride = sizeof(float) * 3;
    mesh_desc.triangles.stride = sizeof(int) * 3;
    mesh_desc.points.data = collider->vertices;
    mesh_desc.triangles.data = collider->triangles;

    auto mesh = cooking_->createTriangleMesh(mesh_desc, physics_->getPhysicsInsertionCallback());
    return mesh;
}

physx::PxConvexMesh* physic_module_impl::create_convex_mesh(mesh_collider* collider) {
    if (collider->vertex_count == 0 || !collider->vertices) {
        ARK_LOG_ERROR("mesh collider do not have vertices");
        return nullptr;
    }

    physx::PxConvexMeshDesc mesh_desc;
    mesh_desc.points.count = collider->vertex_count;
    mesh_desc.points.stride = sizeof(float) * 3;
    mesh_desc.points.data = collider->vertices;
    mesh_desc.flags = physx::PxConvexFlag::eCOMPUTE_CONVEX;

    auto mesh = cooking_->createConvexMesh(mesh_desc, physics_->getPhysicsInsertionCallback());
    return mesh;
}

void physic_module_impl::scene_pvd_setting(physx::PxScene* px_scene) {
    auto pvd_client = px_scene->getScenePvdClient();
    if (pvd_client) {
        pvd_client->setScenePvdFlags(physx::PxPvdSceneFlag::eTRANSMIT_CONSTRAINTS |
                                     physx::PxPvdSceneFlag::eTRANSMIT_SCENEQUERIES |
                                     physx::PxPvdSceneFlag::eTRANSMIT_CONTACTS);

        pvd_client->setScenePvdFlag(physx::PxPvdSceneFlag::eTRANSMIT_CONSTRAINTS, true);

        pvd_client->setScenePvdFlag(physx::PxPvdSceneFlag::eTRANSMIT_SCENEQUERIES, true);
    }
}

void physic_module_impl::quat_roate(physx::PxTransform& transform) {
    auto plane_transform = physx::PxTransformFromPlaneEquation(plane_);
    transform.q = plane_transform.q;
}

void physic_module_impl::on_simulate(uint64_t timer_id, const guid_t& guid) {
    for (auto& iter : scene_list_) {
        auto px_scene = iter.second;
        if (!px_scene) {
            continue;
        }

        physx::PxSceneWriteLock write_lock_simulate(*px_scene);
        px_scene->simulate(config_.step);

        physx::PxSceneWriteLock write_lock_fetch(*px_scene);
        px_scene->fetchResults(true);
    }
}

physx::PxScene* physic_module_impl::find_scene(const guid_t& scene_obj_id) {
    auto iter = scene_list_.find(scene_obj_id);
    if (iter == scene_list_.end()) {
        return nullptr;
    }

    return iter->second;
}

physx::PxActor* physic_module_impl::find_actor(const guid_t& actor_obj_id) {
    auto iter = actor_list_.find(actor_obj_id);
    if (iter == actor_list_.end()) {
        return nullptr;
    }

    return iter->second;
}

physx::PxVec3 physic_module_impl::to_physx_vector(const physic3_d& vec) {
    return physx::PxVec3(vec.x, vec.y, vec.z);
}

physic3_d physic_module_impl::from_physx_vector(const physx::PxVec3& vec) {
    return physic3_d(vec.x, vec.y, vec.z);
}

} // namespace ark
