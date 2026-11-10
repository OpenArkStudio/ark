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

#include "ark/kernel/data_impl.hpp"
#include "ark/kernel/data_list_impl.hpp"
#include "ark/kernel/entity_impl.hpp"
#include "ark/kernel/map_module_impl.hpp"
#include "ark/kernel/node_impl.hpp"

namespace ark {

bool map_module_impl::init() {
    kernel_module_ = find_module<kernel_module>();

    return true;
}

bool map_module_impl::shut() {
    map_infos_.clear();
    return true;
}

std::shared_ptr<ark::map_info> map_module_impl::get_map_info(const int map_id) {
    return map_infos_.find_value(map_id);
}

bool map_module_impl::is_in_map_instance(const guid_t& self) {
    auto entity_ptr = kernel_module_->get_entity(self);
    if (entity_ptr != nullptr) {
        return (entity_ptr->get_int32(entity_meta_base_entity::map_inst_id()) < 0);
    } else {
        ARK_LOG_ERROR("Cannot find entity, id = {}", self);
        return false;
    }
}

bool map_module_impl::exist_map(const int map_id) {
    auto map_info_ptr = map_infos_.find_value(map_id);
    return (map_info_ptr != nullptr);
}

bool map_module_impl::switch_map(const guid_t& self, const int target_map, const int target_inst, const vector3d& pos,
    const float orient, const data_list& args) {
    auto entity_ptr = kernel_module_->get_entity(self);
    if (entity_ptr == nullptr) {
        ARK_LOG_ERROR("Cannot find entity, id = {}", self);
        return false;
    }

    int32_t old_map = entity_ptr->get_int32(entity_meta_base_entity::map_id());
    int32_t old_inst = entity_ptr->get_int32(entity_meta_base_entity::map_inst_id());

    std::shared_ptr<map_info> old_map_info_ptr = map_infos_.find_value(old_map);
    std::shared_ptr<map_info> new_map_info_ptr = map_infos_.find_value(target_map);

    if (nullptr == old_map_info_ptr) {
        ARK_LOG_ERROR("cannot find this map, id={}", old_map);
        return false;
    }

    if (nullptr == new_map_info_ptr) {
        ARK_LOG_ERROR("cannot tind this map, id = {}", target_map);
        return false;
    }

    if (!new_map_info_ptr->exist_instance(target_inst)) {
        ARK_LOG_ERROR("Cannot find this map instance, map={} inst={}", target_map, target_inst);
        return false;
    }

    old_map_info_ptr->remove_entity_from_instance(old_inst, self, true);

    //if (target_map != old_map)
    //{
    //    pEntity->SetInt32(AFEntityMetaBaseEntity::map_id(), 0);
    //    pEntity->SetInt32(AFEntityMetaBaseEntity::map_id(), target_map);
    //}

    //pEntity->SetInt32(AFEntityMetaBaseEntity::map_inst_id(), target_inst);

    //pEntity->SetDouble("X", pos.x);
    //pEntity->SetDouble("Y", pos.y);
    //pEntity->SetDouble("Z", pos.z);
    entity_ptr->switch_scene(target_map, target_inst, pos);

    return new_map_info_ptr->add_entity_to_instance(target_inst, self, true);
}

bool map_module_impl::create_map(const int map_id) {
    auto map_info_ptr = map_infos_.find_value(map_id);
    if (map_info_ptr != nullptr) {
        return false;
    }

    map_info_ptr = std::make_shared<map_info>();
    if (map_info_ptr == nullptr) {
        return false;
    }

    map_infos_.insert(map_id, map_info_ptr);

    // create group 0
    auto map_inst_ptr = std::make_shared<map_instance>(0);
    if (nullptr == map_inst_ptr) {
        return false;
    }

    map_info_ptr->add_instance(0, map_inst_ptr);
    ARK_LOG_INFO("Create scene success, map={} inst=0", map_id);
    return true;
}

bool map_module_impl::destroy_map(const int map_id) {
    return map_infos_.erase(map_id);
}

int map_module_impl::get_online_count() {
    int online_count = 0;
    for (auto iter : map_infos_) {
        auto map_info_ptr = iter.second;
        auto& all_instances = map_info_ptr->get_all_instance();
        for (auto it : all_instances) {
            online_count += it.second->player_entities.size();
        }
    }

    return online_count;
}

int map_module_impl::get_max_online_count() {
    // test count 5000
    // and it should be define in a config file
    return 10000;
}

int map_module_impl::get_map_online_count(const int map_id) {
    int online_count = 0;

    auto map_info_ptr = map_infos_.find_value(map_id);
    if (map_info_ptr == nullptr) {
        return online_count;
    }

    auto all_instances = map_info_ptr->get_all_instance();
    for (auto iter : all_instances) {
        online_count += iter.second->player_entities.size();
    }

    return online_count;
}

int map_module_impl::get_map_online_count(const int map_id, const int map_instance_id) {
    int online_count = 0;

    auto map_info_ptr = map_infos_.find_value(map_id);
    if (map_info_ptr == nullptr) {
        return online_count;
    }

    auto map_inst_ptr = map_info_ptr->get_instance(map_instance_id);
    if (map_inst_ptr == nullptr) {
        return online_count;
    }

    online_count = map_inst_ptr->player_entities.size();
    return online_count;
}

int map_module_impl::get_map_online_list(const int map_id, data_list& list) {
    auto map_info_ptr = map_infos_.find_value(map_id);
    if (map_info_ptr == nullptr) {
        return 0;
    }

    auto all_instances = map_info_ptr->get_all_instance();
    for (auto iter : all_instances) {
        auto map_instance_ptr = iter.second;
        for (auto it : map_instance_ptr->player_entities) {
            list.add_int64(it.first);
        }
    }

    return list.get_count();
}

int map_module_impl::create_map_instance(const int map_id) {
    auto map_info_ptr = map_infos_.find_value(map_id);
    if (map_info_ptr == nullptr) {
        return -1;
    }

    int new_inst_id = map_info_ptr->create_instance_id();

    if (map_info_ptr->exist_instance(new_inst_id)) {
        return -1;
    }

    auto map_instance_ptr = std::make_shared<map_instance>(new_inst_id);
    if (map_instance_ptr == nullptr) {
        return -1;
    }

    map_info_ptr->add_instance(new_inst_id, map_instance_ptr);
    return new_inst_id;
}

bool map_module_impl::release_map_instance(const int map_id, const int inst_id) {
    auto map_info_ptr = map_infos_.find_value(map_id);
    if (nullptr == map_info_ptr) {
        return false;
    }

    if (!map_info_ptr->exist_instance(inst_id)) {
        return false;
    }

    data_list_impl list_object;

    if (get_inst_entity_list(map_id, inst_id, list_object)) {
        for (size_t i = 0; i < list_object.get_count(); ++i) {
            guid_t ident = list_object.int64(i);

            if (!kernel_module_->destroy_entity(ident)) {
                ARK_LOG_ERROR("Destory entity failed, id  = {}, please check", ident);
            }
        }
    }

    map_info_ptr->remove_instance(inst_id);

    return true;
}

bool map_module_impl::exit_map_instance(const int map_id, const int inst_id) {
    auto map_info_ptr = map_infos_.find_value(map_id);
    if (map_info_ptr == nullptr) {
        return false;
    }

    auto map_instance_ptr = map_info_ptr->get_instance(inst_id);
    return (map_instance_ptr != nullptr);
}

bool map_module_impl::get_inst_entity_list(const int map_id, const int inst_id, data_list& list) {
    auto map_info_ptr = map_infos_.find_value(map_id);
    if (map_info_ptr == nullptr) {
        return false;
    }

    auto map_instance_ptr = map_info_ptr->get_instance(inst_id);
    if (map_instance_ptr == nullptr) {
        return false;
    }

    for (auto iter : map_instance_ptr->player_entities) {
        list.add_int64(iter.first);
    }

    for (auto iter : map_instance_ptr->other_entities) {
        list.add_int64(iter.first);
    }

    return true;
}

int map_module_impl::get_entity_by_data_node(
    const int map_id, const std::string& name, const data_list& value_args, data_list& list) {
    data_list_impl var_object_list;
    get_map_online_list(map_id, var_object_list);
    size_t entity_count = var_object_list.get_count();
    for (size_t i = 0; i < entity_count; ++i) {
        guid_t ident = var_object_list.int64(i);
        auto entity_ptr = kernel_module_->get_entity(ident);
        if (entity_ptr == nullptr) {
            continue;
        }

        data_impl data;
        if (!entity_ptr->get_node(name, data)) {
            continue;
        }

        if (value_args.equal(0, data)) {
            list.add_int64(ident);
        }
    }

    return list.get_count();
}

} // namespace ark
