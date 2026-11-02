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

#include <deque>
#include <functional>
#include <memory>

#include <arknet/rudp/rudp_client.hpp>

#include "ark/net/net.hpp"
#include "ark/net/net_define.hpp"
#include "ark/net/net_session.hpp"
#include "ark/platform.hpp"

namespace ark {

class rudp_client final
    : public net
    , public std::enable_shared_from_this<rudp_client>
    , private noncopyable

{
private:
    // data
    bus_id_t dst_bus_id_{0};
    reader_writer_lock rw_lock_;

    net_msg_functor net_msg_cb_;
    net_event_functor net_event_cb_;

    std::atomic<conv_id_t> trust_session_id_{ark_conv_start_id};

    rdp_client_session_ptr session_;
    std::deque<rdp_client_session_ptr> sessions_;
    std::shared_ptr<arknet::rudp_client> client_;

public:
    rudp_client(net_msg_functor message_callback, net_event_functor event_callback)
        : net_msg_cb_(std::move(message_callback))
        , net_event_cb_(std::move(event_callback)) {
    }

    template<typename base_type>
    rudp_client(base_type* base_type_ptr, void (base_type::*handle_recv)(net_msg const*, conv_id_t),
        void (base_type::*handle_event)(net_event const*)) {
        net_msg_cb_ = std::bind_front(handle_recv, base_type_ptr);
        net_event_cb_ = std::bind_front(handle_event, base_type_ptr);
    }

    ~rudp_client() override;

    void update() override;
    bool start_client(
        std::shared_ptr<const msg_header>, bus_id_t dst_busid, const std::string& host, uint16_t port) override;

    void shutdown() override;

    bool send_msg(msg_header* head, const char* msg_data, conv_id_t session_id) override;
    void close_session(conv_id_t session_id) override;

protected:
    void update_net_event(const rdp_client_session_ptr& session);
    bool update_net_msg(const rdp_client_session_ptr& session);
};

} // namespace ark
