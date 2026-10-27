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
#include "ark/module_base.hpp"
#include "ark/net/net_service_manager_module.hpp"
#include "ark/proto.hpp"
#include "ark/utility/timer_module.hpp"
#include "world_net_module.hpp"

namespace ark {

class world_net_module_impl final : public module_base<world_net_module>
{
public:
    bool init() override;
    bool post_init() override;
    //bool PreUpdate() override;

    bool send_msg_to_game(const int game_id, const ark::protobuf::game_message_id msg_id,
        google::protobuf::Message& data_value, const guid_t player = 0) override;
    bool send_msg_to_game(const data_list& arg_object_var, const data_list& arg_game_id,
        const ark::protobuf::game_message_id msg_id, google::protobuf::Message& data_value) override;

    // std::shared_ptr<AFServerData> GetSuitProxyForEnter();
    std::shared_ptr<connection_data> get_suit_proxy_for_enter() override;
    std::shared_ptr<net_server_service> get_net_server() override;

protected:
    bool start_server();
    //int StartClient();

    bool send_msg_to_player(
        const ark::protobuf::game_message_id msg_id, google::protobuf::Message& data_value, const guid_t player);

    int get_player_game_id(const guid_t& self);

private:
    // AFSmartPtrMap<int, AFServerData> reg_servers_;

    int64_t last_check_time_;

    // server id, data
    // AFSmartPtrMap<int, AFServerData> mGameMap;
    // AFSmartPtrMap<int, AFServerData> mProxyMap;

    kernel_module* kernel_module_;
    bus_module* bus_module_;
    msg_module* msg_module_;
    net_service_manager_module* net_service_manager_module_;

    std::shared_ptr<net_server_service> net_server_;
};

} // namespace ark
