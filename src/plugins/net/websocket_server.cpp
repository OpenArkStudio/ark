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
#include <chrono>
#include <utility>
#include <vector>

#include "ark/logger.hpp"
#include "ark/net/websocket_server.hpp"

namespace ark {

websocket_server::~websocket_server() {
    shutdown();
}

void websocket_server::update() {
    update_net_session();
}

bool websocket_server::start_server(std::shared_ptr<const msg_header> head, bus_id_t bus_id, const std::string& host,
    uint16_t port, uint8_t thread_num, uint32_t max_client, const size_t silent_timeout) {
    ARK_ASSERT_RET_VAL(head != nullptr, false);
    if (server_ != nullptr)
    {
        shutdown();
    }
    server_ = std::make_shared<arknet::ws_server>(
        arknet::detail::tcp_frame_size, ark_tcp_recv_buffer_size, std::max<size_t>(thread_num, 1));
    this->bus_id_ = bus_id;
    const auto weak = weak_from_this();
    const auto initial_buffer_size = get_initial_receive_buffer_size();

    server_
        ->bind_start([weak] {
            const auto ec = arknet::get_last_error();
            const auto self = weak.lock();
            if (self == nullptr)
            {
                return;
            }
            if (ec)
            {
                ARK_LOG_INFO("Server(WS) start failed, error={} errno={}", ec.message(), ec.value());
            }
            else
            {
                ARK_LOG_INFO("Server(WS) start success, listen_addr={} listen_port={}", self->server_->listen_address(),
                    self->server_->listen_port());
            }
        })
        .bind_stop([] {
            const auto ec = arknet::get_last_error();
            ARK_LOG_INFO("Server(WS) stop, error={} errno={}", ec.message(), ec.value());
        })
        .bind_accept([silent_timeout](std::shared_ptr<arknet::ws_session>& session_ptr) {
            session_ptr->set_silence_timeout(std::chrono::milliseconds(silent_timeout));
            session_ptr->set_no_delay(true);
        })
        .bind_connect([weak, head, initial_buffer_size, max_client](std::shared_ptr<arknet::ws_session>& session_ptr) {
            const auto self = weak.lock();
            if (self == nullptr)
            {
                session_ptr->stop();
                return;
            }
            ARK_LOG_INFO("Client(WS) connected, remote_addr={} remote_port={}, local_addr={} local_port={}",
                session_ptr->remote_address(), session_ptr->remote_port(), session_ptr->local_address(),
                session_ptr->local_port());

            session_ptr->ws_stream().binary(true);
            auto cur_session_id = self->trusted_session_id_++;
            bool admitted = false;
            {
                scope_w_lock guard(self->rw_lock_);
                if (max_client == 0 || self->active_session_count_ < max_client)
                {
                    session_ptr->set_user_data(cur_session_id);
                    ws_session_ptr session =
                        std::make_shared<ws_session>(head, cur_session_id, session_ptr, initial_buffer_size);
                    admitted = self->add_net_session(session);
                    if (admitted)
                    {
                        ++self->active_session_count_;
                        net_event* net_connect_event = net_event::alloc_event();
                        net_connect_event->set_id(cur_session_id);
                        net_connect_event->set_type(net_event_type::connect);
                        net_connect_event->set_bus_id(self->bus_id_);
                        net_connect_event->set_ip(session_ptr->remote_address());
                        if (!session->add_net_event(net_connect_event))
                        {
                            net_event::release(net_connect_event);
                        }
                    }
                }
            }
            if (!admitted)
            {
                session_ptr->stop();
            }
        })
        .bind_disconnect([weak](std::shared_ptr<arknet::ws_session>& session_ptr) {
            const auto ec = arknet::get_last_error();
            const auto self = weak.lock();
            if (self == nullptr)
            {
                return;
            }
            ARK_LOG_INFO("Client(WS) disconnected, client_addr={} client_port={} error={}",
                session_ptr->remote_address(), session_ptr->remote_port(), ec.message());

            auto session_id = session_ptr->get_user_data<conv_id_t>();

            scope_w_lock guard(self->rw_lock_);
            ws_session_ptr session = self->get_net_session(session_id);
            if (session == nullptr || session->need_remove())
            {
                return;
            }

            net_event* net_disconnect_event = net_event::alloc_event();
            net_disconnect_event->set_id(session_id);
            net_disconnect_event->set_type(net_event_type::disconnect);
            net_disconnect_event->set_bus_id(self->bus_id_);
            net_disconnect_event->set_ip(session_ptr->remote_address());
            session->need_remove(true);
            --self->active_session_count_;
            if (!session->add_net_event(net_disconnect_event))
            {
                net_event::release(net_disconnect_event);
            }
        })
        .bind_upgrade([](std::shared_ptr<arknet::ws_session>& session_ptr) {
            const auto ec = arknet::get_last_error();
            ARK_LOG_INFO("Client(WS) upgrade, client_addr={} client_port={} error={}", session_ptr->remote_address(),
                session_ptr->remote_port(), ec.message());
        })
        .bind_recv([weak](std::shared_ptr<arknet::ws_session>& session_ptr, std::string_view s) {
            const auto self = weak.lock();
            if (self == nullptr)
            {
                return;
            }
            auto session_id = session_ptr->get_user_data<conv_id_t>();

            // process msg data scope
            bool parsed = false;
            {
                scope_r_lock guard(self->rw_lock_);
                const ws_session_ptr session = self->get_net_session(session_id);
                if (session == nullptr || session->need_remove())
                {
                    return;
                }

                session->add_buffer(s.data(), s.length());
                parsed = session->parse_buffer_to_msg();
            }
            if (!parsed)
            {
                session_ptr->stop();
            }
        });

    bool ret = server_->start(host, port);
    set_working(ret);
    return ret;
}

void websocket_server::update_net_session() {
    std::vector<ws_session_ptr> sessions;
    {
        scope_r_lock guard(rw_lock_);
        sessions.reserve(sessions_.size());
        for (const auto& iter : sessions_)
        {
            sessions.push_back(iter.second);
        }
    }
    for (const auto& session : sessions)
    {
        ARK_ASSERT_CONTINUE(session != nullptr);
        const bool disconnected = update_net_event(session);
        update_net_msg(session);
        if (disconnected)
        {
            scope_w_lock guard(rw_lock_);
            close_session(session);
        }
    }
}

bool websocket_server::update_net_event(const ws_session_ptr& session) {
    ARK_ASSERT_RET_VAL(session != nullptr, false);

    bool disconnected = false;
    net_event* event{nullptr};
    while (session->pop_net_event(event))
    {
        disconnected = disconnected || event->get_type() == net_event_type::disconnect;
        net_event_cb_(event);
        net_event::release(event);
    }
    return disconnected;
}

void websocket_server::update_net_msg(const ws_session_ptr& session) {
    ARK_ASSERT_RET_NONE(session != nullptr);

    net_msg* msg{nullptr};
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

void websocket_server::shutdown() {
    if (server_ != nullptr)
    {
        server_->stop();
    }
    set_working(false);
}

bool websocket_server::add_net_session(const ws_session_ptr& session) {
    return sessions_.insert(std::make_pair(session->get_session_id(), session)).second;
}

void websocket_server::close_session(const ws_session_ptr& session) {
    ARK_ASSERT_RET_NONE(session != nullptr);

    auto session_id = session->get_session_id();
    sessions_.erase(session_id);
}

void websocket_server::close_session(conv_id_t session_id) {
    ws_session_ptr session;
    {
        scope_r_lock guard(rw_lock_);
        session = get_net_session(session_id);
    }
    if (session != nullptr)
    {
        session->get_session()->stop();
    }
}

void websocket_server::close_all_session() {
    std::vector<ws_session_ptr> sessions;
    {
        scope_r_lock guard(rw_lock_);
        for (const auto& entry : sessions_)
        {
            sessions.push_back(entry.second);
        }
    }
    for (const auto& session : sessions)
    {
        session->get_session()->stop();
    }
}

ws_session_ptr websocket_server::get_net_session(conv_id_t session_id) {
    auto it = sessions_.find(session_id);
    return (it != sessions_.end() ? it->second : nullptr);
}

bool websocket_server::send_msg(const char* msg, const size_t msg_len, conv_id_t session_id) {
    scope_r_lock guard(rw_lock_);
    auto session = get_net_session(session_id);
    if (session == nullptr || session->need_remove())
    {
        return false;
    }

    return session->get_session()->async_send(msg, msg_len);
}

bool websocket_server::send_msg(msg_header* head, const char* msg_data, conv_id_t session_id) {
    ARK_ASSERT_RET_VAL(head != nullptr && msg_data != nullptr, false);
    if (!head->accepts_body_size(head->message_length()))
    {
        return false;
    }

    scope_r_lock guard(rw_lock_);
    auto session = get_net_session(session_id);
    if (session == nullptr || session->need_remove())
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
    return session->get_session()->async_send(std::move(buffer));
}

void websocket_server::send_msg_to_all_client(const char* msg, size_t msg_len) {
    scope_r_lock guard(rw_lock_);
    for (const auto& it : sessions_)
    {
        auto& session = it.second;
        if (session != nullptr && !session->need_remove())
        {
            session->get_session()->async_send(msg, msg_len);
        }
    }
}

void websocket_server::broadcast_msg(msg_header* head, const char* msg_data) {
    ARK_ASSERT_RET_NONE(head != nullptr && msg_data != nullptr);
    if (!head->accepts_body_size(head->message_length()))
    {
        return;
    }

    std::string buffer;
    head->serialize_to_string(buffer);
    const auto message_length = head->message_length();
    if (buffer.empty() || !head->accepts_body_size(message_length))
    {
        return;
    }
    buffer.append(msg_data, message_length);

    send_msg_to_all_client(buffer.data(), buffer.size());
}

} // namespace ark
