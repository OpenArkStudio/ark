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

#include "ark/container/list.hpp"
#include "ark/container/map.hpp"
#include "ark/define.hpp"
#include "ark/kernel/class_call_back_manager.hpp"
#include "ark/kernel/container_manager.hpp"
#include "ark/kernel/container_manager_impl.hpp"
#include "ark/kernel/data.hpp"
#include "ark/kernel/data_list.hpp"
#include "ark/kernel/entity.hpp"
#include "ark/kernel/event_manager.hpp"
#include "ark/kernel/node.hpp"
#include "ark/kernel/node_manager.hpp"
#include "ark/kernel/table_manager.hpp"

namespace ark {

namespace entity_sync {
class snapshot_access;
}

class entity_opt_charactor
{
public:
    entity_opt_charactor(const int32_t map_id, const int32_t map_entity_id)
        : map_id(map_id)
        , map_entity_id(map_entity_id) {
        containers = std::make_shared<container_manager_impl>();
    }

    // map id
    int32_t map_id{null_int};

    // map obj id
    int32_t map_entity_id{null_int};

    // pos
    vector3d pos{null_vector3d};

    // orient
    float orient{null_float};

    // container manager
    std::shared_ptr<container_manager> containers{nullptr};
};

class entity_impl final : public entity
{
private:
    friend class kernel_module_impl;
    friend class entity_sync::snapshot_access;

    // object unique id
    guid_t guid_{null_guid};

    // config id
    id_type config_id_{0};

    // parent container
    std::shared_ptr<container> parent_container_{nullptr};

    // is sent to client(for container use)
    bool sent_{false};

    // optional data only for player and npc
    std::shared_ptr<entity_opt_charactor> opt_charactor_{nullptr};

    // custom data
    using custom_data_list = object_hashmap<std::string, data>;
    custom_data_list custom_data_list_;

    // class meta
    std::shared_ptr<class_meta> class_meta_{nullptr};

    // node data
    std::shared_ptr<node_manager> node_manager_{nullptr};

    // table data
    std::shared_ptr<table_manager> table_manager_{nullptr};

    // event manager
    std::shared_ptr<event_manager> event_manager_{nullptr};

    // call back manager
    std::shared_ptr<class_call_back_manager> callback_manager_{nullptr};

public:
    entity_impl() = delete;

    explicit entity_impl(std::shared_ptr<class_meta> class_meta_ptr, const guid_t& guid, const id_type config_id,
        const int32_t map_id, const int32_t map_entity_id, const data_list& data_list);

    void update() override;

    // get unique id
    const guid_t& get_id() const override;

    // get parent unique id
    std::shared_ptr<container> get_parent_container() const override;
    bool set_parent_container(std::shared_ptr<container> container_ptr) override;

    bool is_public(const std::string& name) const override;
    bool is_public(const uint32_t index) const override;
    bool is_private(const std::string& name) const override;
    bool is_private(const uint32_t index) const override;
    bool is_save(const std::string& name) const override;
    bool is_save(const uint32_t index) const override;
    bool is_real_time(const std::string& name) const override;
    bool is_real_time(const uint32_t index) const override;
    bool have_mask(const std::string& name, data_mask feature) const override;
    bool have_mask(const uint32_t index, data_mask feature) const override;

    mask_type get_mask(const uint32_t index) const override;

    const std::string& get_class_name() const override;

    data_type get_node_type(const std::string& name) const override;
    data_type get_node_type(const uint32_t value) const override;

    uint32_t get_index(const std::string& name) const override;

    id_type get_config_id() const override;
    int32_t get_map_id() const override;
    int32_t get_map_entity_id() const override;
    bool switch_scene(const int32_t map_id, const int32_t map_inst_id, const vector3d& pos) override;

    const vector3d& get_position() const override;
    float get_position_x() const override;
    float get_position_y() const override;
    float get_position_z() const override;
    float get_orient() const override;

    bool set_position(const vector3d& postion, const float orient) override;
    bool set_position(const float x, const float y, const float z, const float orient) override;

    // set data
    bool set_bool(const std::string& name, bool value) override;
    bool set_int32(const std::string& name, const int32_t value) override;
    bool set_u_int32(const std::string& name, const uint32_t value) override;
    bool set_int64(const std::string& name, const int64_t value) override;
    bool set_u_int64(const std::string& name, const uint64_t value) override;
    bool set_float(const std::string& name, const float value) override;
    bool set_double(const std::string& name, const double value) override;
    bool set_string(const std::string& name, const std::string& value) override;
    bool set_w_string(const std::string& name, const std::wstring& value) override;
    bool set_guid(const std::string& name, const guid_t& value) override;

