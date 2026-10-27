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
#include "ark/kernel/static_entity.hpp"
#include "ark/kernel/table.hpp"

namespace ark {

class static_entity_impl final : public static_entity
{
public:
    using table_list = object_map<uint32_t, table>;

private:
    friend class kernel_module_impl;

    // config id
    id_type config_id_{0};

    // class meta
    std::shared_ptr<class_meta> class_meta_{nullptr};

    // data
    std::shared_ptr<node_manager> node_manager_{nullptr};

public:
    static_entity_impl() = delete;

    explicit static_entity_impl(std::shared_ptr<class_meta> class_meta_ptr,
        std::shared_ptr<node_manager> node_manager_ptr, const id_type config_id);

    // query data
    const std::string& get_class_name() const override;
    id_type get_config_id() const override;
    bool get_bool(const std::string& name) const override;
    int32_t get_int32(const std::string& name) const override;
    uint32_t get_u_int32(const std::string& name) const override;
    int64_t get_int64(const std::string& name) const override;
    uint64_t get_u_int64(const std::string& name) const override;
    float get_float(const std::string& name) const override;
    double get_double(const std::string& name) const override;
    const std::string& get_string(const std::string& name) const override;
    const std::wstring& get_w_string(const std::string& name) const override;

    bool get_bool(const uint32_t index) const override;
    int32_t get_int32(const uint32_t index) const override;
    uint32_t get_u_int32(const uint32_t index) const override;
    int64_t get_int64(const uint32_t index) const override;
    uint64_t get_u_int64(const uint32_t index) const override;
    float get_float(const uint32_t index) const override;
    double get_double(const uint32_t index) const override;
    const std::string& get_string(const uint32_t index) const override;
    const std::wstring& get_w_string(const uint32_t index) const override;

private:
    std::shared_ptr<node_manager> get_node_manager() const;
};

} // namespace ark
