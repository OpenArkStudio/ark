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

#include <functional>

#include "ark/kernel/data_impl.hpp"
#include "ark/kernel/node_impl.hpp"
#include "ark/kernel/row.hpp"

namespace ark {

// constructor
row_impl::row_impl(
    std::shared_ptr<class_meta> class_meta_ptr, uint32_t row, const data_list& args, row_callback_functor&& func)
    : row_(row) {
    // data node
    auto function = std::bind_front(&row_impl::on_node_call_back, this);
    node_manager_ = std::make_shared<node_manager>(class_meta_ptr, args, std::move(function));

    func_ = std::move(func);
}

// get row
uint32_t row_impl::get_row() const {
    return row_;
}

// get row data
bool row_impl::get_bool(const uint32_t index) const {
    ARK_ASSERT_RET_VAL(node_manager_ != nullptr, null_boolean);

    return node_manager_->get_bool(index);
}

int32_t row_impl::get_int32(const uint32_t index) const {
    ARK_ASSERT_RET_VAL(node_manager_ != nullptr, null_int);

    return node_manager_->get_int32(index);
}

uint32_t row_impl::get_u_int32(const uint32_t index) const {
    ARK_ASSERT_RET_VAL(node_manager_ != nullptr, null_int);

    return node_manager_->get_u_int32(index);
}

int64_t row_impl::get_int64(const uint32_t index) const {
    ARK_ASSERT_RET_VAL(node_manager_ != nullptr, null_int64);

    return node_manager_->get_int64(index);
}

uint64_t row_impl::get_u_int64(const uint32_t index) const {
    ARK_ASSERT_RET_VAL(node_manager_ != nullptr, null_int64);

    return node_manager_->get_u_int64(index);
}

float row_impl::get_float(const uint32_t index) const {
    ARK_ASSERT_RET_VAL(node_manager_ != nullptr, null_float);

    return node_manager_->get_float(index);
}

double row_impl::get_double(const uint32_t index) const {
    ARK_ASSERT_RET_VAL(node_manager_ != nullptr, null_double);

    return node_manager_->get_double(index);
}

const std::string& row_impl::get_string(const uint32_t index) const {
    ARK_ASSERT_RET_VAL(node_manager_ != nullptr, null_str);

    return node_manager_->get_string(index);
}

const std::wstring& row_impl::get_w_string(const uint32_t index) const {
    ARK_ASSERT_RET_VAL(node_manager_ != nullptr, null_widestr);

    return node_manager_->get_w_string(index);
}

const guid_t& row_impl::get_guid(const uint32_t index) const {
    ARK_ASSERT_RET_VAL(node_manager_ != nullptr, null_guid);

    return node_manager_->get_guid(index);
}

bool row_impl::get_bool(const std::string& name) const {
    ARK_ASSERT_RET_VAL(node_manager_ != nullptr, null_boolean);

    return node_manager_->get_bool(name);
}

int32_t row_impl::get_int32(const std::string& name) const {
    ARK_ASSERT_RET_VAL(node_manager_ != nullptr, null_int);

    return node_manager_->get_int32(name);
}

uint32_t row_impl::get_u_int32(const std::string& name) const {
    ARK_ASSERT_RET_VAL(node_manager_ != nullptr, null_int);

    return node_manager_->get_u_int32(name);
}

int64_t row_impl::get_int64(const std::string& name) const {
    ARK_ASSERT_RET_VAL(node_manager_ != nullptr, null_int64);

    return node_manager_->get_int64(name);
}

uint64_t row_impl::get_u_int64(const std::string& name) const {
    ARK_ASSERT_RET_VAL(node_manager_ != nullptr, null_int64);

    return node_manager_->get_u_int64(name);
}

float row_impl::get_float(const std::string& name) const {
    ARK_ASSERT_RET_VAL(node_manager_ != nullptr, null_float);

    return node_manager_->get_float(name);
}

double row_impl::get_double(const std::string& name) const {
    ARK_ASSERT_RET_VAL(node_manager_ != nullptr, null_double);

    return node_manager_->get_double(name);
}

const std::string& row_impl::get_string(const std::string& name) const {
    ARK_ASSERT_RET_VAL(node_manager_ != nullptr, null_str);

    return node_manager_->get_string(name);
}

const std::wstring& row_impl::get_w_string(const std::string& name) const {
    ARK_ASSERT_RET_VAL(node_manager_ != nullptr, null_widestr);

    return node_manager_->get_w_string(name);
}

const guid_t& row_impl::get_guid(const std::string& name) const {
    ARK_ASSERT_RET_VAL(node_manager_ != nullptr, null_guid);

    return node_manager_->get_guid(name);
}

// set row data
bool row_impl::set_bool(const uint32_t index, bool value) {
    ARK_ASSERT_RET_VAL(node_manager_ != nullptr, false);

    return node_manager_->set_bool(index, value);
}

bool row_impl::set_int32(const uint32_t index, int32_t value) {
    ARK_ASSERT_RET_VAL(node_manager_ != nullptr, false);

    return node_manager_->set_int32(index, value);
}

bool row_impl::set_int64(const uint32_t index, int64_t value) {
    ARK_ASSERT_RET_VAL(node_manager_ != nullptr, false);

    return node_manager_->set_int64(index, value);
}

bool row_impl::set_u_int32(const uint32_t index, uint32_t value) {
    ARK_ASSERT_RET_VAL(node_manager_ != nullptr, false);

    return node_manager_->set_u_int32(index, value);
}

bool row_impl::set_u_int64(const uint32_t index, uint64_t value) {
    ARK_ASSERT_RET_VAL(node_manager_ != nullptr, false);

    return node_manager_->set_u_int64(index, value);
}

bool row_impl::set_float(const uint32_t index, float value) {
    ARK_ASSERT_RET_VAL(node_manager_ != nullptr, false);

    return node_manager_->set_float(index, value);
}

bool row_impl::set_double(const uint32_t index, double value) {
    ARK_ASSERT_RET_VAL(node_manager_ != nullptr, false);

    return node_manager_->set_double(index, value);
}

bool row_impl::set_string(const uint32_t index, const std::string& value) {
    ARK_ASSERT_RET_VAL(node_manager_ != nullptr, false);

    return node_manager_->set_string(index, value);
}

bool row_impl::set_w_string(const uint32_t index, const std::wstring& value) {
    ARK_ASSERT_RET_VAL(node_manager_ != nullptr, false);

    return node_manager_->set_w_string(index, value);
}

bool row_impl::set_guid(const uint32_t index, const guid_t& value) {
    ARK_ASSERT_RET_VAL(node_manager_ != nullptr, false);

    return node_manager_->set_guid(index, value);
}

bool row_impl::set_bool(const std::string& name, bool value) {
    ARK_ASSERT_RET_VAL(node_manager_ != nullptr, false);

    return node_manager_->set_bool(name, value);
}

bool row_impl::set_int32(const std::string& name, int32_t value) {
    ARK_ASSERT_RET_VAL(node_manager_ != nullptr, false);

    return node_manager_->set_int32(name, value);
}

bool row_impl::set_u_int32(const std::string& name, uint32_t value) {
    ARK_ASSERT_RET_VAL(node_manager_ != nullptr, false);

    return node_manager_->set_u_int32(name, value);
}

bool row_impl::set_int64(const std::string& name, int64_t value) {
    ARK_ASSERT_RET_VAL(node_manager_ != nullptr, false);

    return node_manager_->set_int64(name, value);
}

bool row_impl::set_u_int64(const std::string& name, uint64_t value) {
    ARK_ASSERT_RET_VAL(node_manager_ != nullptr, false);

    return node_manager_->set_u_int64(name, value);
}

bool row_impl::set_float(const std::string& name, float value) {
    ARK_ASSERT_RET_VAL(node_manager_ != nullptr, false);

    return node_manager_->set_float(name, value);
}

bool row_impl::set_double(const std::string& name, double value) {
    ARK_ASSERT_RET_VAL(node_manager_ != nullptr, false);

    return node_manager_->set_double(name, value);
}

bool row_impl::set_string(const std::string& name, const std::string& value) {
    ARK_ASSERT_RET_VAL(node_manager_ != nullptr, false);

    return node_manager_->set_string(name, value);
}

bool row_impl::set_w_string(const std::string& name, const std::wstring& value) {
    ARK_ASSERT_RET_VAL(node_manager_ != nullptr, false);

    return node_manager_->set_w_string(name, value);
}

bool row_impl::set_guid(const std::string& name, const guid_t& value) {
    ARK_ASSERT_RET_VAL(node_manager_ != nullptr, false);

    return node_manager_->set_guid(name, value);
}

std::shared_ptr<node_manager> row_impl::get_node_manager() const {
    return node_manager_;
}

int row_impl::on_node_call_back(node* node_ptr, const data& old_data, const data& new_data) {
    if (func_)
    {
        func_(row_, node_ptr, old_data, new_data);
    }

    return 0;
}

} // namespace ark
