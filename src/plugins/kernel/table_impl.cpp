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

#include <memory>

#include "ark/kernel/data_impl.hpp"
#include "ark/kernel/data_list_impl.hpp"
#include "ark/kernel/row.hpp"
#include "ark/kernel/table_impl.hpp"

namespace ark {

// constructor
table_impl::table_impl(std::shared_ptr<table_meta> table_meta_ptr, table_callback_functor&& func) {
    table_meta_ = table_meta_ptr;
    func_ = std::move(func);
}

const std::string& table_impl::get_name() const {
    ARK_ASSERT_RET_VAL(table_meta_ != nullptr, null_str);

    return table_meta_->get_name();
}

uint32_t table_impl::get_col_count() const {
    ARK_ASSERT_RET_VAL(table_meta_ != nullptr, (uint32_t)null_int);

    return table_meta_->get_col_count();
}

data_type table_impl::get_col_type(const uint32_t index) const {
    ARK_ASSERT_RET_VAL(table_meta_ != nullptr, data_type::dt_empty);

    return table_meta_->get_col_type(index);
}

const mask_type table_impl::get_mask() const {
    ARK_ASSERT_RET_VAL(table_meta_ != nullptr, null_int);

    return table_meta_->get_mask();
}

bool table_impl::have_mask(const data_mask mask) const {
    ARK_ASSERT_RET_VAL(table_meta_ != nullptr, false);

    return table_meta_->have_mask(mask);
}

bool table_impl::is_public() const {
    ARK_ASSERT_RET_VAL(table_meta_ != nullptr, false);

    return table_meta_->is_public();
}

bool table_impl::is_private() const {
    ARK_ASSERT_RET_VAL(table_meta_ != nullptr, false);

    return table_meta_->is_private();
}

bool table_impl::is_real_time() const {
    ARK_ASSERT_RET_VAL(table_meta_ != nullptr, false);

    return table_meta_->is_real_time();
}

bool table_impl::is_save() const {
    ARK_ASSERT_RET_VAL(table_meta_ != nullptr, false);

    return table_meta_->is_save();
}

// table get
uint32_t table_impl::get_row_count() const {
    return data_.size();
}

// table set
row* table_impl::add_row(uint32_t row /* = 0u*/) {
    return add_row(row, data_list_impl());
}

row* table_impl::add_row(uint32_t row, const data_list& args) {
    ARK_ASSERT_RET_VAL(args.get_count() % 2 == 0, nullptr);

    ark::row* row_data = nullptr;
    if (row == 0)
    {
        row = select_row();
    }

    row_data = create_row(row, args);
    ARK_ASSERT_RET_VAL(row_data != nullptr, nullptr);

    // call back
    on_table_changed(row, table_op_type::table_add);

    return row_data;
}

row* table_impl::find_row(uint32_t row) const {
    ARK_ASSERT_RET_VAL(row != 0, nullptr);

    return data_.find_value(row);
}

bool table_impl::remove_row(uint32_t row) {
    ARK_ASSERT_RET_VAL(data_.find_value(row) != nullptr, false);

    // call back
    on_table_changed(row, table_op_type::table_delete);

    data_.erase(row);

    return true;
}

void table_impl::clear() {
    on_table_changed(0u, table_op_type::table_clear);
    data_.clear();
}

// find
uint32_t table_impl::find_int32(const uint32_t index, const int32_t value) const {
    for (auto iter : data_)
    {
        auto row_data = iter.second;
        ARK_ASSERT_RET_VAL(row_data != nullptr, 0u);

        if (row_data->get_int32(index) == value)
        {
            return iter.first;
        }
    }

    return 0u;
}

uint32_t table_impl::find_int64(const uint32_t index, const int64_t value) const {
    for (auto iter : data_)
    {
        auto row_data = iter.second;
        ARK_ASSERT_RET_VAL(row_data != nullptr, 0u);

        if (row_data->get_int64(index) == value)
        {
            return iter.first;
        }
    }

    return 0u;
}

uint32_t table_impl::find_bool(const uint32_t index, bool value) const {
    for (auto iter : data_)
    {
        auto row_data = iter.second;
        ARK_ASSERT_RET_VAL(row_data != nullptr, 0u);

        if (row_data->get_bool(index) == value)
        {
            return iter.first;
        }
    }

    return 0u;
}

uint32_t table_impl::find_float(const uint32_t index, float value) const {
    for (auto iter : data_)
    {
        auto row_data = iter.second;
        ARK_ASSERT_RET_VAL(row_data != nullptr, 0u);

        if (row_data->get_float(index) == value)
        {
            return iter.first;
        }
    }

    return 0u;
}

uint32_t table_impl::find_double(const uint32_t index, double value) const {
    for (auto iter : data_)
    {
        auto row_data = iter.second;
        ARK_ASSERT_RET_VAL(row_data != nullptr, 0u);

        if (row_data->get_double(index) == value)
        {
            return iter.first;
        }
    }

    return 0u;
}

uint32_t table_impl::find_string(const uint32_t index, const std::string& value) const {
    for (auto iter : data_)
    {
        auto row_data = iter.second;
        ARK_ASSERT_RET_VAL(row_data != nullptr, 0u);

        if (row_data->get_string(index) == value)
        {
            return iter.first;
        }
    }

    return 0u;
}

uint32_t table_impl::find_w_string(const uint32_t index, const std::wstring& value) const {
    for (auto iter : data_)
    {
        auto row_data = iter.second;
        ARK_ASSERT_RET_VAL(row_data != nullptr, 0u);

        if (row_data->get_w_string(index) == value)
        {
            return iter.first;
        }
    }

    return 0u;
}

uint32_t table_impl::find_guid(const uint32_t index, const guid_t& value) const {
    for (auto iter : data_)
    {
        auto row_data = iter.second;
        ARK_ASSERT_RET_VAL(row_data != nullptr, 0u);

        if (row_data->get_guid(index) == value)
        {
            return iter.first;
        }
    }

    return 0u;
}

row* table_impl::first() {
    iter_ = data_.begin();
    ARK_ASSERT_RET_VAL(iter_ != data_.end(), nullptr);

    return iter_->second;
}

row* table_impl::next() {
    ARK_ASSERT_RET_VAL(iter_ != data_.end(), nullptr);

    iter_++;
    ARK_ASSERT_RET_VAL(iter_ != data_.end(), nullptr);

    return iter_->second;
}

uint32_t table_impl::get_index() const {
    ARK_ASSERT_RET_VAL(table_meta_ != nullptr, null_int);

    return table_meta_->get_index();
}

uint32_t table_impl::get_index(const std::string& name) const {
    ARK_ASSERT_RET_VAL(table_meta_ != nullptr, null_int);

    return table_meta_->get_index(name);
}

uint32_t table_impl::select_row() const {
    if (data_.size() == current_row_)
    {
        return current_row_ + 1;
    }
    else
    {
        // row starts from 1
        uint32_t row = 1u;
        for (auto iter : data_)
        {
            if (iter.first != row)
            {
                break;
            }

            row += 1;
        }

        return row;
    }
}

void table_impl::release_row(row* row_data) {
    ARK_DELETE(row_data);
}

void table_impl::on_table_changed(uint32_t row, table_op_type op_type) {
    ARK_ASSERT_RET_NONE(table_meta_ != nullptr);

    table_event_data source_data;
    source_data.table_name = table_meta_->get_name();
    source_data.table_index = table_meta_->get_index();
    source_data.row = row;
    source_data.op_type = (size_t)op_type;

    func_(table_meta_->get_mask(), nullptr, source_data, data_impl(data_type::dt_boolean, false),
        data_impl(data_type::dt_boolean, false));
}

// mask of row's nodes are not used yet
int table_impl::on_row_changed(uint32_t row, node* node_ptr, const data& old_data, const data& new_data) {
    ARK_ASSERT_RET_VAL(table_meta_ != nullptr, false);

    table_event_data source_data;
    source_data.table_name = table_meta_->get_name();
    source_data.table_index = table_meta_->get_index();
    source_data.data_index = node_ptr->get_index();
    source_data.row = row;
    source_data.op_type = (size_t)table_op_type::table_update;

    func_(table_meta_->get_mask(), node_ptr, source_data, old_data, new_data);

    return 0;
}

row* table_impl::create_row(uint32_t row, const data_list& args) {
    // return if found
    ARK_ASSERT_RET_VAL(data_.find_value(row) == nullptr, nullptr);

    ARK_ASSERT_RET_VAL(table_meta_ != nullptr, nullptr);

    auto class_meta_ptr = table_meta_->get_class_meta();
    ARK_ASSERT_RET_VAL(class_meta_ptr != nullptr, nullptr);

    auto func = std::bind(&table_impl::on_row_changed, this, std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3, std::placeholders::_4);

    const auto delete_row = [](row_impl* value) { ARK_DELETE(value); };
    std::unique_ptr<row_impl, decltype(delete_row)> row_ptr(
        ARK_NEW row_impl(class_meta_ptr, row, args, std::move(func)), delete_row);
    if (row_ptr == nullptr)
    {
        return nullptr;
    }
    if (!data_.insert(row, row_ptr.get()).second)
    {
        return nullptr;
    }
    auto* published_row = row_ptr.release();

    if (current_row_ < row)
    {
        current_row_ = row;
    }

    return published_row;
}

} // namespace ark
