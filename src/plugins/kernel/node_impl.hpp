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

#include "ark/kernel/node.hpp"
#include "ark/misc.hpp"

namespace ark {

#define DATA_NODE_DECLARE                                                                                              \
    void set_meta(std::shared_ptr<node_meta> data_meta_ptr) override {                                                 \
        data_meta_ = data_meta_ptr;                                                                                    \
    }                                                                                                                  \
                                                                                                                       \
    std::shared_ptr<node_meta> get_meta() override {                                                                   \
        return data_meta_;                                                                                             \
    }                                                                                                                  \
                                                                                                                       \
    const std::string& get_name() override {                                                                           \
        ARK_ASSERT_RET_VAL(data_meta_ != nullptr, null_str);                                                           \
                                                                                                                       \
        return data_meta_->get_name();                                                                                 \
    }                                                                                                                  \
                                                                                                                       \
    uint32_t get_index() const override {                                                                              \
        ARK_ASSERT_RET_VAL(data_meta_ != nullptr, 0);                                                                  \
                                                                                                                       \
        return data_meta_->get_index();                                                                                \
    }                                                                                                                  \
                                                                                                                       \
    mask_type get_mask() const override {                                                                              \
        ARK_ASSERT_RET_VAL(data_meta_ != nullptr, 0);                                                                  \
                                                                                                                       \
        return data_meta_->get_mask();                                                                                 \
    }                                                                                                                  \
                                                                                                                       \
    bool have_mask(data_mask mask) override {                                                                          \
        ARK_ASSERT_RET_VAL(data_meta_ != nullptr, false);                                                              \
                                                                                                                       \
        return data_meta_->have_mask(mask);                                                                            \
    }                                                                                                                  \
                                                                                                                       \
private:                                                                                                               \
    std::shared_ptr<node_meta> data_meta_{nullptr};

// data int32_t
class node_int32 final : public node {
public:
    explicit node_int32(std::shared_ptr<node_meta> data_meta_ptr) {
        data_meta_ = data_meta_ptr;
    }

    ~node_int32() override = default;

    data_type get_type() const override {
        ARK_ASSERT_RET_VAL(data_meta_ != nullptr, data_type::dt_empty);
        return data_meta_->get_type();
    }

    void reset() override {
        data_ = null_int;
    }

    bool is_null() const override {
        return null_int == data_;
    }

    void copy_from(node* other) override {
        ARK_ASSERT_RET_NONE(data_meta_ != nullptr && other != nullptr);
        ARK_ASSERT_RET_NONE(data_meta_->get_type() == other->get_type());

        set_int32(other->get_int32());
    }

    void save_to(node* other) override {
        ARK_ASSERT_RET_NONE(data_meta_ != nullptr && other != nullptr);
        ARK_ASSERT_RET_NONE(data_meta_->get_type() == other->get_type());

        other->set_int32(get_int32());
    }

    int32_t get_int32() const override {
        return data_;
    }

    void set_int32(int32_t value) override {
        data_ = value;
    }

    std::string to_string() const override {
        return misc::to_string(data_);
    }

    void from_string(const std::string& value) override {
        data_ = misc::from_string<int32_t>(value);
    }

    id_type get_value() const override {
        return static_cast<id_type>(data_);
    }

    DATA_NODE_DECLARE

    // data value
    int32_t data_{null_int};
};

// data uint32_t
class node_u_int32 final : public node {
public:
    explicit node_u_int32(std::shared_ptr<node_meta> data_meta_ptr) {
        data_meta_ = data_meta_ptr;
    }

    ~node_u_int32() override = default;

    data_type get_type() const override {
        ARK_ASSERT_RET_VAL(data_meta_ != nullptr, data_type::dt_empty);
        return data_meta_->get_type();
    }

    void reset() override {
        data_ = null_int;
    }

    bool is_null() const override {
        return null_int == data_;
    }

    void copy_from(node* other) override {
        ARK_ASSERT_RET_NONE(data_meta_ != nullptr && other != nullptr);
        ARK_ASSERT_RET_NONE(data_meta_->get_type() == other->get_type());

        set_u_int32(other->get_u_int32());
    }

