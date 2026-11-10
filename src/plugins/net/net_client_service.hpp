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

#include "ark/container/consistent_hash.hpp"
#include "ark/container/map.hpp"
#include "ark/net/net.hpp"
#include "ark/noncopyable.hpp"
#include "ark/socket_func.hpp"

namespace ark {

class connection_data {
public:
    enum connect_state {
        disconnect,
        connecting,
        connected,
        reconnect,
    };

    std::shared_ptr<const msg_header> head;
    int server_bus_id{0};
    endpoint remote_endpoint;
    std::shared_ptr<net> net_client{nullptr};

    connect_state net_state{disconnect}; // net state
    int64_t last_active_time{0};
};

class net_client_service : public noncopyable {
public:
    virtual ~net_client_service() = default;

    template<typename base_type>
    bool reg_msg_callback(
        const msg_id_t msg_id, base_type* base_ptr, void (base_type::*handle_recv)(const net_msg*, conv_id_t)) {
        net_msg_functor functor = std::bind_front(handle_recv, base_ptr);
        return reg_msg_callback(msg_id, std::move(functor));
    }

    template<typename base_type>
    bool reg_forward_msg_callback(base_type* base_ptr, void (base_type::*handle_recv)(const net_msg*, conv_id_t)) {
        net_msg_functor functor = std::bind_front(handle_recv, base_ptr);
        return reg_forward_msg_callback(std::move(functor));
    }

    template<typename base_type>
    bool reg_net_event_callback(base_type* base_ptr, void (base_type::*handler)(const net_event*)) {
        net_event_functor functor = std::bind_front(handler, base_ptr);
        return reg_net_event_callback(std::move(functor));
    }

    virtual bool start_client(
        std::shared_ptr<const msg_header> head, bus_id_t target_bus_id, const endpoint& endpoint) = 0;
    virtual void update() = 0;
    //virtual void Shutdown() = 0;

    virtual std::shared_ptr<connection_data> get_connection_info(bus_id_t bus_id) = 0;
    //virtual AFMapEx<int, AFConnectionData>& GetServerList() = 0;

    virtual bool reg_msg_callback(msg_id_t msg_id, const net_msg_functor&& cb) = 0;
    virtual bool reg_forward_msg_callback(const net_msg_functor&& cb) = 0;
    virtual bool reg_net_event_callback(const net_event_functor&& cb) = 0;

    virtual std::shared_ptr<connection_data> get_suitable_connect(const std::string& key) = 0;

    virtual void add_account_bus_id(const std::string& account, bus_id_t bus_id) = 0;
    virtual void remove_account_bus_id(const std::string& account) = 0;
    virtual int get_account_bus_id(const std::string& account) = 0;

    virtual void add_actor_bus_id(const guid_t& actor, bus_id_t bus_id) = 0;
    virtual void remove_actor_bus_id(const guid_t& actor) = 0;
    virtual int get_actor_bus_id(const guid_t& actor) = 0;
};

} // namespace ark
