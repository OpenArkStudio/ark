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
#include <functional>
#include <memory>
#include <vector>

#include <arknet/tcp/tcp_client.hpp>

#include "ark/net/net.hpp"
#include "ark/net/net_session.hpp"

namespace ark {

class tcp_client final
    : public net
    , public noncopyable
    , public std::enable_shared_from_this<tcp_client>
{
public:
    tcp_client(net_msg_functor message_callback, net_event_functor event_callback)
        : net_msg_cb_(std::move(message_callback))
        , net_event_cb_(std::move(event_callback)) {
    }

    template<typename base_type>
    tcp_client(base_type* base_type_ptr, void (base_type::*handle_recv)(net_msg const*, conv_id_t),
        void (base_type::*handle_event)(net_event const*)) {
        net_msg_cb_ = std::bind_front(handle_recv, base_type_ptr);
        net_event_cb_ = std::bind_front(handle_event, base_type_ptr);
    }

    ~tcp_client() override;

    void update() override;

    bool start_client(
        std::shared_ptr<const msg_header> head, bus_id_t dst_bus_id, const std::string& host, uint16_t port) override;

    void shutdown() final;
    bool send_msg(msg_header* head, const char* msg_data, conv_id_t session_id) override;

    void close_session(conv_id_t session_id) override;

protected:
    bool send_msg(const char* msg, size_t msg_len);

    void update_net_session();
    void update_net_event(const tcp_client_session_ptr& session);
    void update_net_msg(const tcp_client_session_ptr& session);

private:
    bus_id_t dst_bus_id_{0};

    net_msg_functor net_msg_cb_;
    net_event_functor net_event_cb_;
    reader_writer_lock rw_lock_;

    std::atomic<conv_id_t> trust_session_id_{1000};

    tcp_client_session_ptr session_{nullptr};
    std::vector<tcp_client_session_ptr> retired_sessions_;
    std::shared_ptr<arknet::tcp_client> client_;
};

} // namespace ark
