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

#include <atomic>
#include <memory>

#include <arknet/http/http_session.hpp>
#include <arknet/rudp/rudp_session.hpp>
#include <arknet/tcp/tcp_session.hpp>
#include <arknet/websocket/ws_session.hpp>

#include "ark/buffer.hpp"
#include "ark/container/lock_free_queue.hpp"
#include "ark/lock/rw_lock.hpp"
#include "ark/macros.hpp"
#include "ark/net/net_define.hpp"
#include "ark/net/net_event.hpp"
#include "ark/net/net_msg.hpp"
#include "ark/noncopyable.hpp"

namespace ark {

template<typename session_ptr_type, bool is_session>
class net_session : public noncopyable
{
public:
    explicit net_session(std::shared_ptr<const msg_header> req_head, conv_id_t session_id,
        const session_ptr_type session_ptr = nullptr,
        size_t initial_receive_buffer_size = default_initial_receive_buffer_size)
        : req_header_(req_head)
        , session_id_(session_id)
        , buffer_(initial_receive_buffer_size)
        , session_(session_ptr) {
    }

    virtual ~net_session() {
        release_queue_data();
    }

    void release_queue_data() {
        net_event* event = nullptr;
        while (pop_net_event(event))
        {
            net_event::release(event);
        }

        net_msg* msg = nullptr;
        while (pop_net_msg(msg))
        {
            net_msg::release(msg);
        }
    }

    const session_ptr_type get_session() {
        if constexpr (is_session)
        {
            ARK_ASSERT_RET_VAL(session_ != nullptr, nullptr);
            return session_;
        }
        else
        {
            ARK_ASSERT_NO_EFFECT(0);
            return nullptr;
        }
    }

    int add_buffer(const char* data, size_t len) {
        buffer_.write(data, len);
        return (int)buffer_.get_length();
    }

    int add_buffer(std::string const& data) {
        buffer_.write(data.data(), data.length());
        return (int)buffer_.get_length();
    }

    size_t remove_buffer(size_t len) {
        if (len > buffer_.get_length())
        {
            return 0;
        }

        buffer_.remove_data(len);
        if (len > 0)
        {
            pending_header_.reset();
        }
        return buffer_.get_length();
    }

    char* get_buffer() {
        return buffer_.get_data();
    }

    size_t get_buffer_capacity() {
        return buffer_.get_capacity();
    }

    size_t get_buffer_len() {
        return buffer_.get_length();
    }

    uint32_t get_head_len() {
        return req_header_->header_length();
    }

    conv_id_t const& get_session_id() {
        return session_id_;
    }

    void set_session_id(conv_id_t const& id) {
        session_id_ = id;
    }

    bool need_remove() {
        return need_remove_.load();
    }

    void need_remove(bool value) {
        need_remove_.store(value);
    }

    bool add_net_event(net_event*& event) {
        return event_queue_.push(event);
    }

    bool pop_net_event(net_event*& event) {
        return event_queue_.pop(event);
    }

    bool add_net_msg(net_msg*& msg) {
        return msg_queue_.push(msg);
    }

    bool pop_net_msg(net_msg*& msg) {
        return msg_queue_.pop(msg);
    }

    bool parse_buffer_to_msg() {
        size_t pos = 0;
        const size_t head_len = get_head_len();
        if (head_len == 0)
        {
            return false;
        }
        while (get_buffer_len() - pos >= head_len)
        {
            auto msg_head = std::move(pending_header_);
            if (msg_head == nullptr)
            {
                msg_head.reset(req_header_->create());
                msg_head->parse_from_array(get_buffer() + pos);
            }
            const size_t msg_len = msg_head->message_length();
            if (!req_header_->accepts_body_size(msg_len))
            {
                return false;
            }
            if (get_buffer_len() - pos - head_len < msg_len)
            {
                pending_header_ = std::move(msg_head);
                break;
            }

            net_msg* msg = net_msg::alloc_msg(msg_head.release());
            pos += head_len;
            if (msg_len > 0)
            {
                msg->copy_data(get_buffer() + pos, msg_len);
                pos += msg_len;
            }

            if (!add_net_msg(msg))
            {
                net_msg::release(msg);
                return false;
            }
        }

        // remove [0, pos] buffers
        if (pos > 0)
        {
            remove_buffer(pos);
        }
        return true;
    }

private:
    std::shared_ptr<const msg_header> req_header_;
    conv_id_t session_id_{0};
    buffer buffer_;

    lock_free_queue<net_msg*> msg_queue_;
    lock_free_queue<net_event*> event_queue_;
    // this field is meaning when isSession is true
    const session_ptr_type session_{nullptr};
    std::unique_ptr<msg_header> pending_header_;

    std::atomic<bool> need_remove_{false};
};

/// server-side
using tcp_session = net_session<std::shared_ptr<arknet::tcp_session>, true>;
using tcp_session_ptr = std::shared_ptr<tcp_session>;

using udp_session = net_session<std::shared_ptr<arknet::rudp_session>, true>;
using udp_session_ptr = std::shared_ptr<udp_session>;

using ws_session = net_session<std::shared_ptr<arknet::ws_session>, true>;
using ws_session_ptr = std::shared_ptr<ws_session>;

using http_session = net_session<std::shared_ptr<arknet::http_session>, true>;
using http_session_ptr = std::shared_ptr<http_session>;

/// client-side
using tcp_client_session = net_session<std::shared_ptr<arknet::tcp_session>, false>;
using tcp_client_session_ptr = std::shared_ptr<tcp_client_session>;

using rdp_client_session = net_session<std::shared_ptr<arknet::rudp_session>, false>;
using rdp_client_session_ptr = std::shared_ptr<rdp_client_session>;

using ws_client_session = net_session<std::shared_ptr<arknet::ws_session>, false>;
using ws_client_session_ptr = std::shared_ptr<ws_client_session>;

using http_client_session = net_session<std::shared_ptr<arknet::http_session>, false>;
using http_client_session_ptr = std::shared_ptr<http_client_session>;

} // namespace ark
