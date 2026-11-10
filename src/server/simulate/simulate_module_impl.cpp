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

#include "ark/plugin_manager.hpp"
#include "simulate_module_factory.hpp"
#include "simulate_module_impl.hpp"

namespace ark {

std::unique_ptr<simulate_module> detail::make_simulate_module() {
    return std::make_unique<simulate_module_impl>();
}

//method realization

#define GET_AND_CHECK_OBJ_EXIST(scene_id, index)                                                                       \
    auto simulate_scene = find_scene_obj_actors(scene_id, index);                                                      \
    if (!simulate_scene) {                                                                                             \
        ARK_LOG_ERROR("scene has not been created scene id = {} index = {} obj id = {}", scene_id, index, obj_id);     \
        return false;                                                                                                  \
    }                                                                                                                  \
                                                                                                                       \
    if (simulate_scene->exist(obj_id)) {                                                                               \
        ARK_LOG_ERROR("has created this obj scene id = {} index = {} obj id = {}", scene_id, index, obj_id);           \
        return false;                                                                                                  \
    }

bool simulate_module_impl::init() {
    physic_module_ = find_module<physic_module>();

    physic_module_->register_trigger_call_back(this, &simulate_module_impl::trigger_call_back);

    return true;
}

bool simulate_module_impl::create_scene(const std::string& scene_id, const int32_t index) {
    auto simulate_scene = find_scene_obj_actors(scene_id, index);
    if (simulate_scene) {
        ARK_LOG_ERROR("has created this scene id = {} index = {}", scene_id, index);
        return false;
    }

    auto scene_obj_id = physic_module_->create_scene(scene_id);
    if (scene_obj_id == null_guid) {
        ARK_LOG_ERROR("create scene failed scene id = {} index = {}", scene_id, index);
        return false;
    }

    insert_scene_obj_actors(scene_obj_id, scene_id, index);
    return true;
}

bool simulate_module_impl::destroy_scene(const std::string& scene_id, const int32_t index) {
    auto simulate_scene = find_scene_obj_actors(scene_id, index);
    if (!simulate_scene) {
        ARK_LOG_ERROR("does not have this scene id = {} index = {}", scene_id, index);
        return false;
    }

    if (!physic_module_->destroy_scene(simulate_scene->get_scene_obj_id())) {
        ARK_LOG_ERROR("destroy scene failed scene id = {} index = {}", scene_id, index);
        return false;
    }

    remove_scene_obj_actors(scene_id, index);
    return true;
}

bool simulate_module_impl::create_box(const std::string& scene_id, const int32_t index, const simulate_net_id obj_id,
    const physic3_d& position, const physic3_d& size, bool dynamic) {
    GET_AND_CHECK_OBJ_EXIST(scene_id, index);

    guid_t actor_obj_id;
    if (dynamic) {
        actor_obj_id = physic_module_->create_dynamic_box_actor(simulate_scene->get_scene_obj_id(), position, size);
    } else {
        actor_obj_id = physic_module_->create_static_box_actor(simulate_scene->get_scene_obj_id(), position, size);
    }

    if (actor_obj_id == null_guid) {
        ARK_LOG_ERROR("create actor failed scene id = {} index = {} obj id = {}", scene_id, index, obj_id);
        return false;
    }

    simulate_scene->insert_obj_actor(obj_id, actor_obj_id);
    return true;
}

bool simulate_module_impl::create_capsule(const std::string& scene_id, const int32_t index,
    const simulate_net_id obj_id, const physic3_d& position, const float raius, const float height, bool dynamic) {
    GET_AND_CHECK_OBJ_EXIST(scene_id, index);

    guid_t actor_obj_id;
    if (dynamic) {
        actor_obj_id =
            physic_module_->create_dynamic_capsule_actor(simulate_scene->get_scene_obj_id(), position, raius, height);
    } else {
        actor_obj_id =
            physic_module_->create_static_capsule_actor(simulate_scene->get_scene_obj_id(), position, raius, height);
    }

    if (actor_obj_id == null_guid) {
        ARK_LOG_ERROR("create actor failed scene id = {} index = {} obj id = {}", scene_id, index, obj_id);
        return false;
    }

    simulate_scene->insert_obj_actor(obj_id, actor_obj_id);
    return true;
}

bool simulate_module_impl::create_sphere(const std::string& scene_id, const int32_t index, const simulate_net_id obj_id,
    const physic3_d& position, const float raius, bool dynamic) {
    GET_AND_CHECK_OBJ_EXIST(scene_id, index);

    guid_t actor_obj_id;
    if (dynamic) {
        actor_obj_id = physic_module_->create_dynamic_sphere_actor(simulate_scene->get_scene_obj_id(), position, raius);
    } else {
        actor_obj_id = physic_module_->create_static_sphere_actor(simulate_scene->get_scene_obj_id(), position, raius);
    }

    if (actor_obj_id == null_guid) {
        ARK_LOG_ERROR("create actor failed scene id = {} index = {} obj id = {}", scene_id, index, obj_id);
        return false;
    }

    simulate_scene->insert_obj_actor(obj_id, actor_obj_id);
    return true;
}

bool simulate_module_impl::update_actor(
    const std::string& scene_id, const int32_t index, const simulate_net_id obj_id, const physic3_d& position) {
    auto simulate_scene = find_scene_obj_actors(scene_id, index);
    if (!simulate_scene) {
        ARK_LOG_ERROR("scene has not been created scene id = {} index = {} obj id = {}", scene_id, index, obj_id);
        return false;
    }

    auto actor_obj_id = simulate_scene->find_actor(obj_id);
    if (actor_obj_id == null_guid) {
        ARK_LOG_ERROR("does not have this obj scene id = {} index = {} obj id = {}", scene_id, index, obj_id);
        return false;
    }

    if (!physic_module_->update_actor(actor_obj_id, position)) {
        ARK_LOG_ERROR("update acotr failed scene id = {} index = {} obj id = {}", scene_id, index, obj_id);
        return false;
    }

    return true;
}

bool simulate_module_impl::destroy_actor(
    const std::string& scene_id, const int32_t index, const simulate_net_id obj_id) {
    auto simulate_scene = find_scene_obj_actors(scene_id, index);
    if (!simulate_scene) {
        ARK_LOG_ERROR("scene has not been created scene id = {} index = {} obj id = {}", scene_id, index, obj_id);
        return false;
    }

    auto actor_obj_id = simulate_scene->find_actor(obj_id);
    if (actor_obj_id == null_guid) {
        ARK_LOG_ERROR("does not have this obj scene id = {} index = {} obj id = {}", scene_id, index, obj_id);
        return false;
    }

    if (!physic_module_->destroy_actor(actor_obj_id)) {
        ARK_LOG_ERROR("destroy acotr failed scene id = {} index = {} obj id = {}", scene_id, index, obj_id);
        return false;
    }

    simulate_scene->remove_obj(obj_id);
    return true;
}

bool simulate_module_impl::ray_cast(const std::string& scene_id, const int32_t index, const physic3_d& start,
    const physic3_d& direction, const float distance, simulate::ack_ray_cast& ack_ray_cast) {
    auto simulate_scene = find_scene_obj_actors(scene_id, index);
    if (!simulate_scene) {
        ARK_LOG_ERROR("has created this scene id = {} index = {}", scene_id, index);
        return false;
    }

    physic_ray_hit ray_hit;
    if (!physic_module_->ray_cast(simulate_scene->get_scene_obj_id(), start, direction, distance, ray_hit)) {
        ARK_LOG_ERROR("ray cast failed scene id = {} index = {}", scene_id, index);
        return false;
    }

    ack_ray_cast.scene_id = scene_id;
    ack_ray_cast.index = index;
    ack_ray_cast.is_hit = ray_hit.is_hit;

    auto actor_obj_id = ray_hit.hit_obj_id;
    auto obj_id = simulate_scene->find_obj(actor_obj_id);
    ack_ray_cast.hit_obj_id = obj_id;
    ack_ray_cast.hit_pos = to_simulation_vector(ray_hit.hit_pos);

    return true;
}

bool simulate_module_impl::register_trigger_call_back(simulate_trigger_cb_function&& cb) {
    trigger_call_back_function_ = cb;
    return true;
}

void simulate_module_impl::insert_scene_obj_actors(
    const guid_t& scene_obj_id, const std::string& scene_id, const int32_t index) {
    auto iter = scene_index_scene_list_.find(scene_id);
    if (iter == scene_index_scene_list_.end()) {
        index_scene_map obj_actors_list;
        iter = scene_index_scene_list_.insert(std::make_pair(scene_id, obj_actors_list)).first;
        if (iter == scene_index_scene_list_.end()) {
            return;
        }
    }

    auto& index_obj_list = iter->second;
    auto iter_index = index_obj_list.find(index);
    if (iter_index != index_obj_list.end()) {
        return;
    }

    simulate_scene simulate_scene(scene_obj_id);
    index_obj_list.insert(std::make_pair(index, simulate_scene));

    scene_index_pair_list_.insert(std::make_pair(scene_obj_id, scene_index_pair(scene_id, index)));
}

void simulate_module_impl::remove_scene_obj_actors(const std::string& scene_id, const int32_t index) {
    auto iter = scene_index_scene_list_.find(scene_id);
    if (iter == scene_index_scene_list_.end()) {
        return;
    }

    auto& index_obj_list = iter->second;
    auto iter_index = index_obj_list.find(index);
    if (iter_index == index_obj_list.end()) {
        return;
    }

    auto scene_obj_id = iter_index->second.get_scene_obj_id();
    scene_index_pair_list_.erase(scene_obj_id);

    index_obj_list.erase(iter_index);
}

simulate_scene* simulate_module_impl::find_scene_obj_actors(const std::string& scene_id, const int32_t index) {
    auto iter = scene_index_scene_list_.find(scene_id);
    if (iter == scene_index_scene_list_.end()) {
        return nullptr;
    }

    auto& index_obj_list = iter->second;
    auto iter_index = index_obj_list.find(index);
    if (iter_index == index_obj_list.end()) {
        return nullptr;
    }

    return &iter_index->second;
}

simulate::simulate_vec3 simulate_module_impl::to_simulation_vector(const physic3_d& vec3) {
    return simulate::simulate_vec3(vec3.x, vec3.y, vec3.z);
}

void simulate_module_impl::trigger_call_back(const guid_t& scene_obj_id, const physic_trigger_pair& trigger_pair) {
    auto iter = scene_index_pair_list_.find(scene_obj_id);
    if (iter == scene_index_pair_list_.end()) {
        ARK_LOG_ERROR("does not find scene");
        return;
    }

    auto& scene_pair = iter->second;
    auto& scene_id = scene_pair.first;
    auto index = scene_pair.second;

    auto simulate_scene = find_scene_obj_actors(scene_id, index);
    if (!simulate_scene) {
        ARK_LOG_ERROR("does not find scene");
        return;
    }

    if (trigger_call_back_function_) {
        simulate::trigger_pair trigger_pair;
        trigger_pair.trigger_obj_id = simulate_scene->find_obj(trigger_pair.trigger_obj_id);
        trigger_pair.other_obj_id = simulate_scene->find_obj(trigger_pair.other_obj_id);

        trigger_call_back_function_(scene_id, index, trigger_pair);
    }
}

} // namespace ark
