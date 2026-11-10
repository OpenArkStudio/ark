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

#include "ark/buffer.hpp"
#include "ark/container/lock_free_queue.hpp"
#include "ark/define.hpp"
#include "ark/macros.hpp"
#include "ark/net/net_define.hpp"
#include "ark/net/net_event.hpp"
#include "ark/net/net_msg.hpp"

namespace ark {

class net;

using net_msg_functor = std::function<void(const net_msg*, conv_id_t)>;
using net_msg_session_functor = std::function<void(net_msg const*, conv_id_t)>;
using net_event_functor = std::function<void(net_event const*)>;

class net {
public:
    virtual ~net() = default;

    // need to call this function every frame to drive network frame
    virtual void update() = 0;

    virtual bool start_client(
        std::shared_ptr<const msg_header> head, bus_id_t dst_bus_id, const std::string& host, uint16_t port) {
        return false;
    }

    virtual bool start_server(std::shared_ptr<const msg_header> head, bus_id_t bus_id, const std::string& host,
        uint16_t port, uint8_t thread_num, uint32_t max_client, const size_t silent_timeout) {
        return false;
    }

    virtual void shutdown() = 0;

    virtual bool send_msg(msg_header* head, const char* msg_data, conv_id_t session_id) = 0;

    virtual void broadcast_msg(msg_header* head, const char* msg_data) {
    }

    virtual void close_session(conv_id_t session_id) = 0;

    // Configure before start_client/start_server. Existing sessions keep their capacity.
    bool set_initial_receive_buffer_size(size_t bytes) {
        if (bytes == 0 || is_working()) {
            return false;
        }
        initial_receive_buffer_size_ = bytes;
        return true;
    }

    size_t get_initial_receive_buffer_size() const {
        return initial_receive_buffer_size_;
    }

    bool is_working() const {
        return working_;
    }

    void set_working(bool value) {
        working_ = value;
    }

    virtual std::string get_ip(const conv_id_t session_id) {
        return null_str;
    };

private:
    bool working_{false};
    size_t initial_receive_buffer_size_{default_initial_receive_buffer_size};

public:
    // TODO: use this to statistic net traffic.
    size_t statistic_recv_size{0};
    size_t statistic_send_size{0};
};

} // namespace ark