    void save_to(node* other) override {
        ARK_ASSERT_RET_NONE(data_meta_ != nullptr && other != nullptr);
        ARK_ASSERT_RET_NONE(data_meta_->get_type() == other->get_type());

        other->set_u_int32(get_u_int32());
    }

    uint32_t get_u_int32() const override {
        return data_;
    }

    void set_u_int32(uint32_t value) override {
        data_ = value;
    }

    std::string to_string() const override {
        return misc::to_string(data_);
    }

    void from_string(const std::string& value) override {
        data_ = misc::from_string<uint32_t>(value);
    }

    id_type get_value() const override {
        return static_cast<id_type>(data_);
    }

    DATA_NODE_DECLARE

    // data value
    uint32_t data_{null_int};
};

// data string
class node_string final : public node {
public:
    explicit node_string(std::shared_ptr<node_meta> data_meta_ptr) {
        data_meta_ = data_meta_ptr;
    }

    ~node_string() override = default;

    data_type get_type() const override {
        ARK_ASSERT_RET_VAL(data_meta_ != nullptr, data_type::dt_empty);
        return data_meta_->get_type();
    }

    void reset() override {
        data_ = null_str;
    }

    bool is_null() const override {
        return data_.empty();
    }

    void copy_from(node* other) override {
        ARK_ASSERT_RET_NONE(data_meta_ != nullptr && other != nullptr);
        ARK_ASSERT_RET_NONE(data_meta_->get_type() == other->get_type());

        set_string(other->get_string());
    }

    void save_to(node* other) override {
        ARK_ASSERT_RET_NONE(data_meta_ != nullptr && other != nullptr);
        ARK_ASSERT_RET_NONE(data_meta_->get_type() == other->get_type());

        other->set_string(get_string());
    }

    const std::string& get_string() const override {
        return data_;
    }

    void set_string(const std::string& value) override {
        data_ = value;
    }

    std::string to_string() const override {
        return data_;
    }

    void from_string(const std::string& value) override {
        data_ = value;
    }

    id_type get_value() const override {
        return misc::from_string<id_type>(data_);
    }

    DATA_NODE_DECLARE

    // data value
    std::string data_{null_str};
};

// data bool
class node_bool final : public node {
public:
    explicit node_bool(std::shared_ptr<node_meta> data_meta_ptr) {
        data_meta_ = data_meta_ptr;
    }

    ~node_bool() override = default;

    data_type get_type() const override {
        ARK_ASSERT_RET_VAL(data_meta_ != nullptr, data_type::dt_empty);
        return data_meta_->get_type();
    }

    void reset() override {
        data_ = null_boolean;
    }

    bool is_null() const override {
        return data_ == null_boolean;
    }

    void copy_from(node* other) override {
        ARK_ASSERT_RET_NONE(data_meta_ != nullptr && other != nullptr);
        ARK_ASSERT_RET_NONE(data_meta_->get_type() == other->get_type());

        set_bool(other->get_bool());
    }

    void save_to(node* other) override {
        ARK_ASSERT_RET_NONE(data_meta_ != nullptr && other != nullptr);
        ARK_ASSERT_RET_NONE(data_meta_->get_type() == other->get_type());

        other->set_bool(get_bool());
    }

    bool get_bool() const override {
        return data_;
    }

    void set_bool(bool value) override {
        data_ = value;
    }

    std::string to_string() const override {
        return misc::to_string(data_);
    }

    void from_string(const std::string& value) override {
        data_ = misc::from_string<bool>(value);
    }

    id_type get_value() const override {
        return static_cast<id_type>(data_);
    }

    DATA_NODE_DECLARE

    // data value
    bool data_{null_boolean};
};

// data int64
class node_int64 final : public node {
public:
    explicit node_int64(std::shared_ptr<node_meta> data_meta_ptr) {
        data_meta_ = data_meta_ptr;
    }

    ~node_int64() override = default;

    data_type get_type() const override {
        ARK_ASSERT_RET_VAL(data_meta_ != nullptr, data_type::dt_empty);
        return data_meta_->get_type();
    }

    void reset() override {
        data_ = null_boolean;
    }

    bool is_null() const override {
        return data_ == null_int;
    }

