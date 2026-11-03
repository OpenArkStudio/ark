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

#include <google/protobuf/message.h>

#include "ark/module.hpp"
#include "ark/net/net_server_service.hpp"

namespace ark {

class game_net_module : public module
{
public:
    //class GateBaseInfo
    //{
    //public:
    //    GateBaseInfo() {}

    //    GateBaseInfo(const int gateID, const guid_t xIdent)
    //        : nGateID(gateID)
    //        , xClientID(xIdent)
    //    {
    //    }

    //    int nActorID{0};
    //    int nGateID{0};
    //    guid_t xClientID{0};
    //};

    //class GateServerInfo
    //{
    //public:
    //    AFServerData xServerData;
    //    //actor id -> gate fd
    //    std::map<guid_t, guid_t> xRoleInfo;
    //};

    virtual std::shared_ptr<net_server_service> get_net_server_service() = 0;
    virtual void send_msg_protobuf_to_gate(
        const uint16_t msg_id, google::protobuf::Message& msg_value, const guid_t& self) = 0;
    virtual void send_msg_protobuf_to_gate(const uint16_t msg_id, const std::string& msg, const guid_t& self) = 0;
    virtual bool add_player_gate_info(const guid_t& role_id, const guid_t& client_id, const int gate_id) = 0;
    virtual bool remove_player_gate_info(const guid_t& role_id) = 0;
    // virtual std::shared_ptr<GateBaseInfo> GetPlayerGateInfo(const guid_t& nRoleID) = 0;
};

} // namespace ark
