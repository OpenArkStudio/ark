// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "ark/kernel/data_list_impl.hpp"
#include "ark/kernel/map_module.hpp"
#include "ark/net/net_service_manager_module.hpp"
#include "guest_room_module.hpp"

namespace ark::guest_room {

void room_module::owner_thread() const {
    if (std::this_thread::get_id() != owner_)
    {
        throw std::logic_error("guest_room state accessed outside the launcher owner thread");
    }
}

bool room_module::init() {
    owner_ = std::this_thread::get_id();
    kernel_ = find_module<kernel_module>();
    return kernel_ != nullptr;
}

bool room_module::post_init() {
    owner_thread();
    auto* maps = find_module<map_module>();
    auto* network = find_module<net_service_manager_module>();
    if (maps == nullptr || network == nullptr || !maps->create_map(1) ||
        !network->create_server(60000, header(true), header(true)))
    {
        return false;
    }
    server_ = network->get_inter_net_server();
    if (server_ == nullptr)
    {
        return false;
    }
    for (const auto id : {join, move, ping, unsupported})
    {
        if (!server_->reg_msg_callback(id, this, &room_module::on_message))
        {
            return false;
        }
    }
    server_->reg_net_event_callback(this, &room_module::on_event);
    ARK_LOG_INFO("guest_room ready: lobby, 16 players, ARK1 protocol");
    return true;
}

void room_module::enqueue(peer& connection, msg_id_t id, const json& body) {
    if (connection.closing || stopping_)
    {
        return;
    }
    auto text = body.dump();
    if (text.size() > max_body || connection.pending.size() >= 8)
    {
        connection.closing = true;
        connection.pending.clear();
        return;
    }
    connection.pending.push_back({id, std::move(text)});
}

void room_module::reject(peer& connection, const std::string& code, const std::string& reason) {
    enqueue(connection, error, {{"code", code}, {"message", reason}});
}

void room_module::broadcast() {
    json players = json::array();
    for (const auto& [session, connection] : peers_)
    {
        if (connection.player != nullptr && !connection.closing)
        {
            const auto& player = *connection.player;
            players.push_back({{"id", connection.player_id}, {"name", player.get_custom_string("name")},
                {"x", player.get_custom_double("x")}, {"y", player.get_custom_double("y")},
                {"seq", player.get_custom_int64("seq")}});
        }
    }
    const json state{{"room", "lobby"}, {"players", std::move(players)}};
    for (auto& [session, connection] : peers_)
    {
        if (connection.player != nullptr)
        {
            enqueue(connection, snapshot, state);
        }
    }
}

void room_module::join_room(peer& connection, const json& body) {
    if (connection.player != nullptr)
    {
        reject(connection, "already_joined", "This session already joined the lobby");
        return;
    }
    if (!body.contains("name") || !body["name"].is_string() || !valid_name(body["name"].get<std::string>()) ||
        !body.contains("room") || body["room"] != "lobby")
    {
        reject(connection, "invalid_message", "Expected lobby and a name of 1..64 UTF-8 bytes without controls");
        return;
    }
    const auto count =
        std::count_if(peers_.begin(), peers_.end(), [](const auto& entry) { return entry.second.player != nullptr; });
    if (count >= 16)
    {
        reject(connection, "room_full", "The lobby has 16 players");
        return;
    }
    auto player = kernel_->create_entity(null_guid, 1, 0, "GuestPlayer", 0, data_list_impl());
    if (player == nullptr)
    {
        throw std::runtime_error("guest_room Kernel entity creation failed");
    }
    player->add_custom_string("name", body["name"].get<std::string>());
    player->add_custom_double("x", 80.0 + static_cast<double>((next_player_ - 1) % 8) * 100.0);
    player->add_custom_double("y", 100.0 + static_cast<double>(((next_player_ - 1) / 8) % 2) * 200.0);
    player->add_custom_int64("seq", 0);
    connection.player_id = std::to_string(next_player_++);
    connection.player = std::move(player);
    enqueue(connection, accepted,
        {{"player_id", connection.player_id}, {"name", body["name"]}, {"room", "lobby"}, {"width", 960},
            {"height", 540}});
    broadcast();
}

void room_module::move_player(peer& connection, const json& body) {
    if (connection.player == nullptr)
    {
        reject(connection, "not_joined", "Join the lobby before moving");
        return;
    }
    std::uint32_t sequence = 0;
    if (!body.contains("seq") || !uint32_value(body["seq"], sequence) || !body.contains("x") ||
        !body["x"].is_number() || !body.contains("y") || !body["y"].is_number())
    {
        reject(connection, "invalid_message", "Expected an unsigned sequence and finite coordinates");
        return;
    }
    const auto x = body["x"].get<double>();
    const auto y = body["y"].get<double>();
    if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || x > 960 || y < 0 || y > 540)
    {
        reject(connection, "invalid_message", "Coordinates must be within 0..960 and 0..540");
        return;
    }
    if (sequence <= connection.player->get_custom_int64("seq"))
    {
        reject(connection, "stale_sequence", "Movement sequence must strictly increase from 1");
        return;
    }
    connection.player->set_custom_double("x", x);
    connection.player->set_custom_double("y", y);
    connection.player->set_custom_int64("seq", sequence);
    broadcast();
}

