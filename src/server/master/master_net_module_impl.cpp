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

#include "master_module_factory.hpp"
#include "master_net_module_impl.hpp"

namespace ark {

std::unique_ptr<master_net_module> detail::make_master_net_module() {
    return std::make_unique<master_net_module_impl>();
}

bool master_net_module_impl::init() {
    bus_module_ = get_plugin_manager()->try_find_module<bus_module>();
    net_service_manager_module_ = get_plugin_manager()->try_find_module<net_service_manager_module>();

    if (bus_module_ == nullptr || net_service_manager_module_ == nullptr)
    {
        ARK_LOG_ERROR("master network startup requires Bus and Net modules");
        return false;
    }

    timer_module_ = get_plugin_manager()->try_find_module<timer_module>();
    msg_module_ = get_plugin_manager()->try_find_module<msg_module>();

    // log timer
    // m_pTimerModule->AddForeverTimer("master-sub-server-timer", BusID(), 30 * 1000, this,
    // &AFCMasterNetServerModule::OnTimerLogServer);

    return true;
}

bool master_net_module_impl::post_init() {
    return start_server();
}

bool master_net_module_impl::start_server() {
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

// void AFCMasterNetServerModule::OnSocketEvent(const NetEventType event, const guid_t& conn_id, const std::string& ip,
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
//            OnClientDisconnect(bus_id, conn_id);
//        }
//        break;
//    default:
//        break;
//    }
//}

// void AFCMasterNetServerModule::OnClientConnected(const guid_t& conn_id)
//{
//
//}

// void AFCMasterNetServerModule::OnClientDisconnect(bus_id_t bus_id, const guid_t& conn_id)
//{
//    reg_servers_.RemoveElement(bus_id);
//}

// void AFCMasterNetServerModule::OnHeartBeat(const ARK_PKG_BASE_HEAD& head, msg_id_t msg_id, const char* msg, const
// uint32_t msg_len, const guid_t& conn_id)
//{
//    //do nothing
//}

//////////////////////////////////////////////////////////////////////////
// void AFCMasterNetServerModule::OnTimerLogServer(const std::string& name, const guid_t& id)
//{
//    ARK_LOG_INFO("---------------------------Start to log all registered server---------------------------");
//    for (bool ret = reg_servers_.Begin(); ret; ret = reg_servers_.Increase())
//    {
//        auto& server_data = reg_servers_.GetCurrentData();
//        ARK_LOG_INFO("bus[{}] state[{}] url[{}] conn_id[{}]",
//                     server_data->server_info_.bus_id(),
//                     ark::protobuf::e_ss_server_state_Name(server_data->server_info_.logic_status()),
//                     server_data->conn_id_.nLow);
//    }
//    ARK_LOG_INFO("---------------------------End to log all registered server---------------------------");
//}

} // namespace ark
