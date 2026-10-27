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
#include "ark/container/map.hpp"
#include "ark/module_base.hpp"
#include "ark/net/net_service_manager_module.hpp"
#include "ark/proto.hpp"
#include "ark/utility/timer_module.hpp"
#include "master_net_module.hpp"

namespace ark {

class master_net_module_impl final : public module_base<master_net_module>
{
public:
    bool init() override;
    bool post_init() override;

protected:
    bool start_server();
    // void OnSocketEvent(const NetEventType event, const guid_t& conn_id, const std::string& ip, bus_id_t bus_id);

    // void OnClientConnected(const guid_t& xClientID);
    // void OnClientDisconnect(bus_id_t bus_id, const guid_t& xClientID);

    //////////////////////////////////////////////////////////////////////////
    // void OnTimerLogServer(const std::string& name, const guid_t& id);

    // void OnHeartBeat(const ARK_PKG_BASE_HEAD& head, msg_id_t msg_id, const char* msg, const uint32_t msg_len, conv_id_t conn_id);

private:
    bus_module* bus_module_;
    net_service_manager_module* net_service_manager_module_;
    timer_module* timer_module_;
    msg_module* msg_module_;

    std::shared_ptr<net_server_service> net_server_{nullptr};
};

} // namespace ark
