/*
 * This source file is part of ARK
 * For the latest info, see https://github.com/ArkNX
 *
 * Copyright (c) 2013-2020 ArkNX authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"),
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

#include <cstddef>
#include <limits>

#include "ark/net/net_define.hpp"
#include "ark/noncopyable.hpp"
#include "ark/platform.hpp"

namespace ark {

/**
 * @brief 协议头部接口类，通过它支持任意类型协议头部格式
 */
class msg_header
{
public:
    virtual ~msg_header() {
    }

    // 协议头部必须实现的接口如下

    // 返回包头大小
    virtual uint32_t header_length() const = 0;

    // 把读取到的buffer解析成包头
    virtual void parse_from_array(const void* head_buf) = 0;
    // 把包头序列化到string里
    virtual void serialize_to_string(std::string& buf) = 0;

    virtual msg_id_t message_id() const = 0;
    virtual uint32_t message_length() const = 0;

    // Local protocol policy; never negotiated by a peer's wire header.
    virtual uint32_t max_body_size() const {
        return ark_msg_max_length;
    }

    bool accepts_body_size(std::size_t length) const {
        return length <= max_body_size() && length <= std::numeric_limits<std::size_t>::max() - header_length();
    }

    virtual void set_message_id(msg_id_t) = 0;
    virtual void set_message_length(uint32_t) = 0;

    // 新建包头对象
    virtual msg_header* create() const = 0;
    // 包头深拷贝
    virtual msg_header* clone() const = 0;

    // 以下接口是为了兼容原来的代码

    virtual bool is_compressed() const {
        return false;
    }

    virtual guid_t actor_id() const {
        return 0;
    }

    virtual bus_id_t source_bus_id() const {
        return 0;
    }

    virtual bus_id_t dest_bus_id() const {
        return 0;
    }

    virtual void set_compressed(bool compress) {
    }

    virtual void set_actor_id(guid_t) {
    }

    virtual void set_source_bus_id(bus_id_t) {
    }

    virtual void set_dest_bus_id(bus_id_t) {
    }
};

// CS通信默认包头
class s_msg_header : public msg_header
{
public:
    // 返回包头大小
    virtual uint32_t header_length() const override {
        return sizeof header_;
    }

    // 把读取到的buffer解析成包头
    virtual void parse_from_array(const void* head_buf) override {
        const header_impl* bb = reinterpret_cast<const header_impl*>(head_buf);
        const char* cc = reinterpret_cast<const char*>(head_buf);
        memcpy(&header_, head_buf, sizeof header_);
    }

    // 把包头序列化到string里
    virtual void serialize_to_string(std::string& buf) override {
        buf.append(reinterpret_cast<char*>(&header_), sizeof header_);
    }

    virtual msg_id_t message_id() const override {
        return header_.id;
    }

    virtual uint32_t message_length() const override {
        return header_.length;
    }

    virtual void set_message_id(msg_id_t id) override {
        header_.id = id;
    }

    virtual void set_message_length(uint32_t msg_len) override {
        header_.length = msg_len;
    }

    // 新建包头对象
    virtual msg_header* create() const override {
        return ARK_NEW s_msg_header();
    }

    // 包头深拷贝
    virtual msg_header* clone() const override {
        return ARK_NEW s_msg_header(*this);
    }

    virtual bool is_compressed() const override {
        return header_.compress;
    }

    virtual void set_compressed(bool compress) override {
        header_.compress = compress;
    }

private:
#pragma pack(push, 1)

    struct header_impl
    {
        msg_id_t id{0};      // Msg id
        uint32_t length{0};  // Msg length(without header length)
        uint8_t compress{0}; // Msg data compressed
    };

#pragma pack(pop)

private:
    header_impl header_;
};

// SS通信默认包头
class ss_msg_header : public msg_header
{
public:
    // 返回包头大小
    virtual uint32_t header_length() const override {
        return sizeof header_;
    }

    // 把读取到的buffer解析成包头
    virtual void parse_from_array(const void* head_buf) override {
        memcpy(&header_, head_buf, sizeof header_);
    }

    // 把包头序列化到string里
    virtual void serialize_to_string(std::string& buf) override {
        buf.append(reinterpret_cast<char*>(&header_), sizeof header_);
    }

