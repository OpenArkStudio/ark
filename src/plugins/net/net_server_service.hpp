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

#include <functional>

#include "ark/bus.hpp"
#include "ark/net/net.hpp"

namespace ark {

//class AFServerData
//{
//public:
//    void Init(const guid_t& conn_id, const ark::protobuf::msg_ss_server_report& data)
//    {
//        conn_id_ = conn_id;
//        server_info_ = data;
//    }
//
//    guid_t conn_id_{0};
//    ark::protobuf::msg_ss_server_report server_info_;
//};
//
//class AFClientConnectionData
//{
//public:
//    int32_t logic_state_{0};
//    int32_t game_id_{0};
//    guid_t actor_id_{0};
//    guid_t conn_id_{0};
//    std::string account_{};
//};

class net_server_service {
public:
    virtual ~net_server_service() = default;

    template<typename base_type>
    bool reg_msg_callback(
        const msg_id_t msg_id, base_type* base_ptr, void (base_type::*handle_recv)(net_msg const*, conv_id_t)) {
        net_msg_functor functor = std::bind_front(handle_recv, base_ptr);
        return reg_msg_callback(msg_id, std::move(functor));
    }

    template<typename base_type>
    bool reg_forward_msg_callback(base_type* base_ptr, void (base_type::*handle_recv)(net_msg const*, int)) {
        net_msg_functor functor = std::bind_front(handle_recv, base_ptr);
        return reg_forward_msg_callback(std::move(functor));
    }

    template<typename base_type>
    bool reg_net_event_callback(base_type* base_ptr, void (base_type::*handler)(net_event const*)) {
        net_event_functor functor = std::bind_front(handler, base_ptr);
        return reg_net_event_callback(std::move(functor));
    }

    template<typename base_type>
    bool reg_reg_server_call_back(base_type* base_ptr, void (base_type::*handler)(net_msg const*, conv_id_t)) {
        net_msg_session_functor functor = std::bind_front(handler, base_ptr);
        return reg_reg_server_call_back(std::move(functor));
    }

    virtual bool start(std::shared_ptr<const msg_header> head, bus_id_t bus_id, const endpoint& ep,
        uint8_t thread_count, uint32_t max_connection, const size_t silent_timeout) = 0;
    virtual bool update() = 0;

    // virtual bool SendBroadcastMsg(msg_id_t nMsgID, const std::string& msg, const guid_t& player_id) = 0;
    // virtual bool SendBroadcastPBMsg(msg_id_t msg_id, const google::protobuf::Message& pb_msg, const guid_t&
    // player_id) = 0; virtual bool SendPBMsg(msg_id_t msg_id, const google::protobuf::Message& pb_msg, const
    // guid_t& connect_id, const guid_t& player_id, const std::vector<guid_t>* target_list = nullptr) = 0; virtual bool
    // SendMsg(msg_id_t msg_id, const std::string& data, const guid_t& connect_id, const guid_t& player_id, const
    // std::vector<guid_t>* target_list = nullptr) = 0;
    virtual std::shared_ptr<net> get_net() = 0;

    virtual bool reg_msg_callback(msg_id_t msg_id, net_msg_functor&& cb) = 0;
    virtual bool reg_forward_msg_callback(net_msg_functor&& cb) = 0;
    virtual bool reg_net_event_callback(net_event_functor&& cb) = 0;
    virtual bool reg_reg_server_call_back(net_msg_session_functor&& cb) = 0;

    virtual void set_timeout_interval(const int32_t interval) = 0;
};

} // namespace ark
