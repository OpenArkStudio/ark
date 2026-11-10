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
#include "ark/kernel/container.hpp"
#include "ark/kernel/data.hpp"
#include "ark/kernel/table.hpp"

namespace ark {

class container;

class entity {
public:
    virtual ~entity() = default;

    // get object unique id
    virtual const guid_t& get_id() const = 0;

    // get object parent unique id
    virtual std::shared_ptr<container> get_parent_container() const = 0;
    virtual bool set_parent_container(std::shared_ptr<container> container_ptr) = 0;

    virtual void update() = 0;

    // get base data
    virtual bool is_public(const std::string& name) const = 0;
    virtual bool is_public(const uint32_t index) const = 0;
    virtual bool is_private(const std::string& name) const = 0;
    virtual bool is_private(const uint32_t index) const = 0;
    virtual bool is_save(const std::string& name) const = 0;
    virtual bool is_save(const uint32_t index) const = 0;
    virtual bool is_real_time(const std::string& name) const = 0;
    virtual bool is_real_time(const uint32_t index) const = 0;
    virtual bool have_mask(const std::string& name, data_mask feature) const = 0;
    virtual bool have_mask(const uint32_t index, data_mask feature) const = 0;

    virtual mask_type get_mask(const uint32_t index) const = 0;

    virtual const std::string& get_class_name() const = 0;

    virtual data_type get_node_type(const std::string& name) const = 0;
    virtual data_type get_node_type(const uint32_t value) const = 0;

    virtual uint32_t get_index(const std::string& name) const = 0;

    virtual id_type get_config_id() const = 0;
    virtual int32_t get_map_id() const = 0;
    virtual int32_t get_map_entity_id() const = 0;
    virtual bool switch_scene(const int32_t map_id, const int32_t map_inst_id, const vector3d& pos) = 0;

    virtual const vector3d& get_position() const = 0;
    virtual float get_position_x() const = 0;
    virtual float get_position_y() const = 0;
    virtual float get_position_z() const = 0;
    virtual float get_orient() const = 0;

    virtual bool set_position(const vector3d& postion, const float orient) = 0;
    virtual bool set_position(const float x, const float y, const float z, const float orient) = 0;

    // set data
    virtual bool set_bool(const std::string& name, bool value) = 0;
    virtual bool set_int32(const std::string& name, const int32_t value) = 0;
    virtual bool set_u_int32(const std::string& name, const uint32_t value) = 0;
    virtual bool set_int64(const std::string& name, const int64_t value) = 0;
    virtual bool set_u_int64(const std::string& name, const uint64_t value) = 0;
    virtual bool set_float(const std::string& name, const float value) = 0;
    virtual bool set_double(const std::string& name, const double value) = 0;
    virtual bool set_string(const std::string& name, const std::string& value) = 0;
    virtual bool set_w_string(const std::string& name, const std::wstring& value) = 0;
    virtual bool set_guid(const std::string& name, const guid_t& value) = 0;

    virtual bool set_bool(const uint32_t index, bool value) = 0;
    virtual bool set_int32(const uint32_t index, const int32_t value) = 0;
    virtual bool set_u_int32(const uint32_t index, const uint32_t value) = 0;
    virtual bool set_int64(const uint32_t index, const int64_t value) = 0;
    virtual bool set_u_int64(const uint32_t index, const uint64_t value) = 0;
    virtual bool set_float(const uint32_t index, const float value) = 0;
    virtual bool set_double(const uint32_t index, const double value) = 0;
    virtual bool set_string(const uint32_t index, const std::string& value) = 0;
    virtual bool set_w_string(const uint32_t index, const std::wstring& value) = 0;
    virtual bool set_guid(const uint32_t index, const guid_t& value) = 0;