    virtual msg_id_t message_id() const override {
        return header_.id;
    }

    virtual uint32_t message_length() const override {
        return header_.length;
    }

    virtual void set_message_id(msg_id_t id) override {
        header_.id = id;
    }

    virtual void set_message_length(uint32_t msg_len) override {
        header_.length = msg_len;
    }

    // 新建包头对象
    virtual msg_header* create() const override {
        return ARK_NEW ss_msg_header();
    }

    // 包头深拷贝
    virtual msg_header* clone() const override {
        return ARK_NEW ss_msg_header(*this);
    }

    virtual bool is_compressed() const override {
        return header_.compress;
    }

    virtual guid_t actor_id() const override {
        return header_.actor_id;
    }

    virtual bus_id_t source_bus_id() const override {
        return header_.src_bus;
    }

    virtual bus_id_t dest_bus_id() const override {
        return header_.dst_bus;
    }

    virtual void set_compressed(bool compress) override {
        header_.compress = compress;
    }

    virtual void set_actor_id(guid_t actor_id) override {
        header_.actor_id = actor_id;
    }

    virtual void set_source_bus_id(bus_id_t src_bus) override {
        header_.src_bus = src_bus;
    }

    virtual void set_dest_bus_id(bus_id_t dst_bus) override {
        header_.dst_bus = dst_bus;
    }

private:
#pragma pack(push, 1)

    struct header_impl
    {
        msg_id_t id{0};      // Msg id
        uint32_t length{0};  // Msg length(without header length)
        uint8_t compress{0}; // Msg data compressed
        guid_t actor_id{0};  // Actor id
        bus_id_t src_bus{0}; // Source bus id
        bus_id_t dst_bus{0}; // Destination bus id
    };

#pragma pack(pop)

private:
    header_impl header_;
};

class net_msg final : private noncopyable
{
protected:
    net_msg(msg_header* head) {
        head_ = head;
        msg_data_ = nullptr;
        alloc_data(head_->message_length());
    }

    ~net_msg() {
        delete head_;
        delete[] msg_data_;
    }

public:
    static net_msg* alloc_msg(msg_header* head) {
        return ARK_NEW net_msg(head);
    }

    static void release(net_msg*& msg) {
        ARK_DELETE(msg);
    }

    void copy_data(const char* data, const uint32_t len) {
        ARK_ASSERT_RET_NONE(data != nullptr);

        set_msg_length(len);
        memcpy(this->msg_data_, data, len);
    }

    void copy_from(net_msg* msg) {
        ARK_ASSERT_RET_NONE(msg != nullptr);

        delete head_;
        head_ = msg->head_->clone();
        copy_data(msg->msg_data_, msg->get_msg_length());
    }

    msg_id_t get_msg_id() const {
        return head_->message_id();
    }

    uint32_t get_msg_length() const {
        return head_->message_length();
    }

    uint8_t get_compress() const {
        return head_->is_compressed();
    }

    guid_t get_actor_id() const {
        return head_->actor_id();
    }

    bus_id_t get_src_bus() const {
        return head_->source_bus_id();
    }

    bus_id_t get_dst_bus() const {
        return head_->dest_bus_id();
    }

    void set_msg_id(msg_id_t value) {
        head_->set_message_id(value);
    }

    void set_msg_length(uint32_t value) {
        head_->set_message_length(value);
    }

    void set_actor_id(guid_t value) {
        head_->set_actor_id(value);
    }

    void set_src_bus(bus_id_t value) {
        head_->set_source_bus_id(value);
    }

    void set_dst_bus(bus_id_t value) {
        head_->set_dest_bus_id(value);
    }

    char* get_msg_data() const {
        return msg_data_;
    }

    bool is_ss_msg() const {
        return head_->source_bus_id() != 0;
    }

    msg_header& get_head() {
        return *head_;
    }

private:
    void alloc_data(uint32_t len) {
        if (len > 0)
        {
            msg_data_ = ARK_NEW char[len];
        }
    }

    void dealloc_data() {
        if (msg_data_ != nullptr)
        {
            ARK_DELETE_ARRAY(char, msg_data_);
        }

        head_->set_message_length(0);
    }

private:
    msg_header* head_;
    char* msg_data_{nullptr};
};

} // namespace ark
