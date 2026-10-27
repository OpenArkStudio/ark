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
#include "ark/container/consistent_hash.hpp"
#include "ark/module_base.hpp"
#include "ark/net/net_service_manager_module.hpp"
#include "ark/proto.hpp"
#include "proxy_net_module.hpp"

namespace ark {

class proxy_net_module_impl final : public module_base<proxy_net_module>
{
public:
    bool init() override;
    bool post_init() override;
    //bool PreUpdate() override;

    bool verify_connect_data(const std::string& account, const std::string& key) override;

protected:
    bool start_server();

    void on_select_server_result_process(const net_msg* msg, conv_id_t session_id);
    void on_server_info_process(const net_msg* msg, conv_id_t session_id);

    void on_other_message(const net_msg* msg, conv_id_t session_id);
    void on_brocastmsg(const net_msg* msg, conv_id_t session_id);

    void on_socket_event(const net_event* event);

    void on_client_disconnect(const guid_t& client_id_value);
    void on_client_connected(const guid_t& client_id_value);

    // void OnTransMessage(const ARK_PKG_BASE_HEAD& xHead, const int nMsgID, const char* msg, const uint32_t nLen, const
    // guid_t& xClientID);

    template<class message_type>
    void check_session_trans_msg(const net_msg* msg) {

        // if (CheckSessionState(xMsg.game_id(), xClientID, xMsg.account()))
        //{
        //    m_pMsgModule->SendSSMsg(xMsg.game_id(), nMsgID, msg, nLen, xClientID);
        //}
    }

    bool check_session_state(const int game_id, const guid_t& client_id_value, const std::string& account);

    class client_connect_data
    {
    public:
        client_connect_data() {
        }

        std::string account{};
        std::string connect_key{};
    };

    shared_object_map<std::string, client_connect_data> pending_connections_;

private:
    // AFSmartPtrMap<guid_t, AFClientConnectionData> client_connections_; // net_conn_id <--> SessionData

    bus_module* bus_module_;
    net_service_manager_module* net_service_manager_module_;
    msg_module* msg_module_;

    std::shared_ptr<net_server_service> net_server_{nullptr};
};

} // namespace ark