void room_module::on_message(const net_msg* message, conv_id_t session) {
    owner_thread();
    auto found = peers_.find(session);
    if (stopping_ || found == peers_.end() || found->second.closing)
    {
        return;
    }
    auto& connection = found->second;
    const auto now = std::chrono::steady_clock::now();
    if (now - connection.window >= std::chrono::seconds(1))
    {
        connection.window = now;
        connection.messages = 0;
    }
    if (++connection.messages > 32 || ++tick_messages_ > 64)
    {
        connection.closing = true;
        return;
    }
    bool excessive_depth = false;
    const auto body = json::parse(
        message->get_msg_data(), message->get_msg_data() + message->get_msg_length(),
        [&excessive_depth](int depth, json::parse_event_t, json&) {
            excessive_depth = excessive_depth || depth > 8;
            return depth <= 8;
        },
        false);
    if (excessive_depth || body.is_discarded() || !body.is_object())
    {
        reject(connection, "invalid_message", "Expected a UTF-8 JSON object of at most eight nesting levels");
        return;
    }
    switch (message->get_msg_id())
    {
    case join:
        join_room(connection, body);
        break;
    case move:
        move_player(connection, body);
        break;
    case ping:
    {
        std::uint32_t nonce = 0;
        if (!body.contains("nonce") || !uint32_value(body["nonce"], nonce))
        {
            reject(connection, "invalid_message", "Expected an unsigned 32-bit nonce");
        }
        else
        {
            enqueue(connection, pong, {{"nonce", nonce}});
        }
        break;
    }
    default:
        reject(connection, "invalid_message", "Unsupported client message ID");
        break;
    }
}

void room_module::on_event(const net_event* event) {
    owner_thread();
    if (stopping_)
    {
        return;
    }
    if (event->get_type() == net_event_type::connect)
    {
        peers_.try_emplace(event->get_id());
    }
    else if (event->get_type() == net_event_type::disconnect)
    {
        const auto found = peers_.find(event->get_id());
        if (found != peers_.end())
        {
            const bool joined = found->second.player != nullptr;
            if (joined)
            {
                kernel_->destroy_entity(found->second.player->get_id());
            }
            peers_.erase(found);
            if (joined)
            {
                broadcast();
            }
        }
    }
}

bool room_module::update() {
    owner_thread();
    tick_messages_ = 0;
    for (auto& [session, connection] : peers_)
    {
        if (connection.closing)
        {
            server_->get_net()->close_session(session);
            continue;
        }
        while (!connection.pending.empty())
        {
            auto pending = std::move(connection.pending.front());
            connection.pending.pop_front();
            header response;
            response.set_message_id(pending.id);
            response.set_message_length(static_cast<std::uint32_t>(pending.body.size()));
            if (!server_->get_net()->send_msg(&response, pending.body.data(), session))
            {
                connection.closing = true;
                connection.pending.clear();
                break;
            }
        }
    }
    return true;
}

bool room_module::pre_shut() {
    owner_thread();
    stopping_ = true;
    for (auto& [session, connection] : peers_)
    {
        if (connection.player != nullptr)
        {
            kernel_->destroy_entity(connection.player->get_id());
        }
    }
    peers_.clear();
    server_.reset();
    return true;
}

} // namespace ark::guest_room
