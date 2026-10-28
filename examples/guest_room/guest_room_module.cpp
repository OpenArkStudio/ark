// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

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

bool room_module::pre_update() {
    owner_thread();
    auto* maps = find_module<map_module>();
    auto* network = find_module<net_service_manager_module>();
    if (maps == nullptr || network == nullptr || !maps->create_map(1))
    {
        return false;
    }
    const auto profile = kernel_->get_static_entity(1);
    if (!profile || profile->get_class_name() != "GuestPlayer")
    {
        throw std::runtime_error("guest_room requires GuestPlayer Entity schema and configuration profile 1");
    }
    auto probe = kernel_->create_entity(null_guid, 1, 0, "GuestPlayer", 1, data_list_impl());
    if (!probe)
    {
        throw std::runtime_error("guest_room cannot create the configured GuestPlayer Entity");
    }
    const std::vector<std::string> fields{"name", "x", "y", "move_seq", "hp", "mp", "hp_max", "mp_max", "action_seq"};
    for (std::size_t offset = 0; offset < fields.size(); ++offset)
    {
        const auto index = static_cast<std::uint32_t>(100 + offset);
        const auto type = offset == 0   ? data_type::dt_string
                          : offset <= 2 ? data_type::dt_double
                                        : data_type::dt_uint32;
        if (probe->get_index(fields[offset]) != index || probe->get_node_type(fields[offset]) != type)
        {
            throw std::runtime_error("guest_room requires the declared GuestPlayer field: " + fields[offset]);
        }
    }
    if (!kernel_->destroy_entity(probe->get_id()))
    {
        throw std::runtime_error("guest_room cannot retire its startup schema validation Entity");
    }
    probe.reset();
    if (profile->get_u_int32("hp_max") == 0 || profile->get_u_int32("mp_max") == 0 ||
        profile->get_u_int32("hp") > profile->get_u_int32("hp_max") ||
        profile->get_u_int32("mp") > profile->get_u_int32("mp_max") || profile->get_u_int32("move_seq") != 0 ||
        profile->get_u_int32("action_seq") != 0)
    {
        throw std::runtime_error(
            "guest_room profile 1 requires bounded HP/MP, positive maxima and zero initial sequences");
    }
    if (!network->create_server(60000, header(true), header(true)))
    {
        return false;
    }
    server_ = network->get_inter_net_server();
    if (server_ == nullptr)
    {
        return false;
    }
    for (const auto id : {join, move, ping, action, unsupported})
    {
        if (!server_->reg_msg_callback(id, this, &room_module::on_message))
        {
            return false;
        }
    }
    server_->reg_net_event_callback(this, &room_module::on_event);
    ARK_LOG_INFO("guest_room ready: lobby, 16 players, ARK2 protocol");
    return true;
}

void room_module::enqueue(peer& connection, msg_id_t id, const google::protobuf::MessageLite& body) {
    if (connection.closing || stopping_)
    {
        return;
    }
    std::string text;
    if (body.ByteSizeLong() > max_body || !body.SerializeToString(&text) || connection.pending.size() >= 8 ||
        text.size() + 12 > 8 * (max_body + 12) - connection.pending_bytes)
    {
        connection.closing = true;
        connection.pending.clear();
        connection.pending_bytes = 0;
        return;
    }
    connection.pending_bytes += text.size() + 12;
    connection.pending.push_back({id, std::move(text)});
}

void room_module::reject(peer& connection, const std::string& code, const std::string& reason) {
    wire::error_response response;
    response.set_code(code);
    response.set_message(reason);
    enqueue(connection, error, response);
}

void room_module::retire(peer& connection) {
    if (!connection.player)
    {
        return;
    }
    if (connection.publisher)
    {
        removed_.push_back(connection.publisher->remove());
        connection.publisher.reset();
    }
    kernel_->destroy_entity(connection.player->get_id());
    connection.player.reset();
}

