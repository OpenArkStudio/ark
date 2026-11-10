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

#include "ark/kernel/node_meta.hpp"
#include "ark/platform.hpp"

namespace ark {

class node {
public:
    virtual ~node() = default;

    virtual data_type get_type() const = 0;
    virtual void reset() = 0;
    virtual bool is_null() const = 0;
    virtual void copy_from(node* other) = 0;
    virtual void save_to(node* other) = 0;
    virtual bool have_mask(data_mask mask) = 0;
    virtual mask_type get_mask() const = 0;
    virtual id_type get_value() const = 0;

    // Get data
    virtual bool get_bool() const {
        return null_boolean;
    };

    virtual int32_t get_int32() const {
        return null_int;
    }

    virtual uint32_t get_u_int32() const {
        return null_int;
    }

    virtual int64_t get_int64() const {
        return null_int64;
    }

    virtual uint64_t get_u_int64() const {
        return null_int64;
    }

    virtual float get_float() const {
        return null_float;
    }

    virtual double get_double() const {
        return null_double;
    }

    virtual const std::string& get_string() const {
        return null_str;
    }

    virtual const std::wstring& get_w_string() const {
        return null_widestr;
    }

    virtual const guid_t& get_object() const {
        return null_guid;
    }

    virtual void* get_pointer() const {
        return nullptr;
    }

    virtual const void* get_user_data(size_t& size) const {
        return nullptr;
    }

    virtual void* get_raw_user_data() const {
        return nullptr;
    }

    // Set data
    virtual void set_unknown() {
    }

    virtual void set_bool(bool value) {
    }

    virtual void set_int64(int64_t value) {
    }

    virtual void set_u_int64(uint64_t value) {
    }

    virtual void set_float(float value) {
    }

    virtual void set_double(double value) {
    }

    virtual void set_string(const std::string& value) {
    }

    virtual void set_pointer(void* value) {
    }

    virtual void set_user_data(const void* value, size_t size) {
    }

    virtual void set_raw_user_data(void* value) {
    }

    virtual void set_int32(int32_t value) {
    }

    virtual void set_u_int32(uint32_t value) {
    }

    virtual void set_w_string(const std::wstring& value) {
    }

    virtual void set_object(const guid_t& guid) {
    }

    // other handle
    virtual void assign(const data& src) {
    }

    virtual size_t get_mem_usage() const {
        return 0;
    }

    virtual std::string to_string() const {
        return null_str;
    }

    virtual void from_string(const std::string& value) {
    }

    // set meta
    virtual void set_meta(std::shared_ptr<node_meta> data_meta_ptr) = 0;
    virtual std::shared_ptr<node_meta> get_meta() = 0;
    virtual const std::string& get_name() = 0;
    virtual uint32_t get_index() const = 0;
};

} // namespace ark
