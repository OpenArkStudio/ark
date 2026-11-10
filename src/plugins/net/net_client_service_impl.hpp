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

#include "ark/bus/bus_module.hpp"
#include "ark/bus/msg_module.hpp"
#include "ark/container/consistent_hash.hpp"
#include "ark/container/map.hpp"
#include "ark/net/net_client_service.hpp"
#include "ark/net/net_service_manager_module.hpp"
#include "ark/plugin_manager.hpp"

namespace ark {

class net_client_service_impl final : public net_client_service {
public:
    enum constant_definitions {
        default_weight = 255,
    };

    net_client_service_impl(plugin_manager* p);

    ~net_client_service_impl() override;
    void shutdown();

    bool start_client(
        std::shared_ptr<const msg_header> head, bus_id_t target_bus_id, const endpoint& endpoint) override;
    void update() override;
    //void Shutdown() override;

    bool reg_msg_callback(msg_id_t msg_id, const net_msg_functor&& cb) override;
    bool reg_forward_msg_callback(const net_msg_functor&& cb) override;
    bool reg_net_event_callback(const net_event_functor&& cb) override;

    std::shared_ptr<connection_data> get_connection_info(bus_id_t bus_id) override;
    //AFMapEx<int, AFConnectionData>& GetServerList() override;

    std::shared_ptr<connection_data> get_suitable_connect(const std::string& key) override;

    void add_account_bus_id(const std::string& account, bus_id_t bus_id) override;
    void remove_account_bus_id(const std::string& account) override;
    int get_account_bus_id(const std::string& account) override;

    void add_actor_bus_id(const guid_t& actor, const bus_id_t bus_id) override;
    void remove_actor_bus_id(const guid_t& actor) override;
    int get_actor_bus_id(const guid_t& actor) override;

protected:
    void process_update();
    void process_add_connection();

    std::shared_ptr<net> create_net(const proto_type proto);

    //void RegisterToServer(conv_id_t session_id, bus_id_t bus_id);
    int on_connect(const net_event* event);
    int on_disconnect(const net_event* event);

    void on_net_msg(const net_msg* msg, conv_id_t session_id);
    void on_net_event(const net_event* event);

    void keep_report(std::shared_ptr<connection_data>& connection_data) {
        // TODO
    }

    void log_server_info(const std::string& server_info) {
        // TODO
    }

    void log_server_info();
    void keep_alive(std::shared_ptr<connection_data> server_data_ptr);

    //////////////////////////////////////////////////////////////////////////
    //bool GetServerMachineData(const std::string& strServerID, AFCMachineNode& xMachineData);
    //void AddServerWeightData(std::shared_ptr<AFConnectionData>& xInfo);

    //void RemoveServerWeightData(std::shared_ptr<AFConnectionData>& xInfo);
    //////////////////////////////////////////////////////////////////////////
    bool add_server_node(std::shared_ptr<connection_data> data);
    bool get_server_node(const std::string& hash_key, v_node& vnode);
    void remove_server_node(std::shared_ptr<connection_data> data);

    void reg_to_server(const msg_header& head, bus_id_t target_bus_id);
    void send_report(std::shared_ptr<connection_data> server_data_ptr);

private:
    // Owner-thread transport dispatch must retire before the service object.
    std::shared_ptr<int> callback_lifetime_ = std::make_shared<int>();
    bool closed_ = false;
    plugin_manager* plugin_manager_;
    net_service_manager_module* net_service_manager_;
    bus_module* bus_module_;
    msg_module* msg_module_;

    // Connected connections(may the ConnectState is different)
    shared_object_map<bus_id_t, connection_data> real_connections_;

    std::map<guid_t, bus_id_t> actor_bus_map_;                  // actor id bus id
    std::unordered_map<std::string, bus_id_t> account_bus_map_; // account bus id

    // the new consistent hash map
    consistent_hashmap_type consistent_hashmap_;

    std::list<connection_data> tmp_connections_;

    std::map<msg_id_t, net_msg_functor> net_msg_callbacks_;
    std::list<net_event_functor> net_event_callbacks_;

    // [NOT USE now] - forward to other processes
    std::list<net_msg_functor> net_msg_forward_callbacks_;
};

} // namespace ark
