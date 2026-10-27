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

#include "ark/container/map.hpp"
#include "ark/define.hpp"
#include "ark/enum.hpp"
#include "ark/kernel/class_call_back_manager.hpp"
#include "ark/kernel/container_meta.hpp"
#include "ark/kernel/entity_json.hpp"
#include "ark/kernel/node_meta.hpp"
#include "ark/kernel/table_meta.hpp"
#include "ark/macros.hpp"

namespace ark {

class table_meta;

class class_meta final
{
public:
    using name_index_list = std::unordered_map<std::string, uint32_t>;
    using data_meta_list = shared_object_map<uint32_t, node_meta>;
    using table_meta_list = shared_object_map<uint32_t, table_meta>;
    using container_meta_list = shared_object_map<uint32_t, container_meta>;

private:
    // class name
    std::string name_{null_str};

    // res path
    std::string res_path_{null_str};

    // Validated JSON rows are retained for one coherent startup snapshot.
    std::shared_ptr<const entity_json::config_definition> json_config_;
    std::string json_config_pointer_;

    // name index
    name_index_list name_index_list_;

    // data meta list
    data_meta_list data_meta_list_;

    // table meta list
    table_meta_list table_meta_list_;

    // container meta list
    container_meta_list container_meta_list_;

    // class meta call back
    std::shared_ptr<class_call_back_manager> class_meta_call_back_;

public:
    class_meta() = delete;

    explicit class_meta(const std::string& name);
    virtual ~class_meta() = default;

    size_t get_node_count() const;
    const std::string& get_name() const;

    void set_res_path(const std::string& value);
    const std::string& get_res_path() const;

    void set_json_config(std::shared_ptr<const entity_json::config_definition> config, std::string pointer);
    const std::shared_ptr<const entity_json::config_definition>& get_json_config() const;
    const std::string& get_json_config_pointer() const;

    // create data meta
    std::shared_ptr<node_meta> create_data_meta(const std::string& name, const uint32_t index);
    std::shared_ptr<node_meta> find_data_meta(const uint32_t index) const;

    // create table meta
    std::shared_ptr<table_meta> create_table_meta(const std::string& name, const uint32_t index);
    std::shared_ptr<table_meta> find_table_meta(const uint32_t index) const;

    // create table meta
    std::shared_ptr<container_meta> create_container_meta(
        const std::string& name, const uint32_t index, const std::string& class_name, const mask_type mask);
    std::shared_ptr<container_meta> find_container_meta(const uint32_t index) const;

    const data_meta_list& get_data_meta_list() const;
    const table_meta_list& get_table_meta_list() const;
    const container_meta_list& get_container_meta_list() const;

    std::shared_ptr<class_call_back_manager> get_class_call_back_manager() const;

    uint32_t get_index(const std::string& name) const;

    bool is_entity_meta() const;

private:
    bool name_index_insert(const std::string& name, const uint32_t index);
};

} // namespace ark
