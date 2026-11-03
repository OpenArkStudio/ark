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
#include <map>
#include <memory>

#include <arknet/tcp/tcp_server.hpp>

#include "ark/net/net.hpp"
#include "ark/net/net_session.hpp"
#include "ark/uid_generator.hpp"

namespace ark {

class tcp_server final
    : public net
    , public noncopyable
    , public std::enable_shared_from_this<tcp_server>
{
public:
    tcp_server(net_msg_functor message_callback, net_event_functor event_callback)
        : net_msg_cb_(std::move(message_callback))
        , net_event_cb_(std::move(event_callback)) {
    }

    template<typename base_type>
    tcp_server(base_type* base_type_ptr, void (base_type::*handle_recv)(net_msg const*, conv_id_t),
        void (base_type::*handle_event)(net_event const*)) {
        net_msg_cb_ = std::bind_front(handle_recv, base_type_ptr);
        net_event_cb_ = std::bind_front(handle_event, base_type_ptr);
    }

    ~tcp_server() override;

    void update() override;

    bool start_server(std::shared_ptr<const msg_header> head, bus_id_t bus_id, const std::string& ip, uint16_t port,
        uint8_t thread_num, uint32_t max_client, const size_t silent_timeout) override;
    void shutdown() override final;

    bool send_msg(msg_header* head, const char* msg_data, conv_id_t session_id) override;
    void broadcast_msg(msg_header* head, const char* msg_data) override;

    void close_session(conv_id_t session_id) override;

    std::string get_ip(const conv_id_t session_id) override;

protected:
    void send_msg_to_all_client(const char* msg, size_t msg_len);
    bool send_msg(const char* msg, size_t msg_len, conv_id_t session_id);

    bool add_net_session(const tcp_session_ptr& session);
    tcp_session_ptr get_net_session(conv_id_t session_id);
    void close_session(tcp_session_ptr& session);

    void update_net_session();
    bool update_net_event(const tcp_session_ptr& session);
    void update_net_msg(const tcp_session_ptr& session);

    void close_all_session();

private:
    friend struct net_listener_startup_test_access;

    std::map<conv_id_t, tcp_session_ptr> sessions_;
    size_t active_session_count_{0};
    reader_writer_lock rw_lock_;
    bus_id_t bus_id_{0};

    net_msg_session_functor net_msg_cb_;
    net_event_functor net_event_cb_;

    std::atomic<conv_id_t> trusted_session_id_{ark_conv_start_id}; // 1000 means a readable number.
    std::shared_ptr<arknet::tcp_server> server_;
};

} // namespace ark