    // Get data by field name
    virtual bool get_node(const std::string& name, data& data) const = 0;
    virtual bool get_bool(const std::string& name) const = 0;
    virtual int32_t get_int32(const std::string& name) const = 0;
    virtual uint32_t get_u_int32(const std::string& name) const = 0;
    virtual int64_t get_int64(const std::string& name) const = 0;
    virtual uint64_t get_u_int64(const std::string& name) const = 0;
    virtual float get_float(const std::string& name) const = 0;
    virtual double get_double(const std::string& name) const = 0;
    virtual const std::string& get_string(const std::string& name) const = 0;
    virtual const std::wstring& get_w_string(const std::string& name) const = 0;
    virtual const guid_t& get_guid(const std::string& name) const = 0;

    // Get data by field index
    virtual bool get_node(const uint32_t index, data& data) const = 0;
    virtual bool get_bool(const uint32_t index) const = 0;
    virtual int32_t get_int32(const uint32_t index) const = 0;
    virtual uint32_t get_u_int32(const uint32_t index) const = 0;
    virtual int64_t get_int64(const uint32_t index) const = 0;
    virtual uint64_t get_u_int64(const uint32_t index) const = 0;
    virtual float get_float(const uint32_t index) const = 0;
    virtual double get_double(const uint32_t index) const = 0;
    virtual const std::string& get_string(const uint32_t index) const = 0;
    virtual const std::wstring& get_w_string(const uint32_t index) const = 0;
    virtual const guid_t& get_guid(const uint32_t index) const = 0;

    // table data
    virtual table* find_table(const std::string& name) = 0;
    virtual table* find_table(const uint32_t index) = 0;

    virtual std::shared_ptr<container> find_container(const std::string& name) = 0;
    virtual std::shared_ptr<container> find_container(const uint32_t index) = 0;

    // custom data
    virtual bool add_custom_bool(const std::string& name, bool value) = 0;
    virtual bool add_custom_int32(const std::string& name, const int32_t value) = 0;
    virtual bool add_custom_u_int32(const std::string& name, const uint32_t value) = 0;
    virtual bool add_custom_int64(const std::string& name, const int64_t value) = 0;
    virtual bool add_custom_float(const std::string& name, const float value) = 0;
    virtual bool add_custom_double(const std::string& name, const double value) = 0;
    virtual bool add_custom_string(const std::string& name, const std::string& value) = 0;
    virtual bool add_custom_w_string(const std::string& name, const std::wstring& value) = 0;
    virtual bool add_custom_guid(const std::string& name, const guid_t& value) = 0;

    virtual bool set_custom_bool(const std::string& name, bool value) = 0;
    virtual bool set_custom_int32(const std::string& name, const int32_t value) = 0;
    virtual bool set_custom_u_int32(const std::string& name, const uint32_t value) = 0;
    virtual bool set_custom_int64(const std::string& name, const int64_t value) = 0;
    virtual bool set_custom_float(const std::string& name, const float value) = 0;
    virtual bool set_custom_double(const std::string& name, const double value) = 0;
    virtual bool set_custom_string(const std::string& name, const std::string& value) = 0;
    virtual bool set_custom_w_string(const std::string& name, const std::wstring& value) = 0;
    virtual bool set_custom_guid(const std::string& name, const guid_t& value) = 0;

    virtual bool get_custom_bool(const std::string& name) const = 0;
    virtual int32_t get_custom_int32(const std::string& name) const = 0;
    virtual uint32_t get_custom_u_int32(const std::string& name) const = 0;
    virtual int64_t get_custom_int64(const std::string& name) const = 0;
    virtual float get_custom_float(const std::string& name) const = 0;
    virtual double get_custom_double(const std::string& name) const = 0;
    virtual const char* get_custom_string(const std::string& name) const = 0;
    virtual const std::wstring& get_custom_w_string(const std::string& name) const = 0;
    virtual const guid_t& get_custom_guid(const std::string& name) const = 0;

    virtual bool find_custom_data(const std::string& name) const = 0;
    virtual bool remove_custom_data(const std::string& name) = 0;

    virtual bool is_sent() const = 0;
    virtual void update_sent() = 0;
};

} // namespace ark
