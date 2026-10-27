#include <chrono>
#include <vector>

#include "ark/logger.hpp"
#include "ark/net/rudp_server.hpp"

namespace ark {

rudp_server::~rudp_server() {
    shutdown();
}

bool rudp_server::start_server(std::shared_ptr<const msg_header> head, bus_id_t bus_id, const std::string& host,
    uint16_t port, uint8_t thread_num, uint32_t max_client, const size_t silent_timeout) {
    if (!head || !max_client)
    {
        return false;
    }
    shutdown();
    server_ = std::make_shared<arknet::rudp_server>(65536, ark_tcp_recv_buffer_size, thread_num == 0 ? 1 : thread_num);
    this->bus_id_ = bus_id;
    arknet::arq::options transport_options;
    transport_options.mtu = 1400;
    transport_options.congestion_control = false;
    server_->set_rudp_options(transport_options).set_max_sessions(max_client);
    auto weak = weak_from_this();
    const auto initial_buffer_size = get_initial_receive_buffer_size();

    server_
        ->bind_start([weak] {
            const auto ec = arknet::get_last_error();
            auto self = weak.lock();
            if (!self)
            {
                return;
            }
            if (ec)
            {
                ARK_LOG_INFO("Server(RUDP) start failed, error={}, errno={}", ec.message(), ec.value());
            }
            else
            {
                ARK_LOG_INFO("Server(RUDP) start success, listen_addr={} listen_port={}",
                    self->server_->listen_address(), self->server_->listen_port());
            }
        })
        .bind_stop([] {
            const auto ec = arknet::get_last_error();
            ARK_LOG_INFO("Server(RUDP) stop, error={}, errno={}", ec.message(), ec.value());
        })
        .bind_handshake([](std::shared_ptr<arknet::rudp_session>& session_ptr, arknet::error_code ec) {
            if (ec)
            {
                ARK_LOG_INFO("Client(RUDP) handshake failed, remote_addr={} remote_port={} error={} errno={}",
                    session_ptr->remote_address(), session_ptr->remote_port(), ec.message(), ec.value());
            }
            else
            {
                ARK_LOG_INFO("Client(RUDP) handshake success, remote_addr={} remote_port={}",
                    session_ptr->remote_address(), session_ptr->remote_port());
            }
        })
        .bind_connect(
            [weak, head, initial_buffer_size, silent_timeout](std::shared_ptr<arknet::rudp_session>& session_ptr) {
                auto self = weak.lock();
                if (!self)
                {
                    session_ptr->stop();
                    return;
                }
                session_ptr->set_silence_timeout(std::chrono::milliseconds(silent_timeout));
                ARK_LOG_INFO("Client(RUDP) connected, remote_addr={} remote_port={} local_addr={} local_port={}",
                    session_ptr->remote_address(), session_ptr->remote_port(), session_ptr->local_address(),
                    session_ptr->local_port());

                auto cur_session_id = self->trusted_session_id_++;
                session_ptr->set_user_data(cur_session_id);

                net_event* net_connect_event = net_event::alloc_event();
                net_connect_event->set_id(cur_session_id);
                net_connect_event->set_type(net_event_type::connect);
                net_connect_event->set_bus_id(self->bus_id_);
                net_connect_event->set_ip(session_ptr->remote_address());
                udp_session_ptr session =
                    std::make_shared<udp_session>(head, cur_session_id, session_ptr, initial_buffer_size);
                if (self->add_net_session(session))
                {
                    if (!session->add_net_event(net_connect_event))
                    {
                        net_event::release(net_connect_event);
                    }
                }
                else
                {
                    net_event::release(net_connect_event);
                    session_ptr->stop();
                }
            })
        .bind_disconnect([weak](std::shared_ptr<arknet::rudp_session>& session_ptr) {
            const auto ec = arknet::get_last_error();
            auto self = weak.lock();
            if (!self)
            {
                return;
            }
            ARK_LOG_INFO("Client(RUDP) disconnected, remote_addr={} remote_port={} error={}",
                session_ptr->remote_address(), session_ptr->remote_port(), ec.message());

            auto session_id = session_ptr->get_user_data<conv_id_t>();
            auto session = self->get_net_session(session_id);
            if (!session || session->need_remove())
            {
                return;
            }

            net_event* net_disconnect_event = net_event::alloc_event();
            net_disconnect_event->set_id(session_id);
            net_disconnect_event->set_type(net_event_type::disconnect);
            net_disconnect_event->set_bus_id(self->bus_id_);
            net_disconnect_event->set_ip(session_ptr->remote_address());
            if (!session->add_net_event(net_disconnect_event))
            {
                net_event::release(net_disconnect_event);
            }
            session->need_remove(true);
        })
        .bind_recv([weak](std::shared_ptr<arknet::rudp_session>& session_ptr, std::string_view s) {
            auto self = weak.lock();
            if (!self)
            {
                return;
            }
            auto session = self->get_net_session(session_ptr->get_user_data<conv_id_t>());
            if (session && !session->need_remove())
            {
                session->add_buffer(s.data(), s.length());
                if (!session->parse_buffer_to_msg())
                {
                    session_ptr->stop();
                }
            }
        });

    bool ret = server_->start(host, port);
    set_working(ret);
    return ret;
}

void rudp_server::update() {
    update_net_session();
}

void rudp_server::update_net_session() {
    std::vector<udp_session_ptr> sessions;
    {
        scope_r_lock guard(rw_lock_);
        for (auto& iter : sessions_)
        {
            sessions.push_back(iter.second);
        }
    }
    for (auto& session : sessions)
    {
        const auto disconnected = session->need_remove();
        update_net_event(session);
        const auto drained = update_net_msg(session);
        if (disconnected && drained)
        {
            scope_w_lock guard(rw_lock_);
            close_session(session);
        }
    }
}

void rudp_server::update_net_event(const udp_session_ptr& session) {
    ARK_ASSERT_RET_NONE(session != nullptr);

    net_event* event{nullptr};
    while (session->pop_net_event(event))
    {
        net_event_cb_(event);
        net_event::release(event);
    }
}

bool rudp_server::update_net_msg(const udp_session_ptr& session) {
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

void rudp_server::shutdown() {
    if (server_)
    {
        server_->stop();
    }
    set_working(false);
}

bool rudp_server::add_net_session(const udp_session_ptr& session) {
    scope_w_lock guard(rw_lock_);
    return sessions_.insert(std::make_pair(session->get_session_id(), session)).second;
}

udp_session_ptr rudp_server::get_net_session(conv_id_t session_id) {
    scope_r_lock guard(rw_lock_);
    auto iter = sessions_.find(session_id);
    return (iter != sessions_.end() ? iter->second : nullptr);
}

void rudp_server::close_session(conv_id_t session_id) {
    auto session = get_net_session(session_id);
    if (session == nullptr)
    {
        return;
    }

    session->get_session()->stop();
}

std::string rudp_server::get_ip(const conv_id_t session_id) {
    auto session = get_net_session(session_id);
    if (session == nullptr)
    {
        return null_str;
    }

    return session->get_session()->remote_address();
}

void rudp_server::close_session(udp_session_ptr& session) {
    ARK_ASSERT_RET_NONE(session != nullptr);
    sessions_.erase(session->get_session_id());
}

bool rudp_server::send_msg(msg_header* head, const char* msg_data, conv_id_t session_id) {
    if (!head || !msg_data || !head->accepts_body_size(head->message_length()))
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
    return send_msg(buffer.data(), buffer.length(), session_id);
}

bool rudp_server::send_msg(const char* msg, size_t msg_len, conv_id_t session_id) {
    auto session = get_net_session(session_id);
    if (session == nullptr || session->need_remove())
    {
        return false;
    }

    auto transport = session->get_session();
    return transport->async_send(msg, msg_len,
        [weak = std::weak_ptr<arknet::rudp_session>(transport), session_id](arknet::error_code ec, size_t) {
            if (!ec)
            {
                return;
            }
            ARK_LOG_ERROR(
                "Server(RUDP) send failed, session={} error={} errno={}", session_id, ec.message(), ec.value());
            if (auto current = weak.lock())
            {
                current->stop();
            }
        });
}

void rudp_server::broadcast_msg(msg_header* head, const char* msg_data) {
    if (!head || !msg_data || !head->accepts_body_size(head->message_length()))
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
    send_msg_to_all(buffer.data(), buffer.length());
}

void rudp_server::send_msg_to_all(const char* msg, size_t msg_len) {
    std::vector<conv_id_t> session_ids;
    {
        scope_r_lock guard(rw_lock_);
        for (const auto& [id, session] : sessions_)
        {
            if (!session->need_remove())
            {
                session_ids.push_back(id);
            }
        }
    }
    for (auto id : session_ids)
    {
        send_msg(msg, msg_len, id);
    }
}

} // namespace ark
