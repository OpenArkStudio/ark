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

#include "ark/kernel/class_meta.hpp"

namespace ark {

class_meta::class_meta(const std::string& name)
    : name_(name) {
    class_meta_call_back_ = std::make_shared<class_call_back_manager>();
}

size_t class_meta::get_node_count() const {
    return data_meta_list_.size();
}

const std::string& class_meta::get_name() const {
    return name_;
}

void class_meta::set_res_path(const std::string& value) {
    res_path_ = value;
}

const std::string& class_meta::get_res_path() const {
    return res_path_;
}

void class_meta::set_json_config(std::shared_ptr<const entity_json::config_definition> config, std::string pointer) {
    json_config_ = std::move(config);
    json_config_pointer_ = std::move(pointer);
}

const std::shared_ptr<const entity_json::config_definition>& class_meta::get_json_config() const {
    return json_config_;
}

const std::string& class_meta::get_json_config_pointer() const {
    return json_config_pointer_;
}

// create data meta
std::shared_ptr<node_meta> class_meta::create_data_meta(const std::string& name, const uint32_t index) {
    // check arg
    ARK_ASSERT_RET_VAL(!name.empty() && index > 0, nullptr);

    ARK_ASSERT_RET_VAL(data_meta_list_.find(index) == data_meta_list_.end(), nullptr);

    ARK_ASSERT_RET_VAL(name_index_insert(name, index), nullptr);

    // create new data meta
    std::shared_ptr<node_meta> meta_ptr = std::make_shared<node_meta>(name, index);
    ARK_ASSERT_RET_VAL(data_meta_list_.insert(index, meta_ptr).second, nullptr);

    return meta_ptr;
}

std::shared_ptr<node_meta> class_meta::find_data_meta(const uint32_t index) const {
    return data_meta_list_.find_value(index);
}

// create table meta
std::shared_ptr<table_meta> class_meta::create_table_meta(const std::string& name, const uint32_t index) {
    // check arg
    ARK_ASSERT_RET_VAL(!name.empty() && index > 0, nullptr);

    ARK_ASSERT_RET_VAL(table_meta_list_.find(index) == table_meta_list_.end(), nullptr);

    ARK_ASSERT_RET_VAL(name_index_insert(name, index), nullptr);

    std::shared_ptr<table_meta> meta_ptr = std::make_shared<table_meta>(name, index);
    ARK_ASSERT_RET_VAL(table_meta_list_.insert(index, meta_ptr).second, nullptr);

    return meta_ptr;
}

std::shared_ptr<table_meta> class_meta::find_table_meta(const uint32_t index) const {
    return table_meta_list_.find_value(index);
}

// create container meta
std::shared_ptr<container_meta> class_meta::create_container_meta(
    const std::string& name, const uint32_t index, const std::string& class_name, const mask_type mask) {
    // check arg
    ARK_ASSERT_RET_VAL(!name.empty() && index > 0, nullptr);

    ARK_ASSERT_RET_VAL(container_meta_list_.find(index) == container_meta_list_.end(), nullptr);

    ARK_ASSERT_RET_VAL(name_index_insert(name, index), nullptr);

    std::shared_ptr<container_meta> meta_ptr = std::make_shared<container_meta>(name, index, class_name, mask);
    ARK_ASSERT_RET_VAL(container_meta_list_.insert(index, meta_ptr).second, nullptr);

    return meta_ptr;
}

std::shared_ptr<container_meta> class_meta::find_container_meta(const uint32_t index) const {
    return container_meta_list_.find_value(index);
}

const class_meta::data_meta_list& class_meta::get_data_meta_list() const {
    return data_meta_list_;
}

const class_meta::table_meta_list& class_meta::get_table_meta_list() const {
    return table_meta_list_;
}

const class_meta::container_meta_list& class_meta::get_container_meta_list() const {
    return container_meta_list_;
}

std::shared_ptr<class_call_back_manager> class_meta::get_class_call_back_manager() const {
    return class_meta_call_back_;
}

uint32_t class_meta::get_index(const std::string& name) const {
    auto iter = name_index_list_.find(name);
    if (iter == name_index_list_.end())
    {
        return null_int;
    }

    return iter->second;
}

bool class_meta::is_entity_meta() const {
    return res_path_.empty();
}

bool class_meta::name_index_insert(const std::string& name, const uint32_t index) {
    return name_index_list_.insert(std::make_pair(name, index)).second;
}

} // namespace ark
