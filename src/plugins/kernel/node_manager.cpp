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

#include "ark/kernel/data_impl.hpp"
#include "ark/kernel/node_impl.hpp"
#include "ark/kernel/node_manager.hpp"

namespace ark {

node_manager::node_manager(std::shared_ptr<class_meta> class_meta_ptr) {
    class_meta_ = class_meta_ptr;
}

node_manager::node_manager(
    std::shared_ptr<class_meta> class_meta_ptr, const data_list& data_list, node_manager_functor&& func) {
    class_meta_ = class_meta_ptr;
    func_ = std::move(func);

    init_data(data_list);
}

void node_manager::init_data(const data_list& args) {
    size_t count = args.get_count();
    ARK_ASSERT_RET_NONE(count % 2 == 0);

    for (size_t i = 0; i < count; i += 2)
    {
        auto data_type = args.get_type(i);
        auto index = args.u_int(i + 1);
        switch (data_type)
        {
        case ark::data_type::dt_boolean:
        {
            node* node_ptr = find_data(index);
            if (node_ptr != nullptr)
            {
                node_ptr->set_bool(args.get_bool(i));
            }
        }
        break;
        case ark::data_type::dt_int32:
        {
            node* node_ptr = find_data(index);
            if (node_ptr != nullptr)
            {
                node_ptr->set_int32(args.get_int(i));
            }
        }
        break;
        case ark::data_type::dt_uint32:
        {
            node* node_ptr = find_data(index);
            if (node_ptr != nullptr)
            {
                node_ptr->set_u_int32(args.u_int(i));
            }
        }
        break;
        case ark::data_type::dt_int64:
        {
            node* node_ptr = find_data(index);
            if (node_ptr != nullptr)
            {
                node_ptr->set_int64(args.int64(i));
            }
        }
        break;
        case ark::data_type::dt_uint64:
        {
            node* node_ptr = find_data(index);
            if (node_ptr != nullptr)
            {
                node_ptr->set_u_int64(args.u_int64(i));
            }
        }
        break;
        case ark::data_type::dt_float:
        {
            node* node_ptr = find_data(index);
            if (node_ptr != nullptr)
            {
                node_ptr->set_float(args.get_float(i));
            }
        }
        break;
        case ark::data_type::dt_double:
        {
            node* node_ptr = find_data(index);
            if (node_ptr != nullptr)
            {
                node_ptr->set_double(args.get_double(i));
            }
        }
        break;
        case ark::data_type::dt_string:
        {
            node* node_ptr = find_data(index);
            if (node_ptr != nullptr)
            {
                node_ptr->set_string(args.string(i));
            }
        }
        break;
        default:
            break;
        }
    }
}

bool node_manager::is_empty() const {
    return data_list_.size() == 0;
}

// data operation
node* node_manager::create_data(std::shared_ptr<node_meta> data_meta_ptr) {
    ARK_ASSERT_RET_VAL(data_meta_ptr != nullptr, nullptr);

    // has exist
    auto index = data_meta_ptr->get_index();
    ARK_ASSERT_RET_VAL(index > 0, nullptr);
    ARK_ASSERT_RET_VAL(data_list_.find_value(index) == nullptr, nullptr);

    auto data_ptr = create_data_by_meta(data_meta_ptr);
    ARK_ASSERT_RET_VAL(data_ptr != nullptr, nullptr);

    if (!data_list_.insert(index, data_ptr).second)
    {
        ARK_DELETE(data_ptr);
        return nullptr;
    }

    return data_ptr;
}

// this is only called when create new entity so do no need check whether node exist
bool node_manager::create_data(node* data_ptr) {
    ARK_ASSERT_RET_VAL(data_ptr != nullptr, false);

    auto new_data_ptr = create_data_by_meta(data_ptr->get_meta());
    ARK_ASSERT_RET_VAL(new_data_ptr != nullptr, false);

    new_data_ptr->copy_from(data_ptr);

    if (!data_list_.insert(new_data_ptr->get_index(), new_data_ptr).second)
    {
        ARK_DELETE(new_data_ptr);
        return false;
    }

    return true;
}

node* node_manager::create_data(const std::string& name) {
    auto index = get_index(name);
    ARK_ASSERT_RET_VAL(index > 0, nullptr);

    return find_data(index);
}

// get node
node* node_manager::get_node(const std::string& name) const {
    auto index = get_index(name);
    ARK_ASSERT_RET_VAL(index > 0, nullptr);

    return get_node(index);
}

node* node_manager::get_node(const uint32_t index) const {
    return data_list_.find_value(index);
}

// query data
bool node_manager::get_bool(const std::string& name) const {
    auto index = get_index(name);
    ARK_ASSERT_RET_VAL(index > 0, null_boolean);

    return get_bool(index);
}

int32_t node_manager::get_int32(const std::string& name) const {
    auto index = get_index(name);
    ARK_ASSERT_RET_VAL(index > 0, null_int);

    return get_int32(index);
}

uint32_t node_manager::get_u_int32(const std::string& name) const {
    auto index = get_index(name);
    ARK_ASSERT_RET_VAL(index > 0, null_int);

    return get_u_int32(index);
}

int64_t node_manager::get_int64(const std::string& name) const {
    auto index = get_index(name);
    ARK_ASSERT_RET_VAL(index > 0, null_int64);

    return get_int64(index);
}

uint64_t node_manager::get_u_int64(const std::string& name) const {
    auto index = get_index(name);
    ARK_ASSERT_RET_VAL(index > 0, null_int64);

    return get_u_int64(index);
}

float node_manager::get_float(const std::string& name) const {
    auto index = get_index(name);
    ARK_ASSERT_RET_VAL(index > 0, null_float);

    return get_float(index);
}

double node_manager::get_double(const std::string& name) const {
    auto index = get_index(name);
    ARK_ASSERT_RET_VAL(index > 0, null_double);

    return get_double(index);
}

const std::string& node_manager::get_string(const std::string& name) const {
    auto index = get_index(name);
    ARK_ASSERT_RET_VAL(index > 0, null_str);

    return get_string(index);
}

const std::wstring& node_manager::get_w_string(const std::string& name) const {
    auto index = get_index(name);
    ARK_ASSERT_RET_VAL(index > 0, null_widestr);

    return get_w_string(index);
}

const guid_t& node_manager::get_guid(const std::string& name) const {
    auto index = get_index(name);
    ARK_ASSERT_RET_VAL(index > 0, null_guid);

    return get_guid(index);
}

bool node_manager::get_bool(const uint32_t index) const {
    auto data_ptr = find_data(index);
    ARK_ASSERT_RET_VAL(data_ptr != nullptr, null_boolean);

    return data_ptr->get_bool();
}

int32_t node_manager::get_int32(const uint32_t index) const {
    auto data_ptr = find_data(index);
    ARK_ASSERT_RET_VAL(data_ptr != nullptr, null_int);

    return data_ptr->get_int32();
}

uint32_t node_manager::get_u_int32(const uint32_t index) const {
    auto data_ptr = find_data(index);
    ARK_ASSERT_RET_VAL(data_ptr != nullptr, null_int);

    return data_ptr->get_u_int32();
}

int64_t node_manager::get_int64(const uint32_t index) const {
    auto data_ptr = find_data(index);
    ARK_ASSERT_RET_VAL(data_ptr != nullptr, null_int64);

    return data_ptr->get_int64();
}

uint64_t node_manager::get_u_int64(const uint32_t index) const {
    auto data_ptr = find_data(index);
    ARK_ASSERT_RET_VAL(data_ptr != nullptr, null_int64);

    return data_ptr->get_u_int64();
}

float node_manager::get_float(const uint32_t index) const {
    auto data_ptr = find_data(index);
    ARK_ASSERT_RET_VAL(data_ptr != nullptr, null_float);

    return data_ptr->get_float();
}

double node_manager::get_double(const uint32_t index) const {
    auto data_ptr = find_data(index);
    ARK_ASSERT_RET_VAL(data_ptr != nullptr, null_double);

    return data_ptr->get_double();
}

const std::string& node_manager::get_string(const uint32_t index) const {
    auto data_ptr = find_data(index);
    ARK_ASSERT_RET_VAL(data_ptr != nullptr, null_str);

    return data_ptr->get_string();
}

const std::wstring& node_manager::get_w_string(const uint32_t index) const {
    auto data_ptr = find_data(index);
    ARK_ASSERT_RET_VAL(data_ptr != nullptr, null_widestr);

    return data_ptr->get_w_string();
}

const guid_t& node_manager::get_guid(const uint32_t index) const {
    auto data_ptr = find_data(index);
    ARK_ASSERT_RET_VAL(data_ptr != nullptr, null_guid);

    return data_ptr->get_object();
}

// set data
bool node_manager::set_bool(const std::string& name, bool value) {
    auto index = get_index(name);
    ARK_ASSERT_RET_VAL(index > 0, false);

    return set_bool(index, value);
}

bool node_manager::set_int32(const std::string& name, const int32_t value) {
    auto index = get_index(name);
    ARK_ASSERT_RET_VAL(index > 0, false);

    return set_int32(index, value);
}

bool node_manager::set_u_int32(const std::string& name, const uint32_t value) {
    auto index = get_index(name);
    ARK_ASSERT_RET_VAL(index > 0, false);

    return set_u_int32(index, value);
}

bool node_manager::set_int64(const std::string& name, const int64_t value) {
    auto index = get_index(name);
    ARK_ASSERT_RET_VAL(index > 0, false);

    return set_int64(index, value);
}

bool node_manager::set_u_int64(const std::string& name, const uint64_t value) {
    auto index = get_index(name);
    ARK_ASSERT_RET_VAL(index > 0, false);

    return set_u_int64(index, value);
}

bool node_manager::set_float(const std::string& name, const float value) {
    auto index = get_index(name);
    ARK_ASSERT_RET_VAL(index > 0, false);

    return set_float(index, value);
}

bool node_manager::set_double(const std::string& name, const double value) {
    auto index = get_index(name);
    ARK_ASSERT_RET_VAL(index > 0, false);

    return set_double(index, value);
}

bool node_manager::set_string(const std::string& name, const std::string& value) {
    auto index = get_index(name);
    ARK_ASSERT_RET_VAL(index > 0, false);

    return set_string(index, value);
}

bool node_manager::set_w_string(const std::string& name, const std::wstring& value) {
    auto index = get_index(name);
    ARK_ASSERT_RET_VAL(index > 0, false);

    return set_w_string(index, value);
}

bool node_manager::set_guid(const std::string& name, const guid_t& value) {
    auto index = get_index(name);
    ARK_ASSERT_RET_VAL(index > 0, false);

    return set_guid(index, value);
}

bool node_manager::set_bool(const uint32_t index, bool value) {
    auto data_ptr = find_data(index);
    ARK_ASSERT_RET_VAL(data_ptr != nullptr, false);

    auto data_type = data_ptr->get_type();
    ARK_ASSERT_RET_VAL(data_type == data_type::dt_boolean, false);

    auto old_value = data_ptr->get_bool();
    if (old_value == value)
    {
        return false;
    }

    data_ptr->set_bool(value);

    if (func_)
    {
        // data callbacks
        func_(data_ptr, data_impl(data_type, old_value), data_impl(data_type, value));
    }

    return true;
}

bool node_manager::set_int32(const uint32_t index, const int32_t value) {
    auto data_ptr = find_data(index);
    ARK_ASSERT_RET_VAL(data_ptr != nullptr, false);

    auto data_type = data_ptr->get_type();
    ARK_ASSERT_RET_VAL(data_type == data_type::dt_int32, false);

    auto old_value = data_ptr->get_int32();
    if (old_value == value)
    {
        return false;
    }

    data_ptr->set_int32(value);

    if (func_)
    {
        // data callbacks
        func_(data_ptr, data_impl(data_type, old_value), data_impl(data_type, value));
    }

    return true;
}

bool node_manager::set_u_int32(const uint32_t index, const uint32_t value) {
    auto data_ptr = find_data(index);
    ARK_ASSERT_RET_VAL(data_ptr != nullptr, false);

    auto data_type = data_ptr->get_type();
    ARK_ASSERT_RET_VAL(data_type == data_type::dt_uint32, false);

    auto old_value = data_ptr->get_u_int32();
    if (old_value == value)
    {
        return false;
    }

    data_ptr->set_u_int32(value);

    if (func_)
    {
        // data callbacks
        func_(data_ptr, data_impl(data_type, old_value), data_impl(data_type, value));
    }

    return true;
}

bool node_manager::set_int64(const uint32_t index, const int64_t value) {
    auto data_ptr = find_data(index);
    ARK_ASSERT_RET_VAL(data_ptr != nullptr, false);

    auto data_type = data_ptr->get_type();
    ARK_ASSERT_RET_VAL(data_type == data_type::dt_int64, false);

    auto old_value = data_ptr->get_int64();
    if (old_value == value)
    {
        return false;
    }

    data_ptr->set_int64(value);

    if (func_)
    {
        // data callbacks
        func_(data_ptr, data_impl(data_type, old_value), data_impl(data_type, value));
    }

    return true;
}

bool node_manager::set_u_int64(const uint32_t index, const uint64_t value) {
    auto data_ptr = find_data(index);
    ARK_ASSERT_RET_VAL(data_ptr != nullptr, false);

    auto data_type = data_ptr->get_type();
    ARK_ASSERT_RET_VAL(data_type == data_type::dt_uint64, false);

    auto old_value = data_ptr->get_u_int64();
    if (old_value == value)
    {
        return false;
    }

    data_ptr->set_u_int64(value);

    if (func_)
    {
        // data callbacks
        func_(data_ptr, data_impl(data_type, old_value), data_impl(data_type, value));
    }

    return true;
}

bool node_manager::set_float(const uint32_t index, const float value) {
    auto data_ptr = find_data(index);
    ARK_ASSERT_RET_VAL(data_ptr != nullptr, false);

    auto data_type = data_ptr->get_type();
    ARK_ASSERT_RET_VAL(data_type == data_type::dt_float, false);

    auto old_value = data_ptr->get_float();
    if (old_value == value)
    {
        return false;
    }

    data_ptr->set_float(value);

    if (func_)
    {
        // data callbacks
        func_(data_ptr, data_impl(data_type, old_value), data_impl(data_type, value));
    }

    return true;
}

bool node_manager::set_double(const uint32_t index, const double value) {
    auto data_ptr = find_data(index);
    ARK_ASSERT_RET_VAL(data_ptr != nullptr, false);

    auto data_type = data_ptr->get_type();
    ARK_ASSERT_RET_VAL(data_type == data_type::dt_double, false);

    auto old_value = data_ptr->get_double();
    if (old_value == value)
    {
        return false;
    }

    data_ptr->set_double(value);

    if (func_)
    {
        // data callbacks
        func_(data_ptr, data_impl(data_type, old_value), data_impl(data_type, value));
    }

    return true;
}

bool node_manager::set_string(const uint32_t index, const std::string& value) {
    auto data_ptr = find_data(index);
    ARK_ASSERT_RET_VAL(data_ptr != nullptr, false);

    auto data_type = data_ptr->get_type();
    ARK_ASSERT_RET_VAL(data_type == data_type::dt_string, false);

    auto old_value = data_ptr->get_string();
    if (old_value == value)
    {
        return false;
    }

    data_ptr->set_string(value);

    if (func_)
    {
        // data callbacks
        func_(data_ptr, data_impl(data_type, old_value.c_str()), data_impl(data_type, value.c_str()));
    }

    return true;
}

bool node_manager::set_w_string(const uint32_t index, const std::wstring& value) {
    auto data_ptr = find_data(index);

    ARK_ASSERT_RET_VAL(data_ptr != nullptr, false);

    auto data_type = data_ptr->get_type();
    ARK_ASSERT_RET_VAL(data_type == data_type::dt_wstring, false);

    auto old_value = data_ptr->get_w_string();
    if (old_value == value)
    {
        return false;
    }

    data_ptr->set_w_string(value);

    if (func_)
    {
        // data callbacks
        func_(data_ptr, data_impl(data_type, old_value.c_str()), data_impl(data_type, value.c_str()));
    }

    return true;
}

bool node_manager::set_guid(const uint32_t index, const guid_t& value) {
    auto data_ptr = find_data(index);

    ARK_ASSERT_RET_VAL(data_ptr != nullptr, false);

    auto data_type = data_ptr->get_type();
    ARK_ASSERT_RET_VAL(data_type == data_type::dt_guid, false);

    auto old_value = data_ptr->get_object();
    if (old_value == value)
    {
        return false;
    }

    data_ptr->set_object(value);

    if (func_)
    {
        // data callbacks
        func_(data_ptr, data_impl::from_guid(old_value), data_impl::from_guid(value));
    }

    return true;
}

// other query
const node_manager::node_map& node_manager::get_data_list() const {
    return data_list_;
}

uint32_t node_manager::get_index(const std::string& name) const {
    ARK_ASSERT_RET_VAL(class_meta_ != nullptr, null_int);

    return class_meta_->get_index(name);
}

node* node_manager::find_data(const uint32_t index) {
    auto data_ptr = data_list_.find_value(index);
    if (nullptr == data_ptr)
    {
        auto data_meta_ptr = class_meta_->find_data_meta(index);
        ARK_ASSERT_RET_VAL(data_meta_ptr != nullptr, nullptr);

        data_ptr = create_data_by_meta(data_meta_ptr);
        ARK_ASSERT_RET_VAL(data_ptr != nullptr, nullptr);

        if (!data_list_.insert(index, data_ptr).second)
        {
            ARK_DELETE(data_ptr);
            return nullptr;
        }
    }

    return data_ptr;
}

node* node_manager::find_data(const uint32_t index) const {
    return data_list_.find_value(index);
}

} // namespace ark
