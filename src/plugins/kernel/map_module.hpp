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

#include "ark/container/list.hpp"
#include "ark/container/map.hpp"
#include "ark/kernel/data_list.hpp"
#include "ark/math/vector3d.hpp"
#include "ark/module.hpp"

namespace ark {

// Map instance
class map_instance {
public:
    explicit map_instance(int inst_id)
        : inst_id(inst_id) {
    }

    shared_object_map<guid_t, bool> player_entities;
    shared_object_map<guid_t, bool> other_entities;
    int inst_id;
};

// All instance in this map
class map_info {
public:
    map_info() = default;

    map_info(int width)
        : width_(width) {
    }

    virtual ~map_info() = default;

    int create_instance_id() {
        return ++instance_id_;
    }

    int get_width() {
        return width_;
    }

    std::shared_ptr<map_instance> get_instance(int inst_id) {
        return map_instances_.find_value(inst_id);
    }

    bool exist_instance(int inst_id) {
        return (map_instances_.find_value(inst_id) != nullptr);
    }

    shared_object_map<int, map_instance>& get_all_instance() {
        return map_instances_;
    }

    bool add_instance(int inst_id, std::shared_ptr<map_instance> inst) {
        return map_instances_.insert(inst_id, inst).second;
    }

    bool remove_instance(int inst_id) {
        map_instances_.erase(inst_id);
        return true;
    }

    bool add_entity_to_instance(const int inst_id, const guid_t& entity_id, bool is_player) {
        std::shared_ptr<map_instance> info_ptr = map_instances_.find_value(inst_id);
        if (info_ptr == nullptr) {
            return false;
        } else {
            if (is_player) {
                return info_ptr->player_entities.insert(entity_id, nullptr).second; // TODO:Map.second mean nothing
            } else {
                return info_ptr->other_entities.insert(entity_id, nullptr).second; // TODO:Map.second mean nothing
            }
        }
    }

    bool remove_entity_from_instance(const int inst_id, const guid_t& entity_id, bool is_player) {
        std::shared_ptr<map_instance> info_ptr = map_instances_.find_value(inst_id);
        if (nullptr == info_ptr) {
            return false;
        } else {
            if (is_player) {
                return info_ptr->player_entities.erase(entity_id);
            } else {
                return info_ptr->other_entities.erase(entity_id);
            }
        }
    }

private:
    int instance_id_{0};
    //int map_id_{0};
    int width_{512}; // will separate the grid
    shared_object_map<int, map_instance> map_instances_;
};

class map_module : public module {
public:
    virtual std::shared_ptr<map_info> get_map_info(const int map_id) = 0;

    virtual bool is_in_map_instance(const guid_t& self) = 0;
    virtual bool exist_map(const int map_id) = 0;

    virtual bool switch_map(const guid_t& self, const int target_map, const int target_inst, const vector3d& pos,
        const float orient, const data_list& args) = 0;

    virtual bool create_map(const int map_id) = 0;
    virtual bool destroy_map(const int map_id) = 0;

    virtual int get_online_count() = 0;
    virtual int get_max_online_count() = 0;
    virtual int get_map_online_count(const int map_id) = 0;
    virtual int get_map_online_count(const int map_id, const int inst_id) = 0;
    virtual int get_map_online_list(const int map_id, data_list& args) = 0;

    virtual int create_map_instance(const int map_id) = 0;
    virtual bool release_map_instance(const int map_id, const int inst_id) = 0;
    virtual bool exit_map_instance(const int map_id, const int inst_id) = 0;

    virtual bool get_inst_entity_list(const int map_id, const int inst_id, data_list& list) = 0;
    virtual int get_entity_by_data_node(
        const int map_id, const std::string& data_node, const data_list& args, data_list& entities) = 0;
};

} // namespace ark