void room_module::flush_room() {
    owner_thread();
    wire::room_delta changes;
    changes.set_room("lobby");
    // Each active Entity is captured once at this explicit owner-thread flush.
    for (auto& [session, connection] : peers_)
    {
        if (connection.closing)
        {
            retire(connection);
            continue;
        }
        if (!connection.player)
        {
            continue;
        }
        auto state = kernel_->capture_scalar_snapshot(*connection.player, player_fields, player_limits);
        if (!connection.publisher)
        {
            entity_sync::scalar_baseline initial{player_schema, 1, 1, std::move(state)};
            validate_player(initial);
            connection.publisher = std::make_unique<entity_sync::scalar_publisher>(initial, player_limits);
            auto& added = *changes.add_added();
            added.set_player_id(connection.player_id);
            encode(initial, *added.mutable_state());
        }
        else
        {
            validate_player({player_schema, 1, 1, state});
            if (const auto changed = connection.publisher->update(state))
            {
                encode(*changed, *changes.add_changed());
            }
        }
    }
    for (const auto& removed : removed_)
    {
        encode(*removed, *changes.add_changed());
    }
    for (auto& [session, connection] : peers_)
    {
        if (!connection.player || connection.closing)
        {
            continue;
        }
        if (connection.needs_baseline)
        {
            wire::room_baseline baseline;
            baseline.set_room("lobby");
            for (const auto& [other_session, other] : peers_)
            {
                if (other.player && !other.closing)
                {
                    auto& added = *baseline.add_players();
                    added.set_player_id(other.player_id);
                    encode(other.publisher->baseline(), *added.mutable_state());
                }
            }
            enqueue(connection, snapshot, baseline);
            connection.needs_baseline = false;
        }
        else if (changes.added_size() != 0 || changes.changed_size() != 0)
        {
            enqueue(connection, delta, changes);
        }
    }
    removed_.clear();
}

void room_module::join_room(peer& connection, const wire::join_request& body) {
    if (connection.player != nullptr)
    {
        reject(connection, "already_joined", "This session already joined the lobby");
        return;
    }
    if (body.protocol_version() != protocol_version || !valid_name(body.name()) || body.room() != "lobby")
    {
        reject(connection, "invalid_message",
            "Expected protocol version 1, lobby and a name of 1..64 UTF-8 bytes without controls");
        return;
    }
    const auto count =
        std::count_if(peers_.begin(), peers_.end(), [](const auto& entry) { return entry.second.player != nullptr; });
    if (count >= 16)
    {
        reject(connection, "room_full", "The lobby has 16 players");
        return;
    }
    auto player = kernel_->create_entity(null_guid, 1, 0, "GuestPlayer", 1, data_list_impl());
    if (player == nullptr)
    {
        throw std::runtime_error("guest_room Kernel entity creation failed");
    }
    // Typed setters report false for unchanged values; the schema was checked at startup.
    player->set_string("name", body.name());
    player->set_double("x", 80.0 + static_cast<double>((next_player_ - 1) % 8) * 100.0);
    player->set_double("y", 100.0 + static_cast<double>(((next_player_ - 1) / 8) % 2) * 200.0);
    connection.player_id = std::to_string(next_player_++);
    connection.player = std::move(player);
    wire::join_accepted response;
    response.set_player_id(connection.player_id);
    response.set_name(body.name());
    response.set_room("lobby");
    response.set_width(960);
    response.set_height(540);
    response.set_entity_id(encode_identity(connection.player->get_id()));
    response.set_protocol_version(protocol_version);
    enqueue(connection, accepted, response);
}

void room_module::move_player(peer& connection, const wire::move_request& body) {
    if (connection.player == nullptr)
    {
        reject(connection, "not_joined", "Join the lobby before moving");
        return;
    }
    const auto sequence = body.sequence();
    const auto x = body.x();
    const auto y = body.y();
    if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || x > 960 || y < 0 || y > 540)
    {
        reject(connection, "invalid_message", "Coordinates must be within 0..960 and 0..540");
        return;
    }
    if (sequence <= connection.player->get_u_int32("move_seq"))
    {
        reject(connection, "stale_sequence", "Movement sequence must strictly increase from 1");
        return;
    }
    connection.player->set_double("x", x);
    connection.player->set_double("y", y);
    connection.player->set_u_int32("move_seq", sequence);
}

