// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

#include <chrono>
#include <deque>
#include <map>
#include <memory>
#include <thread>
#include <vector>

#include "ark/kernel/kernel_module.hpp"
#include "ark/module_base.hpp"
#include "ark/net/net_server_service.hpp"
#include "guest_room_wire.hpp"

namespace ark::guest_room {

namespace detail {
struct room_module_test_access;
}

class room_module final : public module_base<module>
{
public:
    bool init() override;
    bool pre_update() override;
    bool update() override;
    bool pre_shut() override;

private:
    friend struct detail::room_module_test_access;

    struct output
    {
        msg_id_t id;
        std::string body;
    };

    struct peer
    {
        std::shared_ptr<entity> player;
        std::string player_id;
        std::unique_ptr<entity_sync::scalar_publisher> publisher;
        bool needs_baseline{true};
        std::size_t pending_bytes{0};
        std::deque<output> pending;
        std::chrono::steady_clock::time_point window{std::chrono::steady_clock::now()};
        unsigned messages{0};
        bool closing{false};
    };

    void on_message(const net_msg* message, conv_id_t session);
    void on_stopping_request(const net_msg* message, conv_id_t session);
    peer* consume_message_budget(conv_id_t session);
    void on_event(const net_event* event);
    void join_room(peer& connection, const wire::join_request& body);
    void move_player(peer& connection, const wire::move_request& body);
    void act_player(peer& connection, const wire::action_request& body);
    void enqueue(peer& connection, msg_id_t id, const google::protobuf::MessageLite& body);
    void reject(peer& connection, const std::string& code, const std::string& reason);
    void flush_room();
    void retire(peer& connection);
    void owner_thread() const;

    kernel_module* kernel_{nullptr};
    std::shared_ptr<net_server_service> server_;
    std::map<conv_id_t, peer> peers_;
    std::thread::id owner_;
    std::vector<std::shared_ptr<const entity_sync::scalar_delta>> removed_;
    std::uint64_t next_player_{1};
    unsigned tick_messages_{0};
    bool stopping_{false};
};

} // namespace ark::guest_room
