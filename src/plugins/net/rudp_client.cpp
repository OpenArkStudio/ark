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

#include <algorithm>
#include <vector>

#include "ark/logger.hpp"
#include "ark/net/rudp_client.hpp"

namespace ark {

rudp_client::~rudp_client() {
    shutdown();
}

bool rudp_client::start_client(
    std::shared_ptr<const msg_header> head, bus_id_t dst_bus_id, const std::string& host, uint16_t port) {
    if (!head)
    {
        return false;
    }
    shutdown();
    client_ = std::make_shared<arknet::rudp_client>(65536, ark_tcp_recv_buffer_size);
    this->dst_bus_id_ = dst_bus_id;
    arknet::arq::options transport_options;
    transport_options.mtu = 1400;
    transport_options.congestion_control = false;
    client_->set_rudp_options(transport_options);
    client_->set_auto_reconnect(true, ark_reconnect_time);
    client_->set_connect_timeout(ark_connect_timeout);
    auto weak = weak_from_this();
    const auto initial_buffer_size = get_initial_receive_buffer_size();
    client_
        ->bind_connect([weak, head, initial_buffer_size] {
            const auto ec = arknet::get_last_error();
            auto self = weak.lock();
            if (!self)
            {
                return;
            }
            if (ec)
            {
                ARK_LOG_INFO("Client(RUDP) connect server failed, error={} errno={}, server_addr={} "
                             "server_port={},local_addr={} local_port={}",
                    ec.message(), ec.value(), self->client_->remote_address(), self->client_->remote_port(),
                    self->client_->local_address(), self->client_->local_port());
                return;
            }
            else
            {
                ARK_LOG_INFO(
                    "Client(RUDP) connect server success, server_addr={} server_port={} local_addr={} local_port={}",
                    self->client_->remote_address(), self->client_->remote_port(), self->client_->local_address(),
                    self->client_->local_port());
            }

            auto cur_session_id = self->trust_session_id_++;
            self->client_->set_user_data(cur_session_id);

            net_event* net_connect_event = net_event::alloc_event();
            net_connect_event->set_id(cur_session_id);
            net_connect_event->set_type(net_event_type::connect);
            net_connect_event->set_bus_id(self->dst_bus_id_);
            net_connect_event->set_ip(self->client_->remote_address());
            {
                scope_w_lock guard(self->rw_lock_);
                self->session_ =
                    std::make_shared<rdp_client_session>(head, cur_session_id, nullptr, initial_buffer_size);
                self->sessions_.push_back(self->session_);
                if (!self->session_->add_net_event(net_connect_event))
                {
                    net_event::release(net_connect_event);
                }
            }
        })
        .bind_disconnect([weak] {
            const auto ec = arknet::get_last_error();
            auto self = weak.lock();
            if (!self)
            {
                return;
            }
            ARK_LOG_INFO("Client(RUDP) disconnect server, error={} errno={} remote_addr={} remote_port={} "
                         "local_addr={} local_port={}",
                ec.message(), ec.value(), self->client_->remote_address(), self->client_->remote_port(),
                self->client_->local_address(), self->client_->local_port());

            auto session_id = self->client_->get_user_data<conv_id_t>();

            scope_w_lock guard(self->rw_lock_);
            if (!self->session_ || self->session_->get_session_id() != session_id || self->session_->need_remove())
            {
                return;
            }
            net_event* net_disconnect_event = net_event::alloc_event();
            net_disconnect_event->set_id(session_id);
            net_disconnect_event->set_type(net_event_type::disconnect);
            net_disconnect_event->set_bus_id(self->dst_bus_id_);
            net_disconnect_event->set_ip(self->client_->remote_address());
            if (!self->session_->add_net_event(net_disconnect_event))
            {
                net_event::release(net_disconnect_event);
            }
            self->session_->need_remove(true);
        })
        .bind_recv([weak](std::string_view s) {
            auto self = weak.lock();
            if (!self)
            {
                return;
            }
            rdp_client_session_ptr session;
            {
                scope_r_lock guard(self->rw_lock_);
                session = self->session_;
            }
            if (session && !session->need_remove())
            {
                session->add_buffer(s.data(), s.length());
                if (!session->parse_buffer_to_msg())
                {
                    self->client_->stop();
                }
            }
        });

    bool ret = client_->start(host, port);
    set_working(ret);
    return ret;
}

void rudp_client::update() {
    std::vector<rdp_client_session_ptr> sessions;
    {
        scope_r_lock guard(rw_lock_);
        sessions.assign(sessions_.begin(), sessions_.end());
    }
    for (const auto& session : sessions)
    {
        const auto disconnected = session->need_remove();
        update_net_event(session);
        const auto drained = update_net_msg(session);
        if (disconnected && drained)
        {
            scope_w_lock guard(rw_lock_);
            sessions_.erase(std::remove(sessions_.begin(), sessions_.end(), session), sessions_.end());
            if (session_ == session)
            {
                session_.reset();
            }
        }
    }
}

void rudp_client::update_net_event(const rdp_client_session_ptr& session) {
    net_event* event{nullptr};
    while (session->pop_net_event(event))
    {
        net_event_cb_(event);
        net_event::release(event);
    }
}

bool rudp_client::update_net_msg(const rdp_client_session_ptr& session) {
    net_msg* msg{nullptr};
    int msg_count = 0;
    while (session->pop_net_msg(msg))
    {
        net_msg_cb_(msg, session->get_session_id());
        net_msg::release(msg);

        ++msg_count;
        if (msg_count >= ark_process_net_msg_count_once)
        {
            return false;
        }
    }
    return true;
}

void rudp_client::shutdown() {
    if (client_)
    {
        client_->stop();
    }
    set_working(false);
}

bool rudp_client::send_msg(msg_header* head, const char* msg_data, conv_id_t session_id) {
    if (!head || !msg_data || !head->accepts_body_size(head->message_length()) || !client_)
    {
        return false;
    }

    std::string buffer;
    head->serialize_to_string(buffer);
    const auto message_length = head->message_length();
    if (buffer.empty() || !head->accepts_body_size(message_length))
    {
        return false;
    }

    buffer.append(msg_data, message_length);
    scope_r_lock guard(rw_lock_);
    const auto session = session_;
    if (!session || session->need_remove() || (session_id != 0 && session_id != session->get_session_id()))
    {
        return false;
    }

    return client_->async_send(std::move(buffer), [weak = std::weak_ptr<arknet::rudp_client>(client_),
                                                      id = session->get_session_id()](arknet::error_code ec, size_t) {
        if (!ec)
        {
            return;
        }
        ARK_LOG_ERROR("Client(RUDP) send failed, session={} error={} errno={}", id, ec.message(), ec.value());
        if (auto client = weak.lock(); client && client->is_started())
        {
            // Check the session on its executor so an old send cannot stop a reconnected client.
            client->post([weak, id] {
                if (auto current = weak.lock(); current && current->get_user_data<conv_id_t>() == id)
                {
                    current->stop();
                }
            });
        }
    });
}

void rudp_client::close_session(conv_id_t session_id) {
    if (!client_)
    {
        return;
    }
    client_->post([weak = std::weak_ptr<arknet::rudp_client>(client_), session_id] {
        if (auto client = weak.lock(); client && (session_id == 0 || client->get_user_data<conv_id_t>() == session_id))
        {
            client->stop();
        }
    });
}

} // namespace ark
