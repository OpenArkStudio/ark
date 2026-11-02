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

#include "ark/net/net_server_service_impl.hpp"
#include "ark/net/rudp_server.hpp"
#include "ark/net/tcp_server.hpp"

namespace ark {

net_server_service_impl::net_server_service_impl(plugin_manager* p)
    : plugin_manager_(p) {
    net_service_manager_ = plugin_manager_->find_module<net_service_manager_module>();
    msg_module_ = plugin_manager_->find_module<msg_module>();
}

net_server_service_impl::~net_server_service_impl() {
    shutdown();
}

void net_server_service_impl::shutdown() {
    closed_ = true;
    callback_lifetime_.reset();
    if (network_)
    {
        network_->shutdown();
        network_.reset();
    }
    reg_server_call_back_ = {};
    net_msg_callbacks_.clear();
    net_forward_msg_callbacks_.clear();
    net_event_callbacks_.clear();
    connection_list_.clear();
    client_connection_list_.clear();
}

bool net_server_service_impl::start(std::shared_ptr<const msg_header> head, bus_id_t bus_id, const endpoint& ep,
    uint8_t thread_count, uint32_t max_connection, const size_t silent_timeout) {
    if (closed_)
    {
        return false;
    }
    const std::weak_ptr<void> lifetime = callback_lifetime_;
    const net_msg_functor message_callback = [this, lifetime](const net_msg* message, conv_id_t session) {
        if (const auto alive = lifetime.lock())
        {
            on_net_msg(message, session);
        }
    };
    const net_event_functor event_callback = [this, lifetime](const net_event* event) {
        if (const auto alive = lifetime.lock())
        {
            on_net_event(event);
        }
    };
    bool ret = false;
    if (ep.proto() == proto_type::tcp)
    {
        network_ = std::make_shared<tcp_server>(message_callback, event_callback);
        ret = network_->start_server(
            head, bus_id, ep.get_ip(), ep.get_port(), thread_count, max_connection, silent_timeout);

        //AFINetServerService::RegMsgCallback(
        //    ark::protobuf::e_ss_msg_id_server_report, this, &AFCNetServerService::OnClientRegister);
    }
    else if (ep.proto() == proto_type::udp)
    {
        network_ = std::make_shared<rudp_server>(message_callback, event_callback);
        ret = network_->start_server(
            head, bus_id, ep.get_ip(), ep.get_port(), thread_count, max_connection, silent_timeout);
    }
    else if (ep.proto() == proto_type::ws)
    {
        // will add websocket server
    }
    else
    {
        // other protocols will be supported
        return false;
    }

    return ret;
}

bool net_server_service_impl::update() {
    if (closed_)
    {
        return true;
    }
    ARK_ASSERT_RET_VAL(network_ != nullptr, false);
    network_->update();

    //ProcessConnection();
    process_client_connection();
    return true;
}

std::shared_ptr<net> net_server_service_impl::get_net() {
    return network_;
}

bool net_server_service_impl::reg_msg_callback(msg_id_t msg_id, net_msg_functor&& cb) {
    if (net_msg_callbacks_.find(msg_id) != net_msg_callbacks_.end())
    {
        return false;
    }
    else
    {
        net_msg_callbacks_.insert(std::make_pair(msg_id, std::forward<net_msg_functor>(cb)));
        return true;
    }
}

bool net_server_service_impl::reg_forward_msg_callback(net_msg_functor&& cb) {
    net_forward_msg_callbacks_.push_back(std::forward<net_msg_functor>(cb));
    return true;
}

bool net_server_service_impl::reg_net_event_callback(net_event_functor&& cb) {
    net_event_callbacks_.push_back(std::forward<net_event_functor>(cb));
    return true;
}

bool net_server_service_impl::reg_reg_server_call_back(net_msg_session_functor&& cb) {
    reg_server_call_back_ = std::forward<net_msg_session_functor>(cb);
    return true;
}

void net_server_service_impl::set_timeout_interval(const int32_t interval) {
    if (interval <= 0)
    {
        return;
    }

    client_timeout_interval_ = interval;
}

void net_server_service_impl::on_net_msg(const net_msg* msg, conv_id_t session_id) {
    ARK_LOG_DEBUG("[msg][recv] msg id = {} session id = {}", msg->get_msg_id(), session_id);

    auto msg_id = msg->get_msg_id();
    if (msg_id == ark::protobuf::e_ss_msg_id_reg_to_server)
    {
        reg_server_call_back_(msg, session_id);

        net_service_manager_->remove_net_connection_bus(msg->get_src_bus());
        net_service_manager_->add_net_connection_bus(msg->get_src_bus(), get_net());

        remove_connection(msg->get_src_bus());
        add_connection(msg->get_src_bus(), session_id);
        return;
    }
    else if (msg_id == ark::protobuf::e_ss_msg_id_heart_beat)
    {
        update_connection(msg->get_src_bus());
        return;
    }

    if (msg->get_src_bus() == 0)
    {
        update_client_connection(session_id);
    }

    auto it = net_msg_callbacks_.find(msg_id);
    if (it != net_msg_callbacks_.end())
    {
        (it->second)(msg, session_id);
    }
    else
    {
        // TODO:forward to other server process

        ARK_LOG_ERROR("Invalid message, id = {}", msg_id);

        // for (const auto& iter : mxCallBackList)
        //{
        //    (iter)(head, msg_id, msg, msg_len, session_id);
        //}
    }
}

void net_server_service_impl::on_net_event(const net_event* event) {
    switch (event->get_type())
    {
    case net_event_type::connect:
        ARK_LOG_INFO("Connected server = {} succenssfully, ip = {}, session_id = {}",
            bus_addr(event->get_bus_id()).to_string(), event->get_ip(), event->get_id());
        // TODO: NickYang, add bus relation of AFINet
        break;
    case net_event_type::disconnect:
        ARK_LOG_INFO("Disconnected server = {} succenssfully, ip = {}, session_id = {}",
            bus_addr(event->get_bus_id()).to_string(), event->get_ip(), event->get_id());
        //m_pNetServiceManagerModule->RemoveSessionID(event->GetId());
        //m_pNetServiceManagerModule->RemoveNetConnectionBus(event->GetBusId());
        remove_client_connection(event->get_id());
        break;
    default:
        break;
    }

    for (const auto& it : net_event_callbacks_)
    {
        (it)(event);
    }
}

void net_server_service_impl::on_disconnect_net_event(const net_event* event) {
    ARK_LOG_INFO("TimeOut Disconnected server = {} succenssfully, ip = {}, session_id = {}",
        bus_addr(event->get_bus_id()).to_string(), event->get_ip(), event->get_id());

    for (const auto& it : net_event_callbacks_)
    {
        (it)(event);
    }
}

//void AFCNetServerService::ProcessConnection()
//{
//    ///check connection
//    auto now = AFDateTime::GetNowTime();
//    for (auto& iter : connection_list_)
//    {
//        auto connection = iter.second;
//        if (!connection)
//        {
//            continue;
//        }
//
//        if (connection->connect_state_ != AFConnection::eConnectState::CONNECTED)
//        {
//            continue;
//        }
//
//        if (connection->last_active_time_ + 30 * 1000 < now)
//        {
//            connection->connect_state_ = AFConnection::eConnectState::DISCONNECT;
//
//            /// close session
//            connection_list_.erase(iter.first);
//            m_pNet->CloseSession(connection->session_id_);
//
//            /// todo
//            AFNetEvent event;
//            event.SetType(AFNetEventType::DISCONNECT);
//            event.SetBusId(connection->bus_id_);
//            event.SetId(connection->session_id_);
//            OnNetEvent(&event);
//            break;
//        }
//    }
//}

void net_server_service_impl::process_client_connection() {
    std::list<conv_id_t> close_list;

    ///check connection
    auto now = date_time::get_now_time();
    for (auto& iter : client_connection_list_)
    {
        auto& connection = iter.second;
        if (connection.last_active_time + client_timeout_interval_ < now)
        {
            /// close session
            close_list.push_back(iter.first);
            network_->close_session(iter.first);

            net_event event;
            event.set_type(net_event_type::disconnect);
            event.set_bus_id(connection.bus_id);
            event.set_id(connection.session_id);
            on_disconnect_net_event(&event);
        }
    }

    for (auto iter : close_list)
    {
        remove_client_connection(iter);
    }
}

void net_server_service_impl::add_connection(bus_id_t bus_id, conv_id_t session_id) {
    //if (connection_list_.find_value(bus_id))
    //{
    //    return;
    //}
    remove_connection(bus_id);

    auto connection = std::make_shared<ark::connection>(
        bus_id, session_id, date_time::get_now_time(), ark::connection::connect_state::connected);
    connection_list_.insert(bus_id, connection);
}

void net_server_service_impl::update_connection(bus_id_t bus_id) {
    auto connection = connection_list_.find_value(bus_id);
    if (!connection)
    {
        return;
    }

    auto now = date_time::get_now_time();
    connection->last_active_time = now;
}

void net_server_service_impl::remove_connection(bus_id_t bus_id) {
    connection_list_.erase(bus_id);
}

void net_server_service_impl::update_client_connection(conv_id_t session_id) {
    auto iter = client_connection_list_.find(session_id);
    if (iter == client_connection_list_.end())
    {
        auto connection =
            ark::connection(0, session_id, date_time::get_now_time(), ark::connection::connect_state::connected);
        client_connection_list_.insert(std::make_pair(session_id, connection));
    }
    else
    {
        iter->second.last_active_time = date_time::get_now_time();
    }
}

} // namespace ark
