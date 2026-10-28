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

#include "ark/kernel/class_call_back_manager.hpp"

namespace ark {

class_call_back_manager::node_sync_call_backs class_call_back_manager::node_sync_call_back_list_;
class_call_back_manager::table_sync_call_backs class_call_back_manager::table_sync_call_back_list_;
class_call_back_manager::container_sync_call_back class_call_back_manager::container_sync_call_backs_list_;
class_call_back_manager::data_delay_sync_call_backs class_call_back_manager::delay_sync_call_back_list_;
class_call_back_manager::delay_sync_data_list class_call_back_manager::delay_sync_data_list_;

bool class_call_back_manager::add_class_call_back(class_event_functor&& cb, const int32_t prio) {
    class_events_.insert(std::make_pair(prio, std::move(cb)));
    return true;
}

bool class_call_back_manager::add_data_call_back(
    const uint32_t index, data_node_event_functor&& cb, const int32_t prio) {
    auto iter = data_call_backs_list_.find(index);
    if (iter == data_call_backs_list_.end())
    {
        node_call_backs call_backs;
        call_backs.insert(std::make_pair(prio, std::move(cb)));
        data_call_backs_list_.insert(std::make_pair(index, call_backs));
    }
    else
    {
        iter->second.insert(std::make_pair(prio, std::move(cb)));
    }

    return true;
}

bool class_call_back_manager::add_table_call_back(
    const uint32_t index, data_table_event_functor&& cb, const int32_t prio) {
    auto iter = table_call_backs_list_.find(index);
    if (iter == table_call_backs_list_.end())
    {
        table_call_backs call_backs;

        call_backs.insert(std::make_pair(prio, std::move(cb)));
        table_call_backs_list_.insert(std::make_pair(index, call_backs));
    }
    else
    {
        iter->second.insert(std::make_pair(prio, std::move(cb)));
    }

    return true;
}

bool class_call_back_manager::add_container_call_back(
    const uint32_t index, container_event_functor&& cb, const int32_t prio) {
    auto iter = container_call_backs_list_.find(index);
    if (iter == container_call_backs_list_.end())
    {
        container_call_backs call_backs;

        call_backs.insert(std::make_pair(prio, std::move(cb)));
        container_call_backs_list_.insert(std::make_pair(index, call_backs));
    }
    else
    {
        iter->second.insert(std::make_pair(prio, std::move(cb)));
    }

    return true;
}

bool class_call_back_manager::add_leave_scene_event(scene_event_functor&& cb, const int32_t prio) {
    leave_scene_events_.insert(std::make_pair(prio, std::move(cb)));
    return true;
}

bool class_call_back_manager::add_enter_scene_event(scene_event_functor&& cb, const int32_t prio) {
    enter_scene_events_.insert(std::make_pair(prio, std::move(cb)));
    return true;
}

bool class_call_back_manager::add_move_event(move_event_functor&& cb, const int32_t prio) {
    move_events_.insert(std::make_pair(prio, std::move(cb)));
    return true;
}

bool class_call_back_manager::on_class_event(
    const guid_t& id, const std::string& class_name, const entity_event class_event, const data_list& value_list) {
    for (auto& iter : class_events_)
    {
        iter.second(id, class_name, class_event, value_list);
    }

    return true;
}

bool class_call_back_manager::on_node_call_back(
    const guid_t& self, node* node_ptr, const data& old_data, const data& new_data) {
    auto index = node_ptr->get_index();
    auto& name = node_ptr->get_name();

    // sync call back
    if (node_ptr->have_mask(data_mask::pf_real_time))
    {
        for (auto& iter : node_sync_call_back_list_)
        {
            auto mask_value = iter.first;
            if (!node_ptr->have_mask(mask_value))
            {
                continue;
            }

            iter.second(self, index, mask_value, new_data);
        }
    }
    else
    {
        update_delay_node_list(self, node_ptr);
    }

    // common call back
    auto iter_call_backs = data_call_backs_list_.find(index);
    if (iter_call_backs != data_call_backs_list_.end())
    {
        for (auto& iter : iter_call_backs->second)
        {
            iter.second(self, name, index, old_data, new_data);
        }
    }

    return true;
}

bool class_call_back_manager::on_table_call_back(const guid_t& self, const mask_type mask, node* node_ptr,
    const table_event_data& event_data, const data& old_data, const data& new_data) {
    // sync call back
    if (mask.test((size_t)data_mask::pf_real_time))
    {
        for (auto& iter : table_sync_call_back_list_)
        {
            auto mask_value = iter.first;
            if (!mask.test((size_t)mask_value))
            {
                continue;
            }

            iter.second(self, event_data, mask_value, new_data);
        }
    }
    else
    {
        update_delay_table_list(
            self, mask, event_data.table_index, event_data.row, (table_op_type)event_data.op_type, node_ptr);
    }

    // common call back
    auto iter_call_backs = table_call_backs_list_.find(event_data.table_index);
    if (iter_call_backs != table_call_backs_list_.end())
    {
        for (auto& iter : iter_call_backs->second)
        {
            iter.second(self, event_data, old_data, new_data);
        }
    }

    return true;
}

bool class_call_back_manager::on_container_call_back(const guid_t& self, const uint32_t index, const mask_type mask,
    const container_op_type op_type, uint32_t src_index, uint32_t dest_index,
    std::shared_ptr<entity> src_entity /* = nullptr*/) {
    // sync call back
    if (mask.test((size_t)data_mask::pf_real_time))
    {
        for (auto& iter : container_sync_call_backs_list_)
        {
            auto mask_value = iter.first;
            if (!mask.test((size_t)mask_value))
            {
                continue;
            }

            iter.second(self, index, mask_value, op_type, src_index, dest_index);
        }
    }
    else
    {
        update_delay_container_list(self, index, mask, op_type, src_index, dest_index, src_entity);
    }

    auto iter_call_backs = container_call_backs_list_.find(index);
    if (iter_call_backs != container_call_backs_list_.end())
    {
        for (auto& iter : iter_call_backs->second)
        {
            iter.second(self, index, op_type, src_index, dest_index);
        }
    }

    return true;
}

bool class_call_back_manager::on_leave_scene_event(const guid_t& self, const int map_id, const int map_inst_id) {
    for (auto& iter : leave_scene_events_)
    {
        iter.second(self, map_id, map_inst_id);
    }

    return true;
}

bool class_call_back_manager::on_enter_scene_event(const guid_t& self, const int map_id, const int map_inst_id) {
    for (auto& iter : enter_scene_events_)
    {
        iter.second(self, map_id, map_inst_id);
    }

    return true;
}

bool class_call_back_manager::on_move_event(const guid_t& self, const vector3d& old_pos, const vector3d& new_pos) {
    for (auto& iter : move_events_)
    {
        iter.second(self, old_pos, new_pos);
    }

    return true;
}

// sync call back
void class_call_back_manager::add_node_sync_call_back(const data_mask mask_value, node_sync_functor&& cb) {
    node_sync_call_back_list_.insert_or_assign(mask_value, std::move(cb));
}

void class_call_back_manager::add_table_sync_call_back(const data_mask mask_value, table_sync_functor&& cb) {
    table_sync_call_back_list_.insert_or_assign(mask_value, std::move(cb));
}

void class_call_back_manager::add_container_sync_call_back(const data_mask mask_value, container_sync_functor&& cb) {
    container_sync_call_backs_list_.insert_or_assign(mask_value, std::move(cb));
}

void class_call_back_manager::add_delay_sync_call_back(const data_mask mask_value, delay_sync_functor&& cb) {
    delay_sync_call_back_list_.insert_or_assign(mask_value, std::move(cb));
}

bool class_call_back_manager::on_delay_sync() {
    static bool flushing = false;
    if (flushing)
    {
        return false;
    }

    struct flush_guard
    {
        bool& active;

        ~flush_guard() {
            active = false;
        }
    };

    flushing = true;
    flush_guard guard{flushing};

    // Newly collected entries remain pending for the next flush, even on throw.
    // Borrowed node values are not frozen. The detached batch is not replayed.
    delay_sync_data_list batch;
    batch.swap(delay_sync_data_list_);
    for (auto& iter : batch)
    {
        auto& guid = iter.first;
        auto& mask_data_map = iter.second;
        for (auto& iter_data : mask_data_map)
        {
            auto mask_value = iter_data.first;
            auto iter_call_back = delay_sync_call_back_list_.find(mask_value);
            if (iter_call_back == delay_sync_call_back_list_.end())
            {
                continue;
            }

            iter_call_back->second(guid, mask_value, iter_data.second);
        }
    }

    return true;
}

class_call_back_manager::delay_sync_mask_data& class_call_back_manager::get_delay_sync_mask_data(const guid_t& self) {
    auto iter_data = delay_sync_data_list_.find(self);
    if (iter_data == delay_sync_data_list_.end())
    {
        delay_sync_mask_data mask_data_map;
        iter_data = delay_sync_data_list_.insert(std::make_pair(self, mask_data_map)).first;
    }

    return iter_data->second;
}

delay_sync_table& class_call_back_manager::get_delay_sync_mask_table(
    delay_sync_mask_data& mask_data_map, const data_mask mask_value, uint32_t index) {
    auto iter_mask_data = mask_data_map.find(mask_value);
    if (iter_mask_data == mask_data_map.end())
    {
        delay_sync_data data;
        iter_mask_data = mask_data_map.insert(std::make_pair(mask_value, data)).first;
    }

    auto& table_list = iter_mask_data->second.table_list;
    auto iter_table = table_list.find(index);
    if (iter_table == table_list.end())
    {
        delay_sync_table table;
        iter_table = table_list.insert(std::make_pair(index, table)).first;
    }

    return iter_table->second;
}

delay_sync_container& class_call_back_manager::get_delay_sync_mask_container(
    delay_sync_mask_data& mask_data_map, const data_mask mask_value, uint32_t index) {
    auto iter_mask_data = mask_data_map.find(mask_value);
    if (iter_mask_data == mask_data_map.end())
    {
        delay_sync_data data;
        iter_mask_data = mask_data_map.insert(std::make_pair(mask_value, data)).first;
    }

    auto& contaier_list = iter_mask_data->second.container_list;
    auto iter_container = contaier_list.find(index);
    if (iter_container == contaier_list.end())
    {
        delay_sync_container container;
        iter_container = contaier_list.insert(std::make_pair(index, container)).first;
    }

    return iter_container->second;
}

void class_call_back_manager::update_delay_node_list(const guid_t& self, node* node_ptr) {
    // update node list
    auto& mask_data_map = get_delay_sync_mask_data(self);
    for (auto& iter : delay_sync_call_back_list_)
    {
        auto mask_value = iter.first;
        if (!node_ptr->have_mask(mask_value))
        {
            continue;
        }

        auto iter_mask_data = mask_data_map.find(mask_value);
        if (iter_mask_data == mask_data_map.end())
        {
            delay_sync_data data;
            data.node_list.insert(node_ptr);
            mask_data_map.insert(std::make_pair(mask_value, data));
        }
        else
        {
            iter_mask_data->second.node_list.insert(node_ptr);
        }
    }
}

void class_call_back_manager::update_delay_table_list(const guid_t& self, const mask_type mask,
    const uint32_t table_index, const uint32_t row_index, table_op_type op_type, node* node_ptr) {
    auto& mask_data_map = get_delay_sync_mask_data(self);
    for (auto& iter : delay_sync_call_back_list_)
    {
        auto mask_value = iter.first;
        if (!mask.test((size_t)mask_value))
        {
            continue;
        }

        auto& table = get_delay_sync_mask_table(mask_data_map, mask_value, table_index);
        if (op_type == table_op_type::table_add)
        {
            on_table_add(table, row_index);
        }
        else if (op_type == table_op_type::table_delete)
        {
            on_table_delete(table, row_index);
        }
        else if (op_type == table_op_type::table_update)
        {
            on_table_row_update(table, row_index, node_ptr);
        }
        else if (op_type == table_op_type::table_swap)
        {
            // todo : for now there is no operation swap
        }
        else if (op_type == table_op_type::table_clear)
        {
            on_table_clear(table);
        }
    }
}

void class_call_back_manager::update_delay_container_list(const guid_t& self, const uint32_t index,
    const mask_type mask, const container_op_type op_type, uint32_t src_index, uint32_t dest_index,
    std::shared_ptr<entity> src_entity = nullptr) {
    // update container list
    auto& mask_data_map = get_delay_sync_mask_data(self);
    for (auto& iter : delay_sync_call_back_list_)
    {
        auto mask_value = iter.first;
        if (!mask.test((size_t)mask_value))
        {
            continue;
        }

        auto& container = get_delay_sync_mask_container(mask_data_map, mask_value, index);
        if (op_type == container_op_type::op_place)
        {
            on_container_place(container, src_index);
        }
        else if (op_type == container_op_type::op_remove)
        {
            on_container_remove(container, src_index);
        }
        else if (op_type == container_op_type::op_destroy)
        {
            on_container_destroy(container, src_index, src_entity);
        }
        else if (op_type == container_op_type::op_swap)
        {
            on_container_swap(container, src_index, dest_index);
        }
    }
}

void class_call_back_manager::on_table_add(delay_sync_table& table, const int row) {
    auto iter = table.row_list.find(row);
    if (iter == table.row_list.end())
    {
        delay_sync_row row_data(row);
        table.row_list.insert(std::make_pair(row, row_data));
    }
    else
    {
        iter->second.node_list.clear();
    }
}

void class_call_back_manager::on_table_delete(delay_sync_table& table, const int row) {
    auto iter = table.row_list.find(row);
    if (iter == table.row_list.end())
    {
        delay_sync_row row_data(row);
        row_data.need_clear = true;

        table.row_list.insert(std::make_pair(row, row_data));
    }
    else
    {
        iter->second.need_clear = true;
        iter->second.node_list.clear();
    }
}

void class_call_back_manager::on_table_row_update(delay_sync_table& table, const int row, node* node_ptr) {
    auto iter = table.row_list.find(row);
    if (iter == table.row_list.end())
    {
        delay_sync_row row_data(row);
        row_data.node_list.insert(node_ptr);
        table.row_list.insert(std::make_pair(row, row_data));
    }
    else
    {
        iter->second.node_list.insert(node_ptr);
    }
}

void class_call_back_manager::on_table_clear(delay_sync_table& table) {
    table.need_clear = true;
    table.row_list.clear();
}

void class_call_back_manager::on_container_place(delay_sync_container& container, uint32_t index) {
    container.index_list.insert(index);
}

void class_call_back_manager::on_container_remove(delay_sync_container& container, uint32_t index) {
    container.index_list.insert(index);
}

void class_call_back_manager::on_container_destroy(
    delay_sync_container& container, uint32_t index, std::shared_ptr<entity> src_entity) {
    if (src_entity->is_sent())
    {
        container.destroy_list.insert(index);
    }

    container.index_list.insert(index);
}

void class_call_back_manager::on_container_swap(
    delay_sync_container& container, uint32_t src_index, uint32_t dest_index) {
    container.index_list.insert(src_index);
    container.index_list.insert(dest_index);
}

} // namespace ark
