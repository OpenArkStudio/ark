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

#include "world_module_factory.hpp"
#include "world_net_module_impl.hpp"

namespace ark {

std::unique_ptr<world_net_module> detail::make_world_net_module() {
    return std::make_unique<world_net_module_impl>();
}

bool world_net_module_impl::init() {
    last_check_time_ = get_plugin_manager()->get_now_time();

    kernel_module_ = get_plugin_manager()->try_find_module<kernel_module>();
    bus_module_ = get_plugin_manager()->try_find_module<bus_module>();
    msg_module_ = get_plugin_manager()->try_find_module<msg_module>();
    net_service_manager_module_ = get_plugin_manager()->try_find_module<net_service_manager_module>();

    if (bus_module_ == nullptr || net_service_manager_module_ == nullptr)
    {
        ARK_LOG_ERROR("world network startup requires Bus and Net modules");
        return false;
    }

    return true;
}

bool world_net_module_impl::post_init() {
    return start_server();
}

bool world_net_module_impl::start_server() {
    auto ret = net_service_manager_module_->create_server();
    if (!ret)
    {
        ARK_LOG_ERROR("Cannot start server net, busid = {}", bus_module_->get_self_bus_name());
        return false;
    }

    net_server_ = net_service_manager_module_->get_inter_net_server();
    if (net_server_ == nullptr)
    {
        ARK_LOG_ERROR("Cannot find server net, busid = {}", bus_module_->get_self_bus_name());
        return false;
    }

    return true;
}

//bool AFCWorldNetModule::PreUpdate()
//{
//    int ret = StartClient();
//    return (ret == 0);
//}
//
//int AFCWorldNetModule::StartClient()
//{
//    int ret = m_pNetServiceManagerModule->CreateClusterClients();
//    if (ret != 0)
//    {
//        ARK_LOG_ERROR("Cannot start server net, busid = {}, error = {}", m_pBusModule->GetSelfBusName(), ret);
//        ARK_ASSERT_NO_EFFECT(0);
//        return ret;
//    }
//
//    // if need to add a member
//    AFINetClientService* pNetClientWorld =
//        m_pNetServiceManagerModule->GetNetClientService(ARK_APP_TYPE::ARK_APP_MASTER);
//    if (pNetClientWorld == nullptr)
//    {
//        return 0;
//    }
//
//    // pNetClientWorld->RegMsgCallback(ark::protobuf::EGMI_REQ_CONNECT_WORLD, this,
//    // &AFCWorldNetClientModule::OnSelectServerProcess);
//    // pNetClientWorld->RegMsgCallback(ark::protobuf::EGMI_REQ_KICK_CLIENT_INWORLD, this,
//    // &AFCWorldNetClientModule::OnKickClientProcess); pNetClientWorld->RegMsgCallback(this,
//    // &AFCWorldNetClientModule::InvalidMessage);
//
//    // pNetClientWorld->AddNetEventCallBack(this, &AFCWorldNetClientModule::OnSocketEvent);
//
//    return 0;
//}

// void AFCWorldNetServerModule::OnSocketEvent(const NetEventType event, const guid_t& conn_id, const std::string& ip,
// bus_id_t bus_id)
//{
//    switch (event)
//    {
//    case CONNECTED:
//        {
//            ARK_LOG_INFO("Connected success, id = {}", conn_id.ToString());
//            OnClientConnected(conn_id);
//        }
//        break;
//    case DISCONNECTED:
//        {
//            ARK_LOG_INFO("Connection closed, id = {}", conn_id.ToString());
//            OnClientDisconnect(conn_id);
//        }
//        break;
//    default:
//        break;
//    }
//}

// void AFCWorldNetServerModule::OnClientDisconnect(const guid_t& xClientID)
//{
//    //for (std::shared_ptr<AFServerData> pServerData = mGameMap.First(); nullptr != pServerData; pServerData =
//    mGameMap.Next())
//    //{
//    //    if (xClientID == pServerData->xClient)
//    //    {
//    //        pServerData->xData.set_logic_status(ark::protobuf::est_crash);
//    //        pServerData->xClient = 0;

//    //        return;
//    //    }
//    //}
//    ////////////////////////////////////////////////////////////////////////////
//    //int nServerID = 0;
//    //for (std::shared_ptr<AFServerData> pServerData = mProxyMap.First(); pServerData != nullptr; pServerData =
//    mProxyMap.Next())
//    //{
//    //    if (xClientID == pServerData->xClient)
//    //    {
//    //        nServerID = pServerData->xData.bus_id();
//    //        break;
//    //    }
//    //}

//    //mProxyMap.RemoveElement(nServerID);
//}

// void AFCWorldNetServerModule::OnClientConnected(const guid_t& xClientID)
//{
//    //log
//}

bool world_net_module_impl::send_msg_to_game(const int game_id, const ark::protobuf::game_message_id msg_id,
    google::protobuf::Message& data_value, const guid_t player) {
    // std::shared_ptr<AFServerData> pData = mGameMap.GetElement(nGameID);

    // if (nullptr != pData)
    //{
    //    m_pNetServer->SendPBMsg(eMsgID, xData, pData->xClient, nPlayer);
    //}

    return true;
}

bool world_net_module_impl::send_msg_to_game(const data_list& arg_object_var, const data_list& arg_game_id,
    const ark::protobuf::game_message_id msg_id, google::protobuf::Message& data_value) {
    if (arg_game_id.get_count() != arg_object_var.get_count())
    {
        return false;
    }

    for (size_t i = 0; i < arg_object_var.get_count(); i++)
    {
        guid_t ident_other = arg_object_var.int64(i);
        int game_id = arg_game_id.get_int(i);

        send_msg_to_game(game_id, msg_id, data_value, ident_other);
    }

    return true;
}

bool world_net_module_impl::send_msg_to_player(
    const ark::protobuf::game_message_id msg_id, google::protobuf::Message& data_value, const guid_t player) {
    int game_id = get_player_game_id(player);

    if (game_id < 0)
    {
        return false;
    }

    return send_msg_to_game(game_id, msg_id, data_value, player);
}

//std::shared_ptr<AFServerData> AFCWorldNetModule::GetSuitProxyForEnter()
//{
//    return mProxyMap.begin()->second;
//}

std::shared_ptr<connection_data> world_net_module_impl::get_suit_proxy_for_enter() {
    // return mProxyMap.begin()->second;
    auto client_service_ptr = net_service_manager_module_->get_client_service(ark::app_type::ark_app_proxy);
    if (client_service_ptr == nullptr)
    {
        ARK_LOG_INFO("Connection data not found connection");
        return nullptr;
    }

    return client_service_ptr->get_suitable_connect(null_str); // todo : change key
}

std::shared_ptr<net_server_service> world_net_module_impl::get_net_server() {
    return net_server_;
}

int world_net_module_impl::get_player_game_id(const guid_t& self) {
    // do something
    return -1;
}

} // namespace ark
