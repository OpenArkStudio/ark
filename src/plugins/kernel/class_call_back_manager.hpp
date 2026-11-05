/*
 * This source file is part of ArkNX
 * For the latest info, see https://github.com/ArkNX
 *
 * Copyright (c) 2013-2020 ArkNX authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
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

#include <set>

#include "ark/container/list.hpp"
#include "ark/container/map.hpp"
#include "ark/define.hpp"
#include "ark/kernel/data_impl.hpp"
#include "ark/kernel/entity.hpp"
#include "ark/macros.hpp"

namespace ark {

struct delay_sync_row
{
    delay_sync_row(const uint32_t row)
        : row(row) {
    }

    bool need_clear{false};
    uint32_t row{0};
    std::set<node*> node_list;
};

struct delay_sync_table
{
    bool need_clear{false};
    std::map<uint32_t, delay_sync_row> row_list;
};

struct delay_sync_container
{
    std::set<uint32_t> index_list;
    std::set<uint32_t> destroy_list;
};

struct delay_sync_data
{
    // node list
    std::set<node*> node_list;

    // table list
    std::map<uint32_t, delay_sync_table> table_list;

    // container list
    std::map<uint32_t, delay_sync_container> container_list;
};

class class_call_back_manager final
{
public:
    // delay sync data
    using delay_sync_mask_data = std::map<data_mask, delay_sync_data>;
    using delay_sync_data_list = std::map<guid_t, delay_sync_mask_data>;

    // call back functor
    using node_sync_functor = std::function<int(const guid_t&, const uint32_t, const data_mask, const data&)>;
    using table_sync_functor = std::function<int(const guid_t&, const table_event_data&, const data_mask, const data&)>;
    using container_sync_functor =
        std::function<int(const guid_t&, const uint32_t, const data_mask, const container_op_type, uint32_t, uint32_t)>;

    using delay_sync_functor = std::function<int(const guid_t&, const data_mask, const delay_sync_data& data)>;

private:
    // class event list
    std::multimap<int32_t, class_event_functor> class_events_;

    // data call backs list
    using node_call_backs = std::multimap<int32_t, data_node_event_functor>;
    std::map<uint32_t, node_call_backs> data_call_backs_list_;

    // table call backs list
    using table_call_backs = std::multimap<int32_t, data_table_event_functor>;
    std::map<uint32_t, table_call_backs> table_call_backs_list_;

    // container call backs list
    using container_call_backs = std::multimap<int32_t, container_event_functor>;
    std::map<uint32_t, container_call_backs> container_call_backs_list_;

    // scene event
    using scene_events = std::multimap<int32_t, scene_event_functor>;
    scene_events leave_scene_events_;
    scene_events enter_scene_events_;

    // move event
    using move_events = std::multimap<int32_t, move_event_functor>;
    move_events move_events_;

    // sync call back
    using node_sync_call_backs = std::map<data_mask, node_sync_functor>;
    static node_sync_call_backs node_sync_call_back_list_;

    using table_sync_call_backs = std::map<data_mask, table_sync_functor>;
    static table_sync_call_backs table_sync_call_back_list_;

    using container_sync_call_back = std::map<data_mask, container_sync_functor>;
    static container_sync_call_back container_sync_call_backs_list_;

    // delay sync call back
    using data_delay_sync_call_backs = std::map<data_mask, delay_sync_functor>;
    static data_delay_sync_call_backs delay_sync_call_back_list_;

    // delay data
    static delay_sync_data_list delay_sync_data_list_;

public:
    // add call back
    bool add_class_call_back(class_event_functor&& cb, const int32_t prio);
    bool add_data_call_back(const uint32_t index, data_node_event_functor&& cb, const int32_t prio);
    bool add_table_call_back(const uint32_t index, data_table_event_functor&& cb, const int32_t prio);
    bool add_container_call_back(const uint32_t index, container_event_functor&& cb, const int32_t prio);
    bool add_leave_scene_event(scene_event_functor&& cb, const int32_t prio);
    bool add_enter_scene_event(scene_event_functor&& cb, const int32_t prio);
    bool add_move_event(move_event_functor&& cb, const int32_t prio);

    // data call back
    bool on_class_event(
        const guid_t& id, const std::string& class_name, const entity_event class_event, const data_list& value_list);
    bool on_node_call_back(const guid_t& self, node* node_ptr, const data& old_data, const data& new_data);
    bool on_table_call_back(const guid_t& self, const mask_type mask, node* node_ptr,
        const table_event_data& event_data, const data& old_data, const data& new_data);
    bool on_container_call_back(const guid_t& self, const uint32_t index, const mask_type mask,
        const container_op_type op_type, uint32_t src_index, uint32_t dest_index,
        std::shared_ptr<entity> src_entity = nullptr);

    // scene call back
    bool on_leave_scene_event(const guid_t& self, const int map_id, const int map_inst_id);
    bool on_enter_scene_event(const guid_t& self, const int map_id, const int map_inst_id);

    // move call back
    bool on_move_event(const guid_t& self, const vector3d& old_pos, const vector3d& new_pos);

    // data sync call back
    static void add_node_sync_call_back(const data_mask mask_value, node_sync_functor&& cb);
    static void add_table_sync_call_back(const data_mask mask_value, table_sync_functor&& cb);
    static void add_container_sync_call_back(const data_mask mask_value, container_sync_functor&& cb);
    static void add_delay_sync_call_back(const data_mask mask_value, delay_sync_functor&& cb);

    // delay sync
    static bool on_delay_sync();

private:
    friend class kernel_module_impl;
    static void retire_delay_sync(const guid_t& self);

    delay_sync_mask_data& get_delay_sync_mask_data(const guid_t& self);
    delay_sync_table& get_delay_sync_mask_table(
        delay_sync_mask_data& mask_data_map, const data_mask mask_value, uint32_t index);
    delay_sync_container& get_delay_sync_mask_container(
        delay_sync_mask_data& mask_data_map, const data_mask mask_value, uint32_t index);

    void update_delay_node_list(const guid_t& self, node* node_ptr);
    void update_delay_table_list(const guid_t& self, const mask_type mask, const uint32_t table_index,
        const uint32_t row_index, table_op_type op_type, node* node_ptr);
    void update_delay_container_list(const guid_t& self, const uint32_t index, const mask_type mask,
        const container_op_type op_type, uint32_t src_index, uint32_t dest_index, std::shared_ptr<entity> src_entity);

    void on_table_add(delay_sync_table& table, const int row);
    void on_table_delete(delay_sync_table& table, const int row);
    void on_table_row_update(delay_sync_table& table, const int row, node* node_ptr);
    void on_table_clear(delay_sync_table& table);

    void on_container_place(delay_sync_container& container, uint32_t index);
    void on_container_remove(delay_sync_container& container, uint32_t index);
    void on_container_destroy(delay_sync_container& container, uint32_t index, std::shared_ptr<entity> src_entity);
    void on_container_swap(delay_sync_container& container, uint32_t src_index, uint32_t dest_index);
};

} // namespace ark