    bool set_bool(const uint32_t index, bool value) override;
    bool set_int32(const uint32_t index, const int32_t value) override;
    bool set_u_int32(const uint32_t index, const uint32_t value) override;
    bool set_int64(const uint32_t index, const int64_t value) override;
    bool set_u_int64(const uint32_t index, const uint64_t value) override;
    bool set_float(const uint32_t index, const float value) override;
    bool set_double(const uint32_t index, const double value) override;
    bool set_string(const uint32_t index, const std::string& value) override;
    bool set_w_string(const uint32_t index, const std::wstring& value) override;
    bool set_guid(const uint32_t index, const guid_t& value) override;

    // get data
    bool get_node(const std::string& name, data& data) const override;
    bool get_bool(const std::string& name) const override;
    int32_t get_int32(const std::string& name) const override;
    uint32_t get_u_int32(const std::string& name) const override;
    int64_t get_int64(const std::string& name) const override;
    uint64_t get_u_int64(const std::string& name) const override;
    float get_float(const std::string& name) const override;
    double get_double(const std::string& name) const override;
    const std::string& get_string(const std::string& name) const override;
    const std::wstring& get_w_string(const std::string& name) const override;
    const guid_t& get_guid(const std::string& name) const override;

    bool get_node(const uint32_t index, data& data) const override;
    bool get_bool(const uint32_t index) const override;
    int32_t get_int32(const uint32_t index) const override;
    uint32_t get_u_int32(const uint32_t index) const override;
    int64_t get_int64(const uint32_t index) const override;
    uint64_t get_u_int64(const uint32_t index) const override;
    float get_float(const uint32_t index) const override;
    double get_double(const uint32_t index) const override;
    const std::string& get_string(const uint32_t index) const override;
    const std::wstring& get_w_string(const uint32_t index) const override;
    const guid_t& get_guid(const uint32_t index) const override;

    // container operation
    std::shared_ptr<container> find_container(const std::string& name) override;
    std::shared_ptr<container> find_container(const uint32_t index) override;

    // table data
    table* find_table(const std::string& name) override;
    table* find_table(const uint32_t index) override;

    // custom data
    bool add_custom_bool(const std::string& name, bool value) override;
    bool add_custom_int32(const std::string& name, const int32_t value) override;
    bool add_custom_u_int32(const std::string& name, const uint32_t value) override;
    bool add_custom_int64(const std::string& name, const int64_t value) override;
    bool add_custom_float(const std::string& name, const float value) override;
    bool add_custom_double(const std::string& name, const double value) override;
    bool add_custom_string(const std::string& name, const std::string& value) override;
    bool add_custom_w_string(const std::string& name, const std::wstring& value) override;
    bool add_custom_guid(const std::string& name, const guid_t& value) override;

    bool set_custom_bool(const std::string& name, bool value) override;
    bool set_custom_int32(const std::string& name, const int32_t value) override;
    bool set_custom_u_int32(const std::string& name, const uint32_t value) override;
    bool set_custom_int64(const std::string& name, const int64_t value) override;
    bool set_custom_float(const std::string& name, const float value) override;
    bool set_custom_double(const std::string& name, const double value) override;
    bool set_custom_string(const std::string& name, const std::string& value) override;
    bool set_custom_w_string(const std::string& name, const std::wstring& value) override;
    bool set_custom_guid(const std::string& name, const guid_t& value) override;

    bool get_custom_bool(const std::string& name) const override;
    int32_t get_custom_int32(const std::string& name) const override;
    uint32_t get_custom_u_int32(const std::string& name) const override;
    int64_t get_custom_int64(const std::string& name) const override;
    float get_custom_float(const std::string& name) const override;
    double get_custom_double(const std::string& name) const override;
    const char* get_custom_string(const std::string& name) const override;
    const std::wstring& get_custom_w_string(const std::string& name) const override;
    const guid_t& get_custom_guid(const std::string& name) const override;

    bool find_custom_data(const std::string& name) const override;
    bool remove_custom_data(const std::string& name) override;

    bool is_sent() const override;
    void update_sent() override;

private:
    std::shared_ptr<node_manager> get_node_manager() const;

    std::shared_ptr<table_manager> get_table_manager() const;

    std::shared_ptr<container_manager> get_container_manager() const;

    std::shared_ptr<event_manager> get_event_manager() const;

    std::shared_ptr<class_meta> get_class_meta() const;

    int on_node_call_back(node* node_ptr, const data& old_data, const data& new_data);

    int on_table_call_back(const mask_type mask, node* node_ptr, const table_event_data& event_data,
        const data& old_data, const data& new_data);
};

} // namespace ark
