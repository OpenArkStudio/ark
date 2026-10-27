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

#include "ark/event_define.hpp"
#include "ark/kernel/data_list_impl.hpp"
#include "game_module_factory.hpp"
#include "game_net_module_impl.hpp"

namespace ark {

std::unique_ptr<game_net_module> detail::make_game_net_module() {
    return std::make_unique<game_net_module_impl>();
}

bool game_net_module_impl::init() {
    kernel_module_ = get_plugin_manager()->try_find_module<kernel_module>();
    uuid_module_ = get_plugin_manager()->try_find_module<guid_module>();
    bus_module_ = get_plugin_manager()->try_find_module<bus_module>();
    msg_module_ = get_plugin_manager()->try_find_module<msg_module>();
    net_service_manager_module_ = get_plugin_manager()->try_find_module<net_service_manager_module>();
    map_module_ = get_plugin_manager()->try_find_module<map_module>();

    if (bus_module_ == nullptr || net_service_manager_module_ == nullptr || kernel_module_ == nullptr)
    {
        ARK_LOG_ERROR("game network startup requires Bus, Net and Kernel modules");
        return false;
    }

    kernel_module_->add_common_class_event(this, &game_net_module_impl::on_common_class_event);

    //m_pKernelModule->AddNodeCallBack(AFEntityMetaPlayer::self_name(), AFEntityMetaBaseEntity::map_inst_id(), this,
    //    &AFCGameNetModule::OnMapInstanceEvent);
    //m_pKernelModule->AddNodeCallBack(
    //    AFEntityMetaPlayer::self_name(), AFEntityMetaBaseEntity::map_id(), this, &AFCGameNetModule::OnContainerEvent);

    kernel_module_->add_leave_scene_event(entity_meta_player::self_name(), this, &game_net_module_impl::on_leave_scene);
    kernel_module_->add_enter_scene_event(entity_meta_player::self_name(), this, &game_net_module_impl::on_enter_scene);

    kernel_module_->add_class_call_back(entity_meta_player::self_name(), this, &game_net_module_impl::on_entity_event);

    return true;
}

bool game_net_module_impl::post_init() {
    return start_server();
}

bool game_net_module_impl::start_server() {
    auto ret = net_service_manager_module_->create_server();
    if (!ret)
    {
        ARK_LOG_ERROR("Cannot start server net, busid = {}", bus_module_->get_self_bus_name());
        return false;
    }

    net_server_service_ = net_service_manager_module_->get_inter_net_server();
    if (net_server_service_ == nullptr)
    {
        ARK_LOG_ERROR("Cannot find server net, busid = {}", bus_module_->get_self_bus_name());
        return false;
    }

    return true;
}

//bool AFCGameNetModule::PreUpdate()
//{
//    int ret = StartClient();
//    return (ret == 0);
//}
//
//int AFCGameNetModule::StartClient()
//{
//    //create all clients
//    // Create all target endpoint clients
//    int ret = m_pNetServiceManagerModule->CreateClusterClients();
//    if (ret != 0)
//    {
//        ARK_LOG_ERROR("Cannot start server net, busid = {}, error = {}", m_pBusModule->GetSelfBusName(), ret);
//        ARK_ASSERT_NO_EFFECT(0);
//        return ret;
//    }
//
//    // if need to add a member
//    AFINetClientService* pNetClient = m_pNetServiceManagerModule->GetNetClientService(ARK_APP_TYPE::ARK_APP_WORLD);
//    if (pNetClient == nullptr)
//    {
//        return -1;
//    }
//
//    // TODO:will add a transfer callback
//    // pNetClient->RegMsgCallback(this, &AFCGameNetClientModule::TransPBToProxy);
//
//    return 0;
//}

int game_net_module_impl::on_view_data_node_enter(const data_list& arg_var, const guid_t& self) {
    if (arg_var.get_count() <= 0 || self == 0)
    {
        return 1;
    }

    std::shared_ptr<entity> entity_ptr = kernel_module_->get_entity(self);
    if (nullptr == entity_ptr)
    {
        return 0;
    }

    ark::protobuf::multi_entity_data_list msg;
    ark::protobuf::protobuf_entity* entity = msg.add_data_list();
    entity->set_id(self);

    mask_type mask;
    mask[(size_t)data_mask::pf_sync_view] = 1;
    kernel_module_->entity_node_to_protobuf_data(entity_ptr, entity->mutable_data(), mask);

    for (size_t i = 0; i < arg_var.get_count(); i++)
    {
        guid_t ident_other = arg_var.int64(i);

        if (self != ident_other)
        {
            send_msg_protobuf_to_gate(ark::protobuf::egmi_ack_entity_data_node_enter, msg, ident_other);
        }
    }

    return 0;
}

int game_net_module_impl::on_self_data_node_enter(const guid_t& self) {
    if (self == 0)
    {
        return 1;
    }

    std::shared_ptr<entity> entity_ptr = kernel_module_->get_entity(self);
    if (nullptr == entity_ptr)
    {
        return 1;
    }

    ark::protobuf::multi_entity_data_list msg;
    ark::protobuf::protobuf_entity* entity = msg.add_data_list();
    entity->set_id(self);

    mask_type mask;
    mask[(size_t)data_mask::pf_sync_self] = 1;
    kernel_module_->entity_node_to_protobuf_data(entity_ptr, entity->mutable_data(), mask);

    send_msg_protobuf_to_gate(ark::protobuf::egmi_ack_entity_data_node_enter, msg, self);
    return 0;
}

int game_net_module_impl::on_self_data_table_enter(const guid_t& self) {
    if (self == 0)
    {
        return 1;
    }

    std::shared_ptr<entity> entity_ptr = kernel_module_->get_entity(self);

    if (nullptr == entity_ptr)
    {
        return 1;
    }

    ark::protobuf::multi_entity_data_list msg;
    ark::protobuf::protobuf_entity* entity = msg.add_data_list();
    entity->set_id(self);

    mask_type mask;
    mask[(size_t)data_mask::pf_sync_self] = 1;
    kernel_module_->entity_table_to_protobuf_data(entity_ptr, entity->mutable_data(), mask);

    send_msg_protobuf_to_gate(ark::protobuf::egmi_ack_entity_data_table_enter, msg, self);
    return 0;
}

int game_net_module_impl::on_view_data_table_enter(const data_list& arg_var, const guid_t& self) {
    if (arg_var.get_count() <= 0 || self == 0)
    {
        return 1;
    }

    std::shared_ptr<entity> entity_ptr = kernel_module_->get_entity(self);

    if (nullptr == entity_ptr)
    {
        return 1;
    }

    ark::protobuf::multi_entity_data_list msg;
    ark::protobuf::protobuf_entity* entity = msg.add_data_list();
    entity->set_id(self);

    mask_type mask;
    mask[(size_t)data_mask::pf_sync_view] = 1;
    kernel_module_->entity_table_to_protobuf_data(entity_ptr, entity->mutable_data(), mask);

    for (size_t i = 0; i < arg_var.get_count(); i++)
    {
        guid_t ident_other = arg_var.int64(i);

        if (self != ident_other && msg.data_list_size() > 0)
        {
            send_msg_protobuf_to_gate(ark::protobuf::egmi_ack_entity_data_table_enter, msg, ident_other);
        }
    }

    return 0;
}

bool game_net_module_impl::process_leave_group(const guid_t& self, int map_id, int old_inst_id) {
    if (old_inst_id <= 0)
    {
        return false;
    }

    data_list_impl value_all_old_object_list;
    data_list_impl value_all_old_player_list;
    map_module_->get_inst_entity_list(map_id, old_inst_id, value_all_old_object_list);

    if (value_all_old_object_list.get_count() > 0)
    {
        //broadcast self to others around
        for (size_t i = 0; i < value_all_old_object_list.get_count(); i++)
        {
            guid_t ident_bc = value_all_old_object_list.int64(i);
            auto entity_ptr = kernel_module_->get_entity(ident_bc);
            if (entity_ptr == nullptr)
            {
                continue;
            }

            if (value_all_old_object_list.int64(i) == self)
            {
                value_all_old_object_list.set_int64(i, 0);
            }

            const std::string& class_name = entity_ptr->get_class_name();
            if (entity_meta_player::self_name() == class_name)
            {
                value_all_old_player_list << ident_bc;
            }
        }

        on_entity_list_leave(value_all_old_player_list, data_list_impl() << self);

        //broadcast self that leave the map
        on_entity_list_leave(data_list_impl() << self, value_all_old_object_list);
    }

    kernel_module_->do_event(self, afed_on_client_leave_scene, data_list_impl() << old_inst_id);

    return true;
}

bool game_net_module_impl::process_enter_group(const guid_t& self, int scene_id, int new_group_id) {
    // broadcast self enter to others
    if (new_group_id <= 0)
    {
        return false;
    }

    //exclude myself
    //////////////////////////////////////////////////////////////////////////
    data_list_impl value_all_object_list;
    data_list_impl value_all_object_list_no_self;
    data_list_impl value_player_list;
    data_list_impl value_player_list_no_self;
    map_module_->get_inst_entity_list(scene_id, new_group_id, value_all_object_list);

    for (size_t i = 0; i < value_all_object_list.get_count(); i++)
    {
        guid_t ident_bc = value_all_object_list.int64(i);
        auto entity_ptr = kernel_module_->get_entity(ident_bc);
        if (entity_ptr == nullptr)
        {
            continue;
        }

        const std::string& class_name = entity_ptr->get_class_name();
        if (entity_meta_player::self_name() == class_name)
        {
            value_player_list << ident_bc;

            if (ident_bc != self)
            {
                value_player_list_no_self << ident_bc;
            }
        }

        if (ident_bc != self)
        {
            value_all_object_list_no_self << ident_bc;
        }
    }

    // broadcast others that I'm here(need exclude self)
    if (value_player_list_no_self.get_count() > 0)
    {
        auto entity_ptr = kernel_module_->get_entity(self);
        if (entity_ptr == nullptr)
        {
            return false;
        }

        on_entity_list_enter(value_player_list_no_self, data_list_impl() << self);
        on_view_data_node_enter(value_player_list_no_self, self);
        on_view_data_table_enter(value_player_list_no_self, self);

        const std::string class_name = entity_ptr->get_class_name();
        if (class_name == entity_meta_player::self_name())
        {
            on_entity_list_enter(data_list_impl() << self, value_all_object_list_no_self);

            for (size_t i = 0; i < value_all_object_list_no_self.get_count(); i++)
            {
                // broadcast myself data to others
                guid_t ident_old = value_all_object_list_no_self.int64(i);

                on_view_data_node_enter(data_list_impl() << self, ident_old);
                on_view_data_table_enter(data_list_impl() << self, ident_old);
            }
        }
    }

    return true;
}

int game_net_module_impl::on_entity_list_enter(const data_list& self, const data_list& arg_var) {
    if (self.get_count() <= 0 || arg_var.get_count() <= 0)
    {
        return 1;
    }

    ark::protobuf::ack_entity_enter_list entity_enter_list_value;

    for (size_t i = 0; i < arg_var.get_count(); i++)
    {
        guid_t ident_old = arg_var.int64(i);

        // Exclude empty entity
        if (ident_old == 0)
        {
            continue;
        }

        auto entity_ptr = kernel_module_->get_entity(ident_old);
        if (entity_ptr == nullptr)
        {
            continue;
        }

        ark::protobuf::entity_enter_info* enter_info_ptr = entity_enter_list_value.add_entity_list();
        enter_info_ptr->set_object_guid(ident_old);
        vector3d point_value;
        point_value.x = entity_ptr->get_float("x");
        point_value.y = entity_ptr->get_float("y");
        point_value.z = entity_ptr->get_float("z");

        *enter_info_ptr->mutable_pos() = msg_module::vec_to_protobuf(point_value);
        enter_info_ptr->set_career_type(entity_ptr->get_int32("Job"));
        enter_info_ptr->set_player_state(entity_ptr->get_int32("State"));
        enter_info_ptr->set_config_id(entity_ptr->get_config_id());
        enter_info_ptr->set_scene_id(entity_ptr->get_map_id());
        enter_info_ptr->set_class_id(entity_ptr->get_class_name());
    }

    if (entity_enter_list_value.entity_list_size() <= 0)
    {
        return 1;
    }

    for (size_t i = 0; i < self.get_count(); i++)
    {
        guid_t ident = self.int64(i);

        if (ident != 0)
        {
            send_msg_protobuf_to_gate(ark::protobuf::egmi_ack_entity_enter, entity_enter_list_value, ident);
        }
    }

    return 0;
}

int game_net_module_impl::on_entity_list_leave(const data_list& self, const data_list& arg_var) {
    if (self.get_count() <= 0 || arg_var.get_count() <= 0)
    {
        return 1;
    }

    ark::protobuf::ack_entity_leave_list entity_leave_info_list_value;

    for (size_t i = 0; i < arg_var.get_count(); i++)
    {
        guid_t ident_old = arg_var.int64(i);

        // Exclude empty entity
        if (ident_old != 0)
        {
            entity_leave_info_list_value.add_entity_list(ident_old);
        }
    }

    for (size_t i = 0; i < self.get_count(); i++)
    {
        guid_t ident = self.int64(i);

        if (ident != 0)
        {
            send_msg_protobuf_to_gate(ark::protobuf::egmi_ack_entity_leave, entity_leave_info_list_value, ident);
        }
    }

    return 0;
}

int game_net_module_impl::common_class_destory_event(const guid_t& self) {
    auto entity_ptr = kernel_module_->get_entity(self);
    if (nullptr == entity_ptr)
    {
        return 0;
    }

    int map_id = entity_ptr->get_int32(entity_meta_base_entity::map_id());
    int map_inst_id = entity_ptr->get_int32(entity_meta_base_entity::map_inst_id());

    if (map_inst_id < 0)
    {
        return 0;
    }

    data_list_impl value_all_object_list;
    data_list_impl value_broad_case_list;
    data_list_impl value_broad_list_no_self;
    map_module_->get_inst_entity_list(map_id, map_inst_id, value_all_object_list);

    for (size_t i = 0; i < value_all_object_list.get_count(); i++)
    {
        guid_t ident_bc = value_all_object_list.int64(i);
        auto bc_entity_ptr = kernel_module_->get_entity(ident_bc);
        if (nullptr == bc_entity_ptr)
        {
            continue;
        }

        const std::string& class_name = bc_entity_ptr->get_class_name();

        if (entity_meta_player::self_name() == class_name)
        {
            value_broad_case_list << ident_bc;

            if (ident_bc != self)
            {
                value_broad_list_no_self << ident_bc;
            }
        }
    }

    on_entity_list_leave(value_broad_list_no_self, data_list_impl() << self);
    return 0;
}

int game_net_module_impl::on_common_class_event(
    const guid_t& self, const std::string& class_name, const entity_event class_event, const data_list& var) {
    switch (class_event)
    {
    case entity_event::entity_evt_destroy:
        common_class_destory_event(self);
        break;

    case entity_event::entity_evt_pre_load_data:
    {
        // do something
    }
    break;

    case entity_event::entity_evt_load_data:
        break;

    case entity_event::entity_evt_data_finished:
    {
        // Just broadcast to self
        if (class_name == entity_meta_player::self_name())
        {
            on_entity_list_enter(data_list_impl() << self, data_list_impl() << self);

            on_self_data_node_enter(self);
            on_self_data_table_enter(self);
        }
    }
    break;

    case entity_event::entity_evt_all_finished:
        break;

    default:
        break;
    }

    return 0;
}

//int AFCGameNetModule::OnMapInstanceEvent(
//    const guid_t& self, const std::string& name, const uint32_t index, const AFIData& oldVar, const AFIData& newVar)
//{
//    // When the map changes, should be map A instance 0 to map B instance 0,
//    auto pEntity = m_pKernelModule->GetEntity(self);
//    if (pEntity == nullptr)
//    {
//        return 0;
//    }
//
//    int nSceneID = pEntity->GetInt32(AFEntityMetaBaseEntity::map_id());
//
//    // broadcast myself leaving to others
//    int nOldGroupID = oldVar.GetInt();
//    ProcessLeaveGroup(self, nSceneID, nOldGroupID);
//
//    int nNewGroupID = newVar.GetInt();
//    ProcessEnterGroup(self, nSceneID, nNewGroupID);
//
//    return 0;
//}
//
//int AFCGameNetModule::OnContainerEvent(
//    const guid_t& self, const std::string& name, const uint32_t index, const AFIData& oldVar, const AFIData& newVar)
//{
//    int nOldSceneID = oldVar.GetInt();
//    int nNowSceneID = newVar.GetInt();
//
//    ARK_LOG_INFO("Enter Scene, id  = {} scene = {}", self, nNowSceneID);
//
//    AFCDataList valueOldAllObjectList;
//    AFCDataList valueNewAllObjectList;
//    AFCDataList valueAllObjectListNoSelf;
//    AFCDataList valuePlayerList;
//    AFCDataList valuePlayerNoSelf;
//
//    m_pMapModule->GetInstEntityList(nOldSceneID, 0, valueOldAllObjectList);
//    m_pMapModule->GetInstEntityList(nNowSceneID, 0, valueNewAllObjectList);
//
//    for (size_t i = 0; i < valueOldAllObjectList.GetCount(); i++)
//    {
//        guid_t identBC = valueOldAllObjectList.Int64(i);
//
//        if (identBC == self)
//        {
//            valueOldAllObjectList.SetInt64(i, 0);
//        }
//    }
//
//    for (size_t i = 0; i < valueNewAllObjectList.GetCount(); i++)
//    {
//        guid_t identBC = valueNewAllObjectList.Int64(i);
//        auto pBCEntity = m_pKernelModule->GetEntity(identBC);
//        if (pBCEntity == nullptr)
//        {
//            continue;
//        }
//
//        const std::string& class_name = pBCEntity->GetClassName();
//
//        if (AFEntityMetaPlayer::self_name() == class_name)
//        {
//            valuePlayerList << identBC;
//
//            if (identBC != self)
//            {
//                valuePlayerNoSelf << identBC;
//            }
//        }
//
//        if (identBC != self)
//        {
//            valueAllObjectListNoSelf << identBC;
//        }
//    }
//
//    //////////////////////////////////////////////////////////////////////////
//
//    OnEntityListLeave(AFCDataList() << self, valueOldAllObjectList);
//
//    // the instance is 0
//    if (valuePlayerList.GetCount() > 0)
//    {
//        OnEntityListEnter(valuePlayerNoSelf, AFCDataList() << self);
//    }
//
//    OnEntityListEnter(AFCDataList() << self, valueAllObjectListNoSelf);
//
//    for (size_t i = 0; i < valueAllObjectListNoSelf.GetCount(); i++)
//    {
//        guid_t identOld = valueAllObjectListNoSelf.Int64(i);
//        OnViewDataNodeEnter(AFCDataList() << self, identOld);
//        OnViewDataTableEnter(AFCDataList() << self, identOld);
//    }
//
//    if (valuePlayerNoSelf.GetCount() > 0)
//    {
//        OnViewDataNodeEnter(valuePlayerNoSelf, self);
//        OnViewDataTableEnter(valuePlayerNoSelf, self);
//    }
//
//    return 0;
//}

int game_net_module_impl::on_leave_scene(const guid_t& self, const int map_id, const int map_inst_id) {
    process_leave_group(self, map_id, map_inst_id);
    return 0;
}

int game_net_module_impl::on_enter_scene(const guid_t& self, const int map_id, const int map_inst_id) {
    process_enter_group(self, map_id, map_inst_id);
    return 0;
}

int game_net_module_impl::get_broadcast_entity_list(const int map_id, const int inst_id, data_list& value_object) {
    data_list_impl map_inst_entity_list;
    map_module_->get_inst_entity_list(map_id, inst_id, map_inst_entity_list);

    for (size_t i = 0; i < map_inst_entity_list.get_count(); i++)
    {
        auto entity_ptr = kernel_module_->get_entity(map_inst_entity_list.int64(i));
        if (entity_ptr == nullptr)
        {
            continue;
        }

        const std::string& obj_class_name = entity_ptr->get_class_name();

        if (entity_meta_player::self_name() == obj_class_name)
        {
            value_object.add_int64(map_inst_entity_list.int64(i));
        }
    }

    return value_object.get_count();
}

int game_net_module_impl::on_entity_event(
    const guid_t& self, const std::string& class_name, const entity_event class_event, const data_list& var) {
    switch (class_event)
    {
    case entity_event::entity_evt_destroy:
        // save data to db by yourself logic
        ARK_LOG_INFO("Player Offline, player_id = {}", self);
        break;

    case entity_event::entity_evt_load_data:
        // load data from db by yourself logic
        ARK_LOG_INFO("Player online, player_id = {}", self);
        break;

    case entity_event::entity_evt_all_finished:
        // load data finished from db by yourself logic
        ARK_LOG_INFO("Player ready, player_id = {}", self);
        break;

    default:
        break;
    }

    return 0;
}

void game_net_module_impl::send_msg_protobuf_to_gate(
    const uint16_t msg_id, google::protobuf::Message& msg_value, const guid_t& self) {
    //std::shared_ptr<GateBaseInfo> pData = mRoleBaseData.find_value(self);

    //if (nullptr == pData)
    //{
    //    return;
    //}

    // std::shared_ptr<GateServerInfo> pProxyData = mProxyMap.GetElement(pData->nGateID);

    // if (nullptr != pProxyData)
    //{
    //    m_pNetServerService->SendPBMsg(nMsgID, xMsg, pProxyData->xServerData.conn_id_, pData->xClientID);
    //}
}

void game_net_module_impl::send_msg_protobuf_to_gate(
    const uint16_t msg_id, const std::string& msg, const guid_t& self) {
    //std::shared_ptr<GateBaseInfo> pData = mRoleBaseData.find_value(self);

    //if (nullptr == pData)
    //{
    //    return;
    //}

    // std::shared_ptr<GateServerInfo> pProxyData = mProxyMap.GetElement(pData->nGateID);

    // if (nullptr != pProxyData)
    //{
    //    m_pNetServerService->SendMsg(nMsgID, strMsg, pProxyData->xServerData.conn_id_, pData->xClientID);
    //}
}

std::shared_ptr<net_server_service> game_net_module_impl::get_net_server_service() {
    return net_server_service_;
}

bool game_net_module_impl::add_player_gate_info(const guid_t& role_id, const guid_t& client_id, const int gate_id) {
    //if (nGateID <= 0)
    //{
    //    return false;
    //}

    //if (nClientID == 0)
    //{
    //    return false;
    //}

    //std::shared_ptr<AFCGameNetModule::GateBaseInfo> pBaseData = mRoleBaseData.find_value(nRoleID);

    //if (nullptr != pBaseData)
    //{
    //    ARK_LOG_ERROR("player is already exist, cannot enter game again, id = {}", nClientID);
    //    return false;
    //}

    //std::shared_ptr<GateServerInfo> pServerData = mProxyMap.find_value(nGateID);

    //if (nullptr == pServerData)
    //{
    //    return false;
    //}

    //if (!pServerData->xRoleInfo.insert(std::make_pair(nRoleID, pServerData->xServerData.conn_id_)).second)
    //{
    //    return false;
    //}

    //if (!mRoleBaseData.insert(nRoleID, std::make_shared<GateBaseInfo>(nGateID, nClientID)).second)
    //{
    //    pServerData->xRoleInfo.erase(nRoleID);
    //    return false;
    //}

    return true;
}

//bool AFCGameNetModule::AddPlayerGate(const guid_t& actor_id, const int gate_bus)
//{
//    if (actor_id == NULL_GUID)
//    {
//        ARK_LOG_ERROR("player does not exist, acrot id is null");
//        return false;
//    }
//
//    auto iter = mActorIDBusIDData.find(actor_id);
//    if (iter != mActorIDBusIDData.end())
//    {
//        ARK_LOG_ERROR("player is already exist, cannot enter game again, actor id = {}", actor_id);
//        return false;
//    }
//
//    return mActorIDBusIDData.insert(std::make_pair(actor_id, gate_bus)).second;
//}
//
//bool AFCGameNetModule::RemovePlayerGate(const guid_t& actor_id)
//{
//    mActorIDBusIDData.erase(actor_id);
//    return true;
//}

bool game_net_module_impl::remove_player_gate_info(const guid_t& role_id) {
    //std::shared_ptr<GateBaseInfo> pBaseData = mRoleBaseData.find_value(nRoleID);

    //if (nullptr == pBaseData)
    //{
    //    return false;
    //}

    //mRoleBaseData.erase(nRoleID);

    //std::shared_ptr<GateServerInfo> pServerData = mProxyMap.find_value(pBaseData->nGateID);

    //if (nullptr == pServerData)
    //{
    //    return false;
    //}

    //pServerData->xRoleInfo.erase(nRoleID);
    return true;
}

//std::shared_ptr<AFIGameNetModule::GateBaseInfo> AFCGameNetModule::GetPlayerGateInfo(const guid_t& nRoleID)
//{
//    return mRoleBaseData.find_value(nRoleID);
//}

//std::shared_ptr<AFIGameNetModule::GateServerInfo> AFCGameNetModule::GetGateServerInfo(const int nGateID)
//{
//    return mProxyMap.find_value(nGateID);
//}
//
//std::shared_ptr<AFIGameNetModule::GateServerInfo> AFCGameNetModule::GetGateServerInfoByClientID(const guid_t& nClientID)
//{
//    int nGateID = -1;
//    for (auto iter : mProxyMap)
//    {
//        auto pServerData = iter.second;
//        if (nClientID == pServerData->xServerData.conn_id_)
//        {
//            nGateID = pServerData->xServerData.server_info_.bus_id();
//            if (nGateID != -1)
//            {
//                return pServerData;
//            }
//        }
//    }
//
//    return nullptr;
//}

} // namespace ark
