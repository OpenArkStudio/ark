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

#include <utility>

#include "ark/logger.hpp"
#include "ark/net/tcp_client.hpp"

namespace ark {

tcp_client::~tcp_client() {
    shutdown();
}

void tcp_client::update() {
    update_net_session();
}

bool tcp_client::start_client(
    std::shared_ptr<const msg_header> head, bus_id_t dst_bus_id, const std::string& host, uint16_t port) {
    ARK_ASSERT_RET_VAL(head != nullptr, false);
    if (client_ != nullptr)
    {
        shutdown();
    }
    client_ = std::make_shared<arknet::tcp_client>(arknet::detail::tcp_frame_size, ark_tcp_recv_buffer_size);
    this->dst_bus_id_ = dst_bus_id;
    const auto weak = weak_from_this();
    const auto initial_buffer_size = get_initial_receive_buffer_size();

    client_->set_auto_reconnect(true, ark_reconnect_time);
    client_->set_connect_timeout(ark_connect_timeout);
    client_
        ->bind_init([weak] {
            if (auto self = weak.lock())
            {
                self->client_->set_no_delay(true);
            }
        })
        .bind_connect([weak, head, initial_buffer_size] {
            const auto ec = arknet::get_last_error();
            const auto self = weak.lock();
            if (self == nullptr)
            {
                return;
            }
            if (ec)
            {
                ARK_LOG_INFO("Client(TCP) connect server failed, error={} errno={}, server_addr={} server_port={} "
                             "local_addr={} local_port={}",
                    ec.message(), ec.value(), self->client_->remote_address(), self->client_->remote_port(),
                    self->client_->local_address(), self->client_->local_port());
                return;
            }
            else
            {
                ARK_LOG_INFO("Client(TCP) connect success, server_addr={} server_port={} local_addr={} local_port={}",
                    self->client_->remote_address(), self->client_->remote_port(), self->client_->local_address(),
                    self->client_->local_port());
            }

            // now session_id
            auto cur_session_id = self->trust_session_id_++;
            // set session ud
            self->client_->set_user_data(cur_session_id);

            // create net event
            net_event* net_connect_event = net_event::alloc_event();
            net_connect_event->set_id(cur_session_id);
            net_connect_event->set_type(net_event_type::connect);
            net_connect_event->set_bus_id(self->dst_bus_id_);
            net_connect_event->set_ip(self->client_->remote_address());
            // TODO: will add port

            // net event & session scope
            {
                scope_w_lock guard(self->rw_lock_);
                if (self->session_ != nullptr)
                {
                    self->retired_sessions_.push_back(self->session_);
                }
                self->session_ =
                    std::make_shared<tcp_client_session>(head, cur_session_id, nullptr, initial_buffer_size);
                if (!self->session_->add_net_event(net_connect_event))
                {
                    net_event::release(net_connect_event);
                }
            }
        })
        .bind_disconnect([weak] {
            const auto ec = arknet::get_last_error();
            const auto self = weak.lock();
            if (self == nullptr)
            {
                return;
            }
            ARK_LOG_INFO(
                "Disconnect server(TCP), error={} errno={} remote_addr={} remote_port={} local_addr={} local_port={}",
                ec.message(), ec.value(), self->client_->remote_address(), self->client_->remote_port(),
                self->client_->local_address(), self->client_->local_port());

            auto session_id = self->client_->get_user_data<conv_id_t>();

            scope_w_lock guard(self->rw_lock_);
            if (self->session_ == nullptr || self->session_->get_session_id() != session_id ||
                self->session_->need_remove())
            {
                return;
            }

            // create net event
            net_event* net_disconnect_event = net_event::alloc_event();
            net_disconnect_event->set_id(session_id);
            net_disconnect_event->set_type(net_event_type::disconnect);
            net_disconnect_event->set_bus_id(self->dst_bus_id_);
            net_disconnect_event->set_ip(self->client_->remote_address());

            self->session_->need_remove(true);
            if (!self->session_->add_net_event(net_disconnect_event))
            {
                net_event::release(net_disconnect_event);
            }
        })
        .bind_recv([weak](std::string_view s) {
            const auto self = weak.lock();
            if (self == nullptr)
            {
                return;
            }
            bool parsed = false;
            {
                scope_r_lock guard(self->rw_lock_);
                if (self->session_ == nullptr || self->session_->need_remove())
                {
                    return;
                }
                self->session_->add_buffer(s.data(), s.length());
                parsed = self->session_->parse_buffer_to_msg();
            }
            if (!parsed)
            {
                self->client_->stop();
            }
        });

    bool ret = client_->start(host, port);

    set_working(ret);
    return ret;
}

void tcp_client::shutdown() {
    if (client_ != nullptr)
    {
        client_->stop();
    }
    set_working(false);
}

void tcp_client::update_net_session() {
    std::vector<tcp_client_session_ptr> sessions;
    {
        scope_w_lock guard(rw_lock_);
        sessions.swap(retired_sessions_);
        if (session_ != nullptr)
        {
            sessions.push_back(session_);
        }
    }
    for (const auto& session : sessions)
    {
        update_net_event(session);
        update_net_msg(session);
    }
}

void tcp_client::update_net_event(const tcp_client_session_ptr& session) {
    ARK_ASSERT_RET_NONE(session != nullptr);

    net_event* event(nullptr);
    while (session->pop_net_event(event))
    {
        net_event_cb_(event);
        net_event::release(event);
    }
}

void tcp_client::update_net_msg(const tcp_client_session_ptr& session) {
    ARK_ASSERT_RET_NONE(session != nullptr);

    net_msg* msg(nullptr);
    int msg_count = 0;
    while (session->pop_net_msg(msg))
    {
        net_msg_cb_(msg, session->get_session_id());
        net_msg::release(msg);

        ++msg_count;
        if (msg_count >= ark_process_net_msg_count_once && !session->need_remove())
        {
            break;
        }
    }
}

bool tcp_client::send_msg(msg_header* head, const char* msg_data, conv_id_t session_id) {
    ARK_ASSERT_RET_VAL(head != nullptr && msg_data != nullptr, false);
    // Header serialization can run application code and reconnect the client.
    std::string buffer;
    head->serialize_to_string(buffer);
    const auto message_length = head->message_length();
    if (buffer.empty() || !head->accepts_body_size(message_length))
    {
        return false;
    }

    buffer.append(msg_data, message_length);
    scope_r_lock guard(rw_lock_);
    if (client_ == nullptr || session_ == nullptr || session_->need_remove() ||
        (session_id != 0 && session_->get_session_id() != session_id))
    {
        return false;
    }
    return client_->async_send(std::move(buffer));
}

bool tcp_client::send_msg(const char* msg, size_t msg_len) {
    return client_ != nullptr && client_->async_send(msg, msg_len);
}

void tcp_client::close_session(conv_id_t session_id) {
    std::shared_ptr<arknet::tcp_client> client;
    {
        scope_r_lock guard(rw_lock_);
        if (client_ == nullptr || session_ == nullptr || session_->need_remove() ||
            session_->get_session_id() != session_id)
        {
            return;
        }
        session_id = session_->get_session_id();
        client = client_;
    }
    client->post([weak = std::weak_ptr(client), session_id] {
        if (const auto current = weak.lock();
            current != nullptr && current->is_started() && current->get_user_data<conv_id_t>() == session_id)
        {
            current->stop();
        }
    });
}

} // namespace ark