    void copy_from(node* other) override {
        ARK_ASSERT_RET_NONE(data_meta_ != nullptr && other != nullptr);
        ARK_ASSERT_RET_NONE(data_meta_->get_type() == other->get_type());

        set_int64(other->get_int64());
    }

    void save_to(node* other) override {
        ARK_ASSERT_RET_NONE(data_meta_ != nullptr && other != nullptr);
        ARK_ASSERT_RET_NONE(data_meta_->get_type() == other->get_type());

        other->set_int64(get_int64());
    }

    int64_t get_int64() const override {
        return data_;
    }

    void set_int64(int64_t value) override {
        data_ = value;
    }

    std::string to_string() const override {
        return misc::to_string(data_);
    }

    void from_string(const std::string& value) override {
        data_ = misc::from_string<int64_t>(value);
    }

    id_type get_value() const override {
        return static_cast<id_type>(data_);
    }

    DATA_NODE_DECLARE

    // data value
    int64_t data_{null_int64};
};

// data uint64
class node_u_int64 final : public node {
public:
    explicit node_u_int64(std::shared_ptr<node_meta> data_meta_ptr) {
        data_meta_ = data_meta_ptr;
    }

    ~node_u_int64() override = default;

    data_type get_type() const override {
        ARK_ASSERT_RET_VAL(data_meta_ != nullptr, data_type::dt_empty);
        return data_meta_->get_type();
    }

    void reset() override {
        data_ = null_boolean;
    }

    bool is_null() const override {
        return data_ == null_int;
    }

    void copy_from(node* other) override {
        ARK_ASSERT_RET_NONE(data_meta_ != nullptr && other != nullptr);
        ARK_ASSERT_RET_NONE(data_meta_->get_type() == other->get_type());

        set_u_int64(other->get_u_int64());
    }

    void save_to(node* other) override {
        ARK_ASSERT_RET_NONE(data_meta_ != nullptr && other != nullptr);
        ARK_ASSERT_RET_NONE(data_meta_->get_type() == other->get_type());

        other->set_u_int64(get_u_int64());
    }

    uint64_t get_u_int64() const override {
        return data_;
    }

    void set_u_int64(uint64_t value) override {
        data_ = value;
    }

    std::string to_string() const override {
        return misc::to_string(data_);
    }

    void from_string(const std::string& value) override {
        data_ = misc::from_string<uint64_t>(value);
    }

    id_type get_value() const override {
        return static_cast<id_type>(data_);
    }

    DATA_NODE_DECLARE

    // data value
    uint64_t data_{null_int64};
};

// data float
class node_float final : public node {
public:
    explicit node_float(std::shared_ptr<node_meta> data_meta_ptr) {
        data_meta_ = data_meta_ptr;
    }

    ~node_float() override = default;

    data_type get_type() const override {
        ARK_ASSERT_RET_VAL(data_meta_ != nullptr, data_type::dt_empty);
        return data_meta_->get_type();
    }

    void reset() override {
        data_ = null_boolean;
    }

    bool is_null() const override {
        return data_ == null_int;
    }

    void copy_from(node* other) override {
        ARK_ASSERT_RET_NONE(data_meta_ != nullptr && other != nullptr);
        ARK_ASSERT_RET_NONE(data_meta_->get_type() == other->get_type());

        set_float(other->get_float());
    }

    void save_to(node* other) override {
        ARK_ASSERT_RET_NONE(data_meta_ != nullptr && other != nullptr);
        ARK_ASSERT_RET_NONE(data_meta_->get_type() == other->get_type());

        other->set_float(get_float());
    }

    float get_float() const override {
        return data_;
    }

    void set_float(float value) override {
        data_ = value;
    }

    std::string to_string() const override {
        return misc::to_string(data_);
    }

    void from_string(const std::string& value) override {
        data_ = misc::from_string<float>(value);
    }

    id_type get_value() const override {
        return static_cast<id_type>(data_);
    }

    DATA_NODE_DECLARE

    // data value
    float data_{null_float};
};

// data double
class node_double final : public node {
public:
    explicit node_double(std::shared_ptr<node_meta> data_meta_ptr) {
        data_meta_ = data_meta_ptr;
    }

    ~node_double() override = default;

    data_type get_type() const override {
        ARK_ASSERT_RET_VAL(data_meta_ != nullptr, data_type::dt_empty);
        return data_meta_->get_type();
    }

