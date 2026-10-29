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
#include "ark/kernel/table.hpp"
#include "ark/kernel/table_meta.hpp"
#include "ark/macros.hpp"

namespace ark {

namespace entity_sync {
class snapshot_access;
}

class table_impl final : public table
{
public:
    using table_data = object_map<uint32_t, row>;
    using table_callback_functor =
        std::function<int(const mask_type, node*, const table_event_data&, const data&, const data&)>;

private:
    friend class kernel_module_impl;
    friend class entity_sync::snapshot_access;
    friend class kernel_nested_snapshot_test_access;

    // table meta
    std::shared_ptr<table_meta> table_meta_{nullptr};

    // current row
    uint32_t current_row_{0};
    std::uint64_t last_incarnation_{0};

    // table data
    table_data data_;

    // table iterator
    table_data::const_iterator iter_;

    // call back
    table_callback_functor func_;

public:
    table_impl() = delete;

    // constructor
    explicit table_impl(std::shared_ptr<table_meta> table_meta_ptr, table_callback_functor&& func);

    const std::string& get_name() const override;

    uint32_t get_col_count() const override;

    data_type get_col_type(const uint32_t index) const override;

    const mask_type get_mask() const override;
    bool have_mask(const data_mask mask) const override;
    bool is_public() const override;
    bool is_private() const override;
    bool is_real_time() const override;
    bool is_save() const override;

    // table get
    uint32_t get_row_count() const override;

    // table set
    row* add_row(uint32_t row = 0u) override;
    row* add_row(uint32_t row, const data_list& args) override;
    row* find_row(uint32_t row) const override;

    bool remove_row(uint32_t row) override;

    void clear() override;

    // find
    uint32_t find_int32(const uint32_t index, const int32_t value) const override;
    uint32_t find_int64(const uint32_t index, const int64_t value) const override;
    uint32_t find_bool(const uint32_t index, bool value) const override;
    uint32_t find_float(const uint32_t index, float value) const override;
    uint32_t find_double(const uint32_t index, double value) const override;
    uint32_t find_string(const uint32_t index, const std::string& value) const override;
    uint32_t find_w_string(const uint32_t index, const std::wstring& value) const override;
    uint32_t find_guid(const uint32_t index, const guid_t& value) const override;

    row* first() override;
    row* next() override;

    uint32_t get_index() const override;
    uint32_t get_index(const std::string& name) const override;

private:
    uint32_t select_row() const;

    void release_row(row* row_data);

    void on_table_changed(uint32_t row, table_op_type op_type);

    int on_row_changed(uint32_t row, node* node_ptr, const data& old_data, const data& new_data);

    row* create_row(uint32_t row, const data_list& args);
};

} // namespace ark