void room_module::act_player(peer& connection, const wire::action_request& body) {
    const auto sequence = body.sequence();
    const auto fail = [&](const std::string& code, const std::string& reason) {
        wire::error_response response;
        response.set_code(code);
        response.set_message(reason);
        response.set_action_sequence(sequence);
        enqueue(connection, error, response);
    };
    if (connection.player == nullptr)
    {
        fail("not_joined", "Join the lobby before using an attribute action");
        return;
    }
    const auto kind = body.kind();
    if (sequence == 0 || kind < wire::action_damage || kind > wire::action_rest)
    {
        fail("invalid_message", "Expected sequence >= 1 and action damage, skill or rest");
        return;
    }
    auto& player = *connection.player;
    if (sequence <= player.get_u_int32("action_seq"))
    {
        fail("stale_sequence", "Attribute action sequence must strictly increase from 1");
        return;
    }
    auto hp = player.get_u_int32("hp");
    auto mp = player.get_u_int32("mp");
    if (kind == wire::action_damage)
    {
        hp = hp > 15 ? hp - 15 : 0;
    }
    else if (kind == wire::action_skill)
    {
        if (mp < 20)
        {
            fail("insufficient_resource", "Skill requires at least 20 MP");
            return;
        }
        mp -= 20;
    }
    else
    {
        const auto hp_max = player.get_u_int32("hp_max");
        const auto mp_max = player.get_u_int32("mp_max");
        hp += std::min(std::uint32_t{20}, hp_max - hp);
        mp += std::min(std::uint32_t{20}, mp_max - mp);
    }
    // A clamped no-op still acknowledges the independently increasing action sequence.
    player.set_u_int32("hp", hp);
    player.set_u_int32("mp", mp);
    player.set_u_int32("action_seq", sequence);
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
    const auto parse = [&](auto& body) {
        return body.ParseFromArray(message->get_msg_data(), static_cast<int>(message->get_msg_length()));
    };
    switch (message->get_msg_id())
    {
    case join:
    {
        wire::join_request body;
        if (parse(body))
        {
            join_room(connection, body);
        }
        else
        {
            connection.closing = true;
        }
        break;
    }
    case move:
    {
        wire::move_request body;
        if (parse(body))
        {
            move_player(connection, body);
        }
        else
        {
            connection.closing = true;
        }
        break;
    }
    case action:
    {
        wire::action_request body;
        if (parse(body))
        {
            act_player(connection, body);
        }
        else
        {
            connection.closing = true;
        }
        break;
    }
    case ping:
    {
        wire::heartbeat body;
        if (parse(body))
        {
            enqueue(connection, pong, body);
        }
        else
        {
            connection.closing = true;
        }
        break;
    }
    default:
        connection.closing = true;
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
            try
            { retire(found->second); }
            catch (const std::exception& exception)
            {
                ARK_LOG_ERROR("guest_room cannot publish removal: {}", exception.what());
                for (auto& [session, connection] : peers_)
                {
                    connection.closing = true;
                }
                if (found->second.player)
                {
                    kernel_->destroy_entity(found->second.player->get_id());
                }
            }
            peers_.erase(found);
        }
    }
}

bool room_module::update() {
    owner_thread();
    tick_messages_ = 0;
    try
    { flush_room(); }
    catch (const std::exception& exception)
    {
        ARK_LOG_ERROR("guest_room scalar flush failed: {}", exception.what());
        for (auto& [session, connection] : peers_)
        {
            connection.closing = true;
        }
    }
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
            connection.pending_bytes -= pending.body.size() + 12;
            header response;
            response.set_message_id(pending.id);
            response.set_message_length(static_cast<std::uint32_t>(pending.body.size()));
            if (!server_->get_net()->send_msg(&response, pending.body.data(), session))
            {
                connection.closing = true;
                connection.pending.clear();
                connection.pending_bytes = 0;
                server_->get_net()->close_session(session);
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
    removed_.clear();
    server_.reset();
    return true;
}

} // namespace ark::guest_room
