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

#include "ark/kernel/class_meta.hpp"
#include "ark/kernel/data_list.hpp"
#include "ark/kernel/node.hpp"

namespace ark {

class node_manager final {
public:
    using node_map = object_map<uint32_t, node>;
    using node_manager_functor = std::function<int(node*, const data&, const data&)>;

private:
    // class meta
    std::shared_ptr<class_meta> class_meta_{nullptr};

    // call back
    node_manager_functor func_;

    // data list
    node_map data_list_;

public:
    node_manager() = delete;

    explicit node_manager(std::shared_ptr<class_meta> class_meta_ptr);
    explicit node_manager(
        std::shared_ptr<class_meta> class_meta_ptr, const data_list& data_list, node_manager_functor&& func);

    void init_data(const data_list& args);

    bool is_empty() const;

    // data operation
    node* create_data(std::shared_ptr<node_meta> data_meta_ptr);
    bool create_data(node* data_ptr);
    node* create_data(const std::string& name);

    // get node
    node* get_node(const std::string& name) const;
    node* get_node(const uint32_t index) const;

    // query data
    bool get_bool(const std::string& name) const;
    int32_t get_int32(const std::string& name) const;
    uint32_t get_u_int32(const std::string& name) const;
    int64_t get_int64(const std::string& name) const;
    uint64_t get_u_int64(const std::string& name) const;
    float get_float(const std::string& name) const;
    double get_double(const std::string& name) const;
    const std::string& get_string(const std::string& name) const;
    const std::wstring& get_w_string(const std::string& name) const;
    const guid_t& get_guid(const std::string& name) const;

    bool get_bool(const uint32_t index) const;
    int32_t get_int32(const uint32_t index) const;
    uint32_t get_u_int32(const uint32_t index) const;
    int64_t get_int64(const uint32_t index) const;
    uint64_t get_u_int64(const uint32_t index) const;
    float get_float(const uint32_t index) const;
    double get_double(const uint32_t index) const;
    const std::string& get_string(const uint32_t index) const;
    const std::wstring& get_w_string(const uint32_t index) const;
    const guid_t& get_guid(const uint32_t index) const;

    // set data
    bool set_bool(const std::string& name, bool value);
    bool set_int32(const std::string& name, const int32_t value);
    bool set_u_int32(const std::string& name, const uint32_t value);
    bool set_int64(const std::string& name, const int64_t value);
    bool set_u_int64(const std::string& name, const uint64_t value);
    bool set_float(const std::string& name, const float value);
    bool set_double(const std::string& name, const double value);
    bool set_string(const std::string& name, const std::string& value);
    bool set_w_string(const std::string& name, const std::wstring& value);
    bool set_guid(const std::string& name, const guid_t& value);

    bool set_bool(const uint32_t index, bool value);
    bool set_int32(const uint32_t index, const int32_t value);
    bool set_u_int32(const uint32_t index, const uint32_t value);
    bool set_int64(const uint32_t index, const int64_t value);
    bool set_u_int64(const uint32_t index, const uint64_t value);
    bool set_float(const uint32_t index, const float value);
    bool set_double(const uint32_t index, const double value);
    bool set_string(const uint32_t index, const std::string& value);
    bool set_w_string(const uint32_t index, const std::wstring& value);
    bool set_guid(const uint32_t index, const guid_t& value);

    // other query
    const node_map& get_data_list() const;

private:
    uint32_t get_index(const std::string& name) const;

    node* find_data(const uint32_t index);
    node* find_data(const uint32_t index) const;
};

} // namespace ark
