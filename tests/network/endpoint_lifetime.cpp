// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <thread>
#include <type_traits>

#include "doctest.h"
#include <arknet/arknet.hpp>

#include "ark/net/rudp_client.hpp"
#include "ark/net/rudp_server.hpp"
#include "ark/net/tcp_client.hpp"
#include "ark/net/tcp_server.hpp"
#include "ark/net/websocket_client.hpp"
#include "ark/net/websocket_server.hpp"

namespace {

using namespace std::chrono_literals;

struct receiver {
    void on_message(const ark::net_msg*, ark::conv_id_t) {
    }

    void on_websocket_message(const ark::net_msg*, const ark::conv_id_t&) {
    }

    void on_event(const ark::net_event*) {
    }
};

class releasing_header final : public ark::s_msg_header {
public:
    explicit releasing_header(std::function<void()> release)
        : release_(std::move(release)) {
    }

    ark::msg_header* create() const override {
        release_();
        return new ark::s_msg_header;
    }

private:
    std::function<void()> release_;
};

template<class server_type, class client_type>
void release_server_from_receive() {
    receiver receiver;
    std::shared_ptr<server_type> server =
        std::make_shared<server_type>(&receiver, &receiver::on_message, &receiver::on_event);
    std::atomic<bool> released{};
    auto header = std::make_shared<releasing_header>([&] {
        server.reset();
        released.store(true);
    });
    asio::io_context context;
    const auto port = [&] {
        if constexpr (std::is_same_v<client_type, arknet::rudp_client>) {
            asio::ip::udp::socket listener(context, {asio::ip::udp::v4(), 0});
            return listener.local_endpoint().port();
        } else {
            asio::ip::tcp::acceptor listener(context, {asio::ip::tcp::v4(), 0});
            return listener.local_endpoint().port();
        }
    }();
    CHECK(server->get_initial_receive_buffer_size() == 4096);
    CHECK_FALSE(server->set_initial_receive_buffer_size(0));
    REQUIRE(server->set_initial_receive_buffer_size(64 * 1024));
    CHECK(server->get_initial_receive_buffer_size() == 64 * 1024);
    REQUIRE(server->start_server(header, 11, "127.0.0.1", port, 2, 10, 30000));
    CHECK_FALSE(server->set_initial_receive_buffer_size(4096));
    const std::weak_ptr weak = server;
    client_type client;
    client.set_auto_reconnect(false);
    REQUIRE(client.start("127.0.0.1", port));
    header->set_message_id(7);
    header->set_message_length(4);
    std::string frame;
    header->serialize_to_string(frame);
    frame += "test";
    REQUIRE(client.async_send(frame));
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (
        (!released.load() || !weak.expired() || client.is_started()) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(1ms);
    }
    CHECK(released.load());
    CHECK(weak.expired());
    CHECK_FALSE(client.is_started());
    client.stop();
}

template<class client_type, class server_type>
void release_client_from_receive() {
    receiver receiver;
    std::shared_ptr<client_type> client;
    if constexpr (std::is_same_v<client_type, ark::websocket_client>) {
        client = std::make_shared<client_type>(&receiver, &receiver::on_websocket_message, &receiver::on_event);
    } else {
        client = std::make_shared<client_type>(&receiver, &receiver::on_message, &receiver::on_event);
    }
    std::atomic<bool> released{};
    auto header = std::make_shared<releasing_header>([&] {
        client.reset();
        released.store(true);
    });
    header->set_message_id(7);
    header->set_message_length(4);
    std::string frame;
    header->serialize_to_string(frame);
    frame += "test";
    server_type server(1024, 1024 * 1024, 2);
    REQUIRE(server.start("127.0.0.1", 0));
    CHECK(client->get_initial_receive_buffer_size() == 4096);
    CHECK_FALSE(client->set_initial_receive_buffer_size(0));
    REQUIRE(client->set_initial_receive_buffer_size(64 * 1024));
    CHECK(client->get_initial_receive_buffer_size() == 64 * 1024);
    REQUIRE(client->start_client(header, 11, "127.0.0.1", server.listen_port()));
    const std::weak_ptr weak = client;
    const auto connected = std::chrono::steady_clock::now() + 5s;
    while (server.get_session_count() == 0 && std::chrono::steady_clock::now() < connected) {
        std::this_thread::sleep_for(1ms);
    }
    REQUIRE(server.get_session_count() == 1);
    server.foreach_session([&](auto& session) { session->async_send(frame); });
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while ((!released.load() || !weak.expired() || server.get_session_count() != 0) &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(1ms);
    }
    CHECK(released.load());
    CHECK(weak.expired());
    CHECK(server.get_session_count() == 0);
    server.stop();
}

} // namespace

TEST_CASE("Owned endpoints survive the last ARK owner being released inside receive callbacks") {
    SUBCASE("TCP server") {
        release_server_from_receive<ark::tcp_server, arknet::tcp_client>();
    }
    SUBCASE("WebSocket server") {
        release_server_from_receive<ark::websocket_server, arknet::ws_client>();
    }
    SUBCASE("RUDP server") {
        release_server_from_receive<ark::rudp_server, arknet::rudp_client>();
    }
    SUBCASE("TCP client") {
        release_client_from_receive<ark::tcp_client, arknet::tcp_server>();
    }
    SUBCASE("WebSocket client") {
        release_client_from_receive<ark::websocket_client, arknet::ws_server>();
    }
    SUBCASE("RUDP client") {
        release_client_from_receive<ark::rudp_client, arknet::rudp_server>();
    }
}
