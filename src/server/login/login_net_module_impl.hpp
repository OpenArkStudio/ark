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
#include "ark/module_base.hpp"
#include "ark/net/net_service_manager_module.hpp"
#include "ark/proto.hpp"
#include "login_net_module.hpp"

namespace ark {

class login_net_module_impl final : public module_base<login_net_module>
{
public:
    bool init() override;
    bool post_init() override;
    //bool PreUpdate() override;

protected:
    bool start_server();

    // void OnSocketClientEvent(const NetEventType event, const guid_t& conn_id, const std::string& ip,  bus_id_t bus_id);

    // void OnClientDisconnect(const guid_t& xClientID);
    // void OnClientConnected(const guid_t& xClientID);

private:
    // AFSmartPtrMap<guid_t, AFClientConnectionData> mmClientSessionData;

    bus_module* bus_module_;
    net_service_manager_module* net_service_manager_module_;

    std::shared_ptr<net_server_service> net_server_;
};

} // namespace ark
