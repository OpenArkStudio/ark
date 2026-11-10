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
#include "ark/proto.hpp"

namespace ark {

class net_server_service;

class world_net_module : public module {
public:
    virtual bool send_msg_to_game(const int game_id, const ark::protobuf::game_message_id msg_id,
        google::protobuf::Message& data_value, const guid_t player = guid_t()) = 0;
    virtual bool send_msg_to_game(const data_list& arg_object_var, const data_list& arg_game_id,
        const ark::protobuf::game_message_id msg_id, google::protobuf::Message& data_value) = 0;

    // virtual std::shared_ptr<AFServerData> GetSuitProxyForEnter() = 0;
    virtual std::shared_ptr<connection_data> get_suit_proxy_for_enter() = 0;
    virtual std::shared_ptr<net_server_service> get_net_server() = 0;
};

} // namespace ark
