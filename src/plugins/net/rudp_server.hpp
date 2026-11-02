#pragma once

#include <functional>
#include <map>
#include <memory>

#include <arknet/rudp/rudp_server.hpp>

#include "ark/net/net.hpp"
#include "ark/net/net_session.hpp"
#include "ark/uid_generator.hpp"

namespace ark {

class rudp_server final
    : public net
    , public std::enable_shared_from_this<rudp_server>
    , private noncopyable
{
    net_event_functor net_event_cb_;
    net_msg_session_functor net_msg_cb_;

    reader_writer_lock rw_lock_;
    bus_id_t bus_id_{0};
    std::atomic<conv_id_t> trusted_session_id_{ark_conv_start_id};
    std::map<conv_id_t, udp_session_ptr> sessions_;

    std::shared_ptr<arknet::rudp_server> server_;

public:
    rudp_server(net_msg_functor message_callback, net_event_functor event_callback)
        : net_event_cb_(std::move(event_callback))
        , net_msg_cb_(std::move(message_callback)) {
    }

    template<typename base_type>
    rudp_server(base_type* base_type_ptr, void (base_type::*handle_recv)(net_msg const*, conv_id_t),
        void (base_type::*handle_event)(net_event const*)) {
        net_msg_cb_ = std::bind_front(handle_recv, base_type_ptr);
        net_event_cb_ = std::bind_front(handle_event, base_type_ptr);
    }

    ~rudp_server() override;

    void update() override;

    bool start_server(std::shared_ptr<const msg_header> head, bus_id_t bus_id, const std::string& ip, uint16_t port,
        uint8_t thread_num, uint32_t max_client, const size_t silent_timeout) override;

    void shutdown() final;
    bool send_msg(msg_header* head, const char* msg_data, conv_id_t session_id) override;
    void broadcast_msg(msg_header* header, const char* msg_data) override;

    void close_session(conv_id_t session_id) override;

    std::string get_ip(const conv_id_t session_id) override;

protected:
    void send_msg_to_all(const char* msg, size_t msg_len);
    bool send_msg(const char* msg, size_t msg_len, conv_id_t session_id);

    bool add_net_session(const udp_session_ptr& session);
    void close_session(udp_session_ptr& session);
    udp_session_ptr get_net_session(conv_id_t session_id);

    void update_net_session();
    void update_net_event(const udp_session_ptr& session);
    bool update_net_msg(const udp_session_ptr& session);
};

} // namespace ark
