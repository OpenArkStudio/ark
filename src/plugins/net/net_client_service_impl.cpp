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

#include "ark/net/net_client_service_impl.hpp"
#include "ark/net/rudp_client.hpp"
#include "ark/net/tcp_client.hpp"
#include "ark/proto.hpp"
#include "ark/time/date_time.hpp"

namespace ark {

net_client_service_impl::net_client_service_impl(plugin_manager* p)
    : plugin_manager_(p) {
    net_service_manager_ = plugin_manager_->find_module<net_service_manager_module>();
    bus_module_ = plugin_manager_->find_module<bus_module>();
    msg_module_ = plugin_manager_->find_module<msg_module>();

    consistent_hashmap_.set_vnode_count(default_weight);
}

net_client_service_impl::~net_client_service_impl() {
    shutdown();
}

void net_client_service_impl::shutdown() {
    closed_ = true;
    callback_lifetime_.reset();
    for (const auto& [bus, connection] : real_connections_)
    {
        if (connection->net_client)
        {
            connection->net_client->shutdown();
            connection->net_client.reset();
        }
    }
    real_connections_.clear();
    tmp_connections_.clear();
    net_msg_callbacks_.clear();
    net_msg_forward_callbacks_.clear();
    net_event_callbacks_.clear();
    actor_bus_map_.clear();
    account_bus_map_.clear();
}

bool net_client_service_impl::start_client(
    std::shared_ptr<const msg_header> head, bus_id_t target_bus_id, const endpoint& endpoint) {
    if (closed_)
    {
        return false;
    }
    connection_data data;
    data.head = head;
    data.server_bus_id = target_bus_id;
    data.remote_endpoint = endpoint;

    tmp_connections_.push_back(data);
    return true;
}

void net_client_service_impl::update() {
    if (closed_)
    {
        return;
    }
    process_add_connection();
    process_update();
}

bool net_client_service_impl::reg_msg_callback(msg_id_t msg_id, const net_msg_functor&& cb) {
    if (net_msg_callbacks_.find(msg_id) != net_msg_callbacks_.end())
    {
        return false;
    }
    else
    {
        net_msg_callbacks_.insert(std::make_pair(msg_id, std::forward<const net_msg_functor>(cb)));
        return true;
    }
}

bool net_client_service_impl::reg_forward_msg_callback(const net_msg_functor&& cb) {
    net_msg_forward_callbacks_.push_back(std::forward<const net_msg_functor>(cb));
    return true;
}

bool net_client_service_impl::reg_net_event_callback(const net_event_functor&& cb) {
    net_event_callbacks_.push_back(std::forward<const net_event_functor>(cb));
    return true;
}

void net_client_service_impl::process_update() {
    for (auto iter : real_connections_)
    {
        auto connection_data = iter.second;
        if (connection_data->net_client == nullptr)
        {
            continue;
        }

        switch (connection_data->net_state)
        {
        case connection_data::disconnect:
        {
            connection_data->net_state = connection_data::reconnect;
            connection_data->net_client->shutdown();
        }
        break;
        case connection_data::connecting:
        {
            connection_data->net_client->update();
        }
        break;
        case connection_data::connected:
        {
            connection_data->net_client->update();

            keep_alive(connection_data);
        }
        break;
        case connection_data::reconnect:
        {
            // RECONNECT 30s/time
            if ((connection_data->last_active_time + 30 * timespan::second_ms) >= plugin_manager_->get_now_time())
            {
                break;
            }

            connection_data->last_active_time = plugin_manager_->get_now_time();

            connection_data->net_client->shutdown();

            // based on protocol to create a new client
            bool ret = connection_data->net_client->start_client(connection_data->head, connection_data->server_bus_id,
                connection_data->remote_endpoint.get_ip(), connection_data->remote_endpoint.get_port());
            if (!ret)
            {
                connection_data->net_state = connection_data::reconnect;
            }
            else
            {
                connection_data->net_state = connection_data::connecting;
            }
        }
        break;
        default:
            break;
        }
    }
}

std::shared_ptr<net> net_client_service_impl::create_net(const proto_type proto) {
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
    if (proto == proto_type::tcp)
    {
        return std::make_shared<tcp_client>(message_callback, event_callback);
    }
    else if (proto == proto_type::udp)
    {
        return std::make_shared<rudp_client>(message_callback, event_callback);
    }
    else if (proto == proto_type::ws)
    {
        // will add web-socket client
    }

    // other protocol will be supported
    return nullptr;
}

void net_client_service_impl::log_server_info() {
    log_server_info("This is a client, begin to print Server Info----------------------------------");

    for (auto iter : real_connections_)
    {
        auto connection_data = iter.second;
        if (connection_data != nullptr)
        {
            std::string info = ARK_FORMAT("TargetBusID={} State={} url={}", connection_data->server_bus_id,
                static_cast<int>(connection_data->net_state), connection_data->remote_endpoint.to_string());
            log_server_info(info);
        }
    }

    log_server_info("This is a client, end to print Server Info----------------------------------");
}

void net_client_service_impl::keep_alive(std::shared_ptr<connection_data> server_data_ptr) {
    if ((server_data_ptr->last_active_time + 5000) > plugin_manager_->get_now_time())
    {
        return;
    }

    send_report(server_data_ptr);

    server_data_ptr->last_active_time = plugin_manager_->get_now_time();

    keep_report(server_data_ptr);
    log_server_info();
}

bool net_client_service_impl::add_server_node(std::shared_ptr<connection_data> data) {
    consistent_hashmap_.insert(data->server_bus_id, data->remote_endpoint.get_ip(), data->remote_endpoint.get_port());
    return true;
}

bool net_client_service_impl::get_server_node(const std::string& hash_key, v_node& vnode) {
    auto iter = consistent_hashmap_.find(hash_key);
    if (iter == consistent_hashmap_.end())
    {
        return false;
    }
    else
    {
        vnode = iter->second;
        return true;
    }
}

void net_client_service_impl::remove_server_node(std::shared_ptr<connection_data> data) {
    v_node vnode(data->server_bus_id, data->remote_endpoint.get_ip(), data->remote_endpoint.get_port(), 0);
    consistent_hashmap_.erase(vnode);
}

int net_client_service_impl::on_connect(const net_event* event) {
    ARK_LOG_INFO("Connected [{}] successfully, ip={} session_id={}", bus_addr(event->get_bus_id()).to_string(),
        event->get_ip(), event->get_id());

    auto connection_info = get_connection_info(event->get_bus_id());

    if (connection_info != nullptr)
    {
        add_server_node(connection_info);
        connection_info->net_state = connection_data::connected;

        // add server-bus-id -> client-bus-id
        net_service_manager_->add_net_connection_bus(event->get_bus_id(), connection_info->net_client);

        // send reg message to server
        reg_to_server(*(connection_info->head.get()), event->get_bus_id());
    }

    return 0;
}

int net_client_service_impl::on_disconnect(const net_event* event) {
    ARK_LOG_ERROR("Disconnect [{}] successfully, ip={} session_id={}", bus_addr(event->get_bus_id()).to_string(),
        event->get_ip(), event->get_id());

    std::shared_ptr<connection_data> connection_info = get_connection_info(event->get_bus_id());

    if (connection_info != nullptr)
    {
        remove_server_node(connection_info);
        connection_info->net_state = connection_data::disconnect;
        connection_info->last_active_time = plugin_manager_->get_now_time();
        // remove net bus
        net_service_manager_->remove_net_connection_bus(event->get_bus_id());
    }

    return 0;
}

void net_client_service_impl::process_add_connection() {
    for (auto& iter : tmp_connections_)
    {
        const connection_data& pending_connection = iter;
        std::shared_ptr<connection_data> target_connection_data =
            real_connections_.find_value(pending_connection.server_bus_id);
        if (target_connection_data != nullptr)
        {
            continue;
        }
        // add new server
        target_connection_data = std::make_shared<connection_data>();

        *target_connection_data = pending_connection;
        target_connection_data->last_active_time = plugin_manager_->get_now_time();

        // based on protocol to create a new client
        target_connection_data->net_client = create_net(target_connection_data->remote_endpoint.proto());
        int ret = target_connection_data->net_client->start_client(target_connection_data->head,
            target_connection_data->server_bus_id, target_connection_data->remote_endpoint.get_ip(),
            target_connection_data->remote_endpoint.get_port());
        if (!ret)
        {
            target_connection_data->net_state = connection_data::reconnect;
        }
        else
        {
            target_connection_data->net_state = connection_data::connecting;
        }

        real_connections_.insert(target_connection_data->server_bus_id, target_connection_data);
    }

    tmp_connections_.clear();
}

void net_client_service_impl::on_net_msg(const net_msg* msg, conv_id_t session_id) {
    ARK_LOG_DEBUG("[msg][recv] msg id = {} session id = {}", msg->get_msg_id(), session_id);

    auto it = net_msg_callbacks_.find(msg->get_msg_id());

    if (it != net_msg_callbacks_.end())
    {
        (it->second)(msg, session_id);
    }
    else
    {
        ARK_LOG_ERROR("Invalid message, id = {}", msg->get_msg_id());
        // TODO:forward to other server process
    }
}

void net_client_service_impl::on_net_event(const net_event* event) {
    switch (event->get_type())
    {
    case net_event_type::connect:
        on_connect(event);
        break;
    case net_event_type::disconnect:
        on_disconnect(event);
        break;
    default:
        break;
    }

    for (const auto& it : net_event_callbacks_)
    {
        (it)(event);
    }
}

std::shared_ptr<connection_data> net_client_service_impl::get_connection_info(bus_id_t bus_id) {
    return real_connections_.find_value(bus_id);
}

std::shared_ptr<connection_data> net_client_service_impl::get_suitable_connect(const std::string& key) {
    static v_node vnode(0, null_str, 0, 0);
    if (!get_server_node(key, vnode))
    {
        return nullptr;
    }

    return get_connection_info(vnode.bus_id);
}

void net_client_service_impl::add_account_bus_id(const std::string& account, bus_id_t bus_id) {
    account_bus_map_.insert(std::make_pair(account, bus_id));
}

void net_client_service_impl::remove_account_bus_id(const std::string& account) {
    account_bus_map_.erase(account);
}

int net_client_service_impl::get_account_bus_id(const std::string& account) {
    auto iter = account_bus_map_.find(account);
    if (iter == account_bus_map_.end())
    {
        return -1;
    }

    return iter->second;
}

void net_client_service_impl::add_actor_bus_id(const guid_t& actor, bus_id_t bus_id) {
    actor_bus_map_.insert(std::make_pair(actor, bus_id));
}

void net_client_service_impl::remove_actor_bus_id(const guid_t& actor) {
    actor_bus_map_.erase(actor);
}

int net_client_service_impl::get_actor_bus_id(const guid_t& actor) {
    auto iter = actor_bus_map_.find(actor);
    if (iter == actor_bus_map_.end())
    {
        return -1;
    }

    return iter->second;
}

void net_client_service_impl::reg_to_server(const msg_header& head, bus_id_t target_bus_id) {
    auto bus_id = bus_module_->get_self_bus_id();
    auto net_ptr = net_service_manager_->get_net_connection_bus(bus_id, target_bus_id);
    if (!net_ptr)
    {
        return;
    }

    auto new_head = head.create();
    new_head->set_message_id(ark::protobuf::e_ss_msg_id_reg_to_server);
    new_head->set_source_bus_id(bus_id);
    new_head->set_dest_bus_id(target_bus_id);

    net_ptr->send_msg(new_head, "", 0);

    delete new_head;
}

void net_client_service_impl::send_report(std::shared_ptr<connection_data> server_data_ptr) {
    auto head = server_data_ptr->head->create();
    head->set_message_id(ark::protobuf::e_ss_msg_id_heart_beat);
    head->set_source_bus_id(bus_module_->get_self_bus_id());
    head->set_dest_bus_id(server_data_ptr->server_bus_id);

    server_data_ptr->net_client->send_msg(head, "", 0);

    delete head;
}

} // namespace ark
