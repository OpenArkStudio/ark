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

#include "ark/kernel/data_list.hpp"
#include "ark/kernel/node.hpp"

namespace ark {

class row {
public:
    virtual ~row() = default;

    // query row
    virtual uint32_t get_row() const = 0;

    // get row data
    virtual bool get_bool(const uint32_t index) const = 0;
    virtual int32_t get_int32(const uint32_t index) const = 0;
    virtual int64_t get_int64(const uint32_t index) const = 0;
    virtual uint32_t get_u_int32(const uint32_t index) const = 0;
    virtual uint64_t get_u_int64(const uint32_t index) const = 0;
    virtual float get_float(const uint32_t index) const = 0;
    virtual double get_double(const uint32_t index) const = 0;
    virtual const std::string& get_string(const uint32_t index) const = 0;
    virtual const std::wstring& get_w_string(const uint32_t index) const = 0;
    virtual const guid_t& get_guid(const uint32_t index) const = 0;

    virtual bool get_bool(const std::string& name) const = 0;
    virtual int32_t get_int32(const std::string& name) const = 0;
    virtual int64_t get_int64(const std::string& name) const = 0;
    virtual uint32_t get_u_int32(const std::string& name) const = 0;
    virtual uint64_t get_u_int64(const std::string& name) const = 0;
    virtual float get_float(const std::string& name) const = 0;
    virtual double get_double(const std::string& name) const = 0;
    virtual const std::string& get_string(const std::string& name) const = 0;
    virtual const std::wstring& get_w_string(const std::string& name) const = 0;
    virtual const guid_t& get_guid(const std::string& name) const = 0;

    // set row data
    virtual bool set_bool(const uint32_t index, bool value) = 0;
    virtual bool set_int32(const uint32_t index, int32_t value) = 0;
    virtual bool set_int64(const uint32_t index, int64_t value) = 0;
    virtual bool set_u_int32(const uint32_t index, uint32_t value) = 0;
    virtual bool set_u_int64(const uint32_t index, uint64_t value) = 0;
    virtual bool set_float(const uint32_t index, float value) = 0;
    virtual bool set_double(const uint32_t index, double value) = 0;
    virtual bool set_string(const uint32_t index, const std::string& value) = 0;
    virtual bool set_w_string(const uint32_t index, const std::wstring& value) = 0;
    virtual bool set_guid(const uint32_t index, const guid_t& value) = 0;

    virtual bool set_bool(const std::string& name, bool value) = 0;
    virtual bool set_int32(const std::string& name, int32_t value) = 0;
    virtual bool set_int64(const std::string& name, int64_t value) = 0;
    virtual bool set_u_int32(const std::string& name, uint32_t value) = 0;
    virtual bool set_u_int64(const std::string& name, uint64_t value) = 0;
    virtual bool set_float(const std::string& name, float value) = 0;
    virtual bool set_double(const std::string& name, double value) = 0;
    virtual bool set_string(const std::string& name, const std::string& value) = 0;
    virtual bool set_w_string(const std::string& name, const std::wstring& value) = 0;
    virtual bool set_guid(const std::string& name, const guid_t& value) = 0;
};

class table {
public:
    virtual ~table() = default;

    // table meta interface
    virtual uint32_t get_index(const std::string& name) const = 0;
    virtual const std::string& get_name() const = 0;
    virtual uint32_t get_col_count() const = 0;
    virtual data_type get_col_type(const uint32_t index) const = 0;
    virtual const mask_type get_mask() const = 0;
    virtual bool have_mask(const data_mask feature) const = 0;
    virtual bool is_public() const = 0;
    virtual bool is_private() const = 0;
    virtual bool is_real_time() const = 0;
    virtual bool is_save() const = 0;

    virtual uint32_t find_int32(const uint32_t index, const int32_t value) const = 0;
    virtual uint32_t find_int64(const uint32_t index, const int64_t value) const = 0;
    virtual uint32_t find_bool(const uint32_t index, bool value) const = 0;
    virtual uint32_t find_float(const uint32_t index, float value) const = 0;
    virtual uint32_t find_double(const uint32_t index, double value) const = 0;
    virtual uint32_t find_string(const uint32_t index, const std::string& value) const = 0;
    virtual uint32_t find_w_string(const uint32_t index, const std::wstring& value) const = 0;
    virtual uint32_t find_guid(const uint32_t index, const guid_t& value) const = 0;

    virtual row* first() = 0;
    virtual row* next() = 0;

    virtual uint32_t get_index() const = 0;

    // table get
    virtual uint32_t get_row_count() const = 0;

    // table set
    virtual row* add_row(uint32_t row = 0u) = 0;
    virtual row* add_row(uint32_t row, const data_list& args) = 0;
    virtual row* find_row(uint32_t row) const = 0;
    virtual bool remove_row(uint32_t row) = 0;
    virtual void clear() = 0;
};

} // namespace ark
