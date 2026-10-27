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

#include "proxy_module_factory.hpp"
#include "proxy_net_module_impl.hpp"

namespace ark {

std::unique_ptr<proxy_net_module> detail::make_proxy_net_module() {
    return std::make_unique<proxy_net_module_impl>();
}

bool proxy_net_module_impl::init() {
    bus_module_ = get_plugin_manager()->try_find_module<bus_module>();
    msg_module_ = get_plugin_manager()->try_find_module<msg_module>();
    net_service_manager_module_ = get_plugin_manager()->try_find_module<net_service_manager_module>();

    if (bus_module_ == nullptr || net_service_manager_module_ == nullptr)
    {
        ARK_LOG_ERROR("proxy network startup requires Bus and Net modules");
        return false;
    }

    return true;
}

bool proxy_net_module_impl::post_init() {
    return start_server();
}

bool proxy_net_module_impl::start_server() {
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

void proxy_net_module_impl::on_server_info_process(const net_msg* msg, conv_id_t session_id) {
}

void proxy_net_module_impl::on_select_server_result_process(const net_msg* msg, conv_id_t session_id) {
}

bool proxy_net_module_impl::verify_connect_data(const std::string& account, const std::string& key) {
    auto connect_data_ptr = pending_connections_.find_value(account);
    if (connect_data_ptr != nullptr && key == connect_data_ptr->connect_key)
    {
        pending_connections_.erase(account);
        return true;
    }

    return false;
}

void proxy_net_module_impl::on_socket_event(const net_event* event) {
    switch (event->get_type())
    {
    case net_event_type::connect:
    {
        ARK_LOG_INFO("Connected success, id = {}", event->get_id());
        on_client_connected(event->get_id());
    }
    break;
    case net_event_type::disconnect:
    {
        ARK_LOG_INFO("Connection closed, id = {}", event->get_id());
        on_client_disconnect(event->get_id());
    }
    break;
    default:
        break;
    }
}

void proxy_net_module_impl::on_client_connected(const guid_t& conn_id) {
    //std::shared_ptr<AFClientConnectionData> pSessionData = std::make_shared<AFClientConnectionData>();

    //pSessionData->conn_id_ = conn_id;
    //client_connections_.insert(conn_id, pSessionData);
}

void proxy_net_module_impl::on_client_disconnect(const guid_t& conn_id) {
    // do something
}

bool proxy_net_module_impl::check_session_state(
    const int game_id, const guid_t& client_id_value, const std::string& account) {
    //AFINetClientService* pWorldNetService = m_pNetServiceManagerModule->GetNetClientServiceByBusID(nGameID);
    //std::shared_ptr<AFConnectionData> pServerData = pWorldNetService->GetServerNetInfo(nGameID);
    //if (pServerData != nullptr && AFConnectionData::CONNECTED == pServerData->net_state_)
    //{
    //    std::shared_ptr<AFClientConnectionData> pSessionData = client_connections_.find_value(xClientID);

    //    if (pSessionData != nullptr && pSessionData->logic_state_ > 0 && pSessionData->game_id_ == nGameID &&
    //        pSessionData->account_ == strAccount)
    //    {
    //        return true;
    //    }
    //}

    return false;
}

void proxy_net_module_impl::on_other_message(const net_msg* msg, conv_id_t session_id) {
    // Transpond(xHead, nMsgID  msg, nLen);
}

void proxy_net_module_impl::on_brocastmsg(const net_msg* msg, conv_id_t session_id) {

    // for (int i = 0; i < xMsg.target_entity_list_size(); i++)
    //{
    //    const ark::protobuf::PBGUID& xPlayerClientID = xMsg.target_entity_list(i);
    //    m_pProxyServerNet_ServerModule->SendToPlayerClient(xMsg.msg_id(), xMsg.msg_data().c_str(),
    //    xMsg.msg_data().size(), AFIMsgModule::PBToGUID(xPlayerC ientID), nPlayerID);
}

} // namespace ark
