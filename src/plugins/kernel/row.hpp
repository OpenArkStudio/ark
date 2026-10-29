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
#include "ark/kernel/data.hpp"
#include "ark/kernel/data_list.hpp"
#include "ark/kernel/node.hpp"
#include "ark/kernel/node_manager.hpp"
#include "ark/kernel/table.hpp"
#include "ark/macros.hpp"

namespace ark {

namespace entity_sync {
class snapshot_access;
}

class row_impl final : public row
{
private:
    friend class kernel_module_impl;
    friend class table_impl;
    friend class entity_sync::snapshot_access;

    // row
    uint32_t row_{0};
    std::uint64_t incarnation_{0};

    // table call back
    using row_callback_functor = std::function<int(const uint32_t, node*, const data&, const data&)>;
    row_callback_functor func_;

    // data
    std::shared_ptr<node_manager> node_manager_{nullptr};

public:
    row_impl() = delete;

    // constructor
    explicit row_impl(
        std::shared_ptr<class_meta> class_meta_ptr, uint32_t row, const data_list& args, row_callback_functor&& func);

    // get row
    uint32_t get_row() const override;

    // get row data
    bool get_bool(const uint32_t index) const override;
    int32_t get_int32(const uint32_t index) const override;
    int64_t get_int64(const uint32_t index) const override;
    uint32_t get_u_int32(const uint32_t index) const override;
    uint64_t get_u_int64(const uint32_t index) const override;
    float get_float(const uint32_t index) const override;
    double get_double(const uint32_t index) const override;
    const std::string& get_string(const uint32_t index) const override;
    const std::wstring& get_w_string(const uint32_t index) const override;
    const guid_t& get_guid(const uint32_t index) const override;

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

    // set row data
    bool set_bool(const uint32_t index, bool value) override;
    bool set_int32(const uint32_t index, int32_t value) override;
    bool set_u_int32(const uint32_t index, uint32_t value) override;
    bool set_int64(const uint32_t index, int64_t value) override;
    bool set_u_int64(const uint32_t index, uint64_t value) override;
    bool set_float(const uint32_t index, float value) override;
    bool set_double(const uint32_t index, double value) override;
    bool set_string(const uint32_t index, const std::string& value) override;
    bool set_w_string(const uint32_t index, const std::wstring& value) override;
    bool set_guid(const uint32_t index, const guid_t& value) override;

    bool set_bool(const std::string& name, bool value) override;
    bool set_int32(const std::string& name, int32_t value) override;
    bool set_u_int32(const std::string& name, uint32_t value) override;
    bool set_int64(const std::string& name, int64_t value) override;
    bool set_u_int64(const std::string& name, uint64_t value) override;
    bool set_float(const std::string& name, float value) override;
    bool set_double(const std::string& name, double value) override;
    bool set_string(const std::string& name, const std::string& value) override;
    bool set_w_string(const std::string& name, const std::wstring& value) override;
    bool set_guid(const std::string& name, const guid_t& value) override;

private:
    std::shared_ptr<node_manager> get_node_manager() const;

    int on_node_call_back(node* node_ptr, const data& old_data, const data& new_data);
};

} // namespace ark