    void reset() override {
        data_ = null_boolean;
    }

    bool is_null() const override {
        return data_ == null_int;
    }

    void copy_from(node* other) override {
        ARK_ASSERT_RET_NONE(data_meta_ != nullptr && other != nullptr);
        ARK_ASSERT_RET_NONE(data_meta_->get_type() == other->get_type());

        set_double(other->get_double());
    }

    void save_to(node* other) override {
        ARK_ASSERT_RET_NONE(data_meta_ != nullptr && other != nullptr);
        ARK_ASSERT_RET_NONE(data_meta_->get_type() == other->get_type());

        other->set_double(get_double());
    }

    double get_double() const override {
        return data_;
    }

    void set_double(double value) override {
        data_ = value;
    }

    std::string to_string() const override {
        return misc::to_string(data_);
    }

    void from_string(const std::string& value) override {
        data_ = misc::from_string<double>(value);
    }

    id_type get_value() const override {
        return static_cast<id_type>(data_);
    }

    DATA_NODE_DECLARE

    // data value
    double data_{null_double};
};

// data object
class node_guid final : public node {
public:
    explicit node_guid(std::shared_ptr<node_meta> data_meta_ptr) {
        data_meta_ = data_meta_ptr;
    }

    ~node_guid() override = default;

    data_type get_type() const override {
        ARK_ASSERT_RET_VAL(data_meta_ != nullptr, data_type::dt_empty);
        return data_meta_->get_type();
    }

    void reset() override {
        data_ = null_boolean;
    }

    bool is_null() const override {
        return data_ == null_int;
    }

    void copy_from(node* other) override {
        ARK_ASSERT_RET_NONE(data_meta_ != nullptr && other != nullptr);
        ARK_ASSERT_RET_NONE(data_meta_->get_type() == other->get_type());

        set_object(other->get_object());
    }

    void save_to(node* other) override {
        ARK_ASSERT_RET_NONE(data_meta_ != nullptr && other != nullptr);
        ARK_ASSERT_RET_NONE(data_meta_->get_type() == other->get_type());

        other->set_object(get_object());
    }

    const guid_t& get_object() const override {
        return data_;
    }

    void set_object(const guid_t& guid) override {
        data_ = guid;
    }

    std::string to_string() const override {
        return misc::to_string(data_);
    }

    void from_string(const std::string& value) override {
        data_ = misc::from_string<guid_t>(value);
    }

    id_type get_value() const override {
        return static_cast<id_type>(data_);
    }

    DATA_NODE_DECLARE

    // data value
    guid_t data_{null_guid};
};

// create new data
static node* create_data_by_meta(std::shared_ptr<node_meta> data_meta_ptr) {
    ARK_ASSERT_RET_VAL(data_meta_ptr != nullptr, nullptr);

    node* data_ptr = nullptr;
    switch (data_meta_ptr->get_type()) {
    case ark::data_type::dt_boolean:
        data_ptr = ARK_NEW node_bool(data_meta_ptr);
        break;
    case ark::data_type::dt_int32:
        data_ptr = ARK_NEW node_int32(data_meta_ptr);
        break;
    case ark::data_type::dt_uint32:
        data_ptr = ARK_NEW node_u_int32(data_meta_ptr);
        break;
    case ark::data_type::dt_int64:
        data_ptr = ARK_NEW node_int64(data_meta_ptr);
        break;
    case ark::data_type::dt_uint64:
        data_ptr = ARK_NEW node_u_int64(data_meta_ptr);
        break;
    case ark::data_type::dt_float:
        data_ptr = ARK_NEW node_float(data_meta_ptr);
        break;
    case ark::data_type::dt_double:
        data_ptr = ARK_NEW node_double(data_meta_ptr);
        break;
    case ark::data_type::dt_string:
        data_ptr = ARK_NEW node_string(data_meta_ptr);
        break;
    case ark::data_type::dt_vector3d:
        break;
    case ark::data_type::dt_array:
        break;
    case ark::data_type::dt_table:
        break;
    case ark::data_type::dt_guid:
        data_ptr = ARK_NEW node_guid(data_meta_ptr);
        break;
    default:
        break;
    }

    return data_ptr;
}

} // namespace ark
