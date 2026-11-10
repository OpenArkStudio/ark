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

#include "ark/bus/msg_module.hpp"
#include "ark/container/map.hpp"
#include "ark/net/net_server_service.hpp"
#include "ark/net/net_service_manager_module.hpp"
#include "ark/plugin_manager.hpp"

namespace ark {

class connection {
public:
    enum class connect_state : uint8_t {
        disconnect = 0,
        connecting,
        connected,
        reconnect,
    };

    connection() = default;

    connection(bus_id_t bus_id, conv_id_t session_id, int64_t last_active_time, connect_state connect_state)
        : bus_id(bus_id)
        , session_id(session_id)
        , last_active_time(last_active_time)
        , state(connect_state) {
    }

    bus_id_t bus_id{0};
    conv_id_t session_id{0};
    int64_t last_active_time{0};
    connect_state state{connect_state::disconnect};
};

class net_server_service_impl final : public net_server_service {
public:
    explicit net_server_service_impl(plugin_manager* p);

    ~net_server_service_impl() override;
    void shutdown();

    bool start(std::shared_ptr<const msg_header> head, bus_id_t bus_id, const endpoint& ep, uint8_t thread_count,
        uint32_t max_connection, const size_t silent_timeout) override;
    bool update() override;

    std::shared_ptr<net> get_net() override;

    bool reg_msg_callback(msg_id_t msg_id, net_msg_functor&& cb) override;
    bool reg_forward_msg_callback(net_msg_functor&& cb) override;
    bool reg_net_event_callback(net_event_functor&& cb) override;

    // RegServer msg callback
    bool reg_reg_server_call_back(net_msg_session_functor&& cb) override;

    void set_timeout_interval(const int32_t interval) override;

protected:
    void on_net_msg(const net_msg* msg, conv_id_t session_id);
    void on_net_event(const net_event* event);
    void on_disconnect_net_event(const net_event* event);

    //void ProcessConnection();
    void process_client_connection();

    void add_connection(bus_id_t bus_id, conv_id_t session_id);
    void update_connection(bus_id_t bus_id);
    void remove_connection(bus_id_t bus_id);

    void update_client_connection(conv_id_t session_id);

    inline void remove_client_connection(conv_id_t session_id) {
        auto iter = client_connection_list_.find(session_id);
        if (iter == client_connection_list_.end()) {
            return;
        }

        client_connection_list_.erase(iter);
    }

    //void OnClientRegister(const AFNetMsg* msg, conv_id_t session_id);
    //void SyncToAllClient(bus_id_t bus_id, conv_id_t session_id);

private:
    // Owner-thread transport dispatch must retire before the service object.
    std::shared_ptr<int> callback_lifetime_ = std::make_shared<int>();
    bool closed_ = false;
    plugin_manager* plugin_manager_;
    net_service_manager_module* net_service_manager_;
    msg_module* msg_module_;

    std::shared_ptr<net> network_{nullptr};

    net_msg_session_functor reg_server_call_back_{nullptr};

    std::map<msg_id_t, net_msg_functor> net_msg_callbacks_;
    std::list<net_msg_functor> net_forward_msg_callbacks_;
    std::list<net_event_functor> net_event_callbacks_;

    using connection_map = shared_object_map<bus_id_t, connection>;
    connection_map connection_list_;
    //AFMapEx<int, AFServerData> reg_clients_;

    using client_connection_map = std::map<conv_id_t, connection>;
    client_connection_map client_connection_list_;
    int32_t client_timeout_interval_{std::chrono::milliseconds(ark_net_heart_time).count()};
};

} // namespace ark
