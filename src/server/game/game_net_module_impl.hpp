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
#include "ark/kernel/kernel_module.hpp"
#include "ark/kernel/map_module.hpp"
#include "ark/module_base.hpp"
#include "ark/net/net_service_manager_module.hpp"
#include "ark/proto.hpp"
#include "ark/utility/guid_module.hpp"
#include "game_net_module.hpp"

namespace ark {

class game_net_module_impl final : public module_base<game_net_module>
{
public:
    bool init() override;
    bool post_init() override;
    //bool PreUpdate() override;

    void send_msg_protobuf_to_gate(
        const uint16_t msg_id, google::protobuf::Message& msg_value, const guid_t& self) override;
    void send_msg_protobuf_to_gate(const uint16_t msg_id, const std::string& msg, const guid_t& self) override;
    std::shared_ptr<net_server_service> get_net_server_service() override;

    bool add_player_gate_info(const guid_t& role_id, const guid_t& client_id, const int gate_id) override;
    bool remove_player_gate_info(const guid_t& role_id) override;
    //std::shared_ptr<GateBaseInfo> GetPlayerGateInfo(const guid_t& nRoleID) override;

    //virtual std::shared_ptr<GateServerInfo> GetGateServerInfo(const int nGateID);
    //virtual std::shared_ptr<GateServerInfo> GetGateServerInfoByClientID(const guid_t& nClientID);

    virtual int on_view_data_node_enter(const data_list& arg_var, const guid_t& self);
    virtual int on_self_data_node_enter(const guid_t& self);
    virtual int on_view_data_table_enter(const data_list& arg_var, const guid_t& self);
    virtual int on_self_data_table_enter(const guid_t& self);

    virtual int on_entity_list_enter(const data_list& self, const data_list& arg_var);
    virtual int on_entity_list_leave(const data_list& self, const data_list& arg_var);

    // void OnSocketEvent(const AFNetEvent* event);
    // void OnClientDisconnect(const guid_t& xClientID);
    // void OnClientConnected(const guid_t& xClientID);

    // Common Node & table & class event
    int on_common_class_event(
        const guid_t& self, const std::string& class_name, const entity_event class_event, const data_list& var);

    //int OnMapInstanceEvent(const guid_t& self, const std::string& name, const uint32_t index, const AFIData& oldVar,
    //    const AFIData& newVar);
    //int OnContainerEvent(const guid_t& self, const std::string& name, const uint32_t index, const AFIData& oldVar,
    //    const AFIData& newVar);

    int on_leave_scene(const guid_t& self, const int map_id, const int map_inst_id);
    int on_enter_scene(const guid_t& self, const int map_id, const int map_inst_id);

    int on_entity_event(
        const guid_t& self, const std::string& class_name, const entity_event class_event, const data_list& var);

    int get_broadcast_entity_list(const int object_container_id, const int group_id, data_list& value_object);

    bool process_leave_group(const guid_t& self, int scene_id, int old_group_id);
    bool process_enter_group(const guid_t& self, int scene_id, int new_group_id);

protected:
    bool start_server();
    //int StartClient();

    int common_class_destory_event(const guid_t& self);

private:
    //<PlayerID, GateBaseInfo>//其实可以在object系统中被代替
    // AFSmartPtrMap<guid_t, GateBaseInfo> mRoleBaseData;
    // gate id,data
    // AFSmartPtrMap<int, GateServerInfo> mProxyMap;
    //////////////////////////////////////////////////////////////////////////
    guid_module* uuid_module_;
    kernel_module* kernel_module_;
    bus_module* bus_module_;
    net_service_manager_module* net_service_manager_module_;
    map_module* map_module_;

    msg_module* msg_module_;

    std::shared_ptr<net_server_service> net_server_service_;
};

} // namespace ark
