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

#include "ark/module.hpp"
#include "ark/net/net_client_service.hpp"
#include "ark/net/net_server_service.hpp"

namespace ark {

class net_service_manager_module : public module {
public:
    // server-side net service
    virtual bool create_server(const size_t silent_timeout = std::chrono::milliseconds(ark_net_heart_time).count(),
        const msg_header& cs_req_header = s_msg_header(), const msg_header& ss_req_header = ss_msg_header()) = 0;
    virtual std::shared_ptr<net_server_service> get_inter_net_server() = 0;
    virtual std::shared_ptr<net_server_service> get_intra_net_server() = 0;

    // client
    //virtual int CreateClusterClients(const AFHeaderLen head_len = AFHeaderLen::SS_HEAD_LENGTH) = 0;
    //virtual int CreateClusterClient(const AFHeaderLen head_len, bus_id_t bus_id, const std::string& url) = 0;

    //virtual AFINetClientService* GetNetClientService(const ARK_APP_TYPE& app_type) = 0;
    //virtual AFINetClientService* GetNetClientServiceByBusID(bus_id_t bus_id) = 0;

    virtual bool add_net_connection_bus(bus_id_t target_bus_id, std::shared_ptr<net> net_ptr) = 0;
    virtual bool remove_net_connection_bus(bus_id_t target_bus_id) = 0;
    virtual std::shared_ptr<net> get_net_connection_bus(bus_id_t src_bus, bus_id_t target_bus) = 0;

    virtual std::shared_ptr<net_client_service> get_client_service(const app_type app_type) = 0;

    virtual conv_id_t get_session_id(bus_id_t client_bus_id) = 0;
    virtual bool remove_session_id(conv_id_t session_id) = 0;

    virtual bool create_client_service(const bus_addr& bus_addr, proto_type proto, const std::string& ip, uint16_t port,
        std::shared_ptr<const msg_header> head = std::shared_ptr<const msg_header>(new ss_msg_header)) = 0;
};

} // namespace ark
