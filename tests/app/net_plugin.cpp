// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <future>
#include <iostream>
#include <string>
#include <thread>
#include <typeinfo>

#include <arknet/arknet.hpp>

#include <ark/net/external_root_callback.hpp>
#include <ark/net/http_client_module.hpp>
#include <ark/net/net_service_manager_module.hpp>
#include <ark/plugin_manager.hpp>
#include "owner_drain.hpp"

namespace {
using namespace std::chrono_literals;

void require(bool condition, const std::string& message) {
    if (!condition)
    {
        std::cerr << message << std::endl;
        std::_Exit(1);
    }
}

template<class predicate, class pump>
void wait_for(predicate ready, pump update) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!ready() && std::chrono::steady_clock::now() < deadline)
    {
        update();
        std::this_thread::sleep_for(1ms);
    }
    require(ready(), "Timed out waiting for owned loopback peer");
}

arknet::error_code listener_probe(asio::io_context&, const asio::ip::tcp::endpoint& endpoint) {
    asio::io_context context;
    asio::ip::tcp::socket probe(context);
    asio::steady_timer deadline(context, 5s);
    arknet::error_code result = asio::error::operation_aborted;
    bool completed = false;
    bool timed_out = false;
    deadline.async_wait([&](const arknet::error_code& error) {
        if (!error && !completed)
        {
            timed_out = true;
            arknet::error_code ignored;
            probe.cancel(ignored);
            probe.close(ignored);
        }
    });
    probe.async_connect(endpoint, [&](const arknet::error_code& error) {
        completed = true;
        result = timed_out ? arknet::error_code(asio::error::timed_out) : error;
        deadline.cancel();
    });
    // Drain both the connect completion and the cancelled timer before their
    // stack captures go out of scope. A deadline is not a refused connection.
    context.run();
    return result;
}

struct receiver
{
    void on_message(const ark::net_msg* message, ark::conv_id_t session) {
        require(std::this_thread::get_id() == owner, "Net callback left the manager thread");
        require(session != 0 && message->get_msg_id() == 45000, "Net callback arguments changed");
        body.assign(message->get_msg_data(), message->get_msg_length());
    }

    const std::thread::id owner = std::this_thread::get_id();
    std::string body;
};

struct drain_hold final : ark::module
{
    bool ready_to_shut() const override {
        return ready;
    }

    bool ready{};
};
} // namespace

int main(int argc, char* argv[]) {
    require(argc == 2, "Expected plugin directory");
    asio::io_context context;
    asio::ip::tcp::acceptor reservation(context, {asio::ip::make_address("127.0.0.1"), 0});
    const auto port = reservation.local_endpoint().port();
    reservation.close();
    std::ofstream config("bus.toml");
    config << "[bus]\nrelations = []\n[[bus.applications]]\nname = 'game'\ntype = 31\n[[bus.processes]]\n"
              "bus_id = '1.1.31.1'\nserver_endpoint = 'tcp://127.0.0.1:"
           << port
           << "'\nintranet_endpoint = ''\nthreads = 1\nmax_connections = 10\n"
              "[bus.registry]\nhost = '127.0.0.1'\nport = 0\nservice_name = 'test'\n"
              "check_interval = '10s'\ncheck_timeout = '3s'\n";
    config.close();
    ark::logger log;
    ark::logger::init(&log);
    log.set_log_func([](spdlog::level::level_enum, const std::string& text) { std::cout << text << std::endl; });
    ark::plugin_manager manager;
    drain_hold hold;
    require(manager.add_module("net-root-drain-hold", &hold), "Could not register controlled Net drain hold");
    manager.set_bus_id(ark::bus_addr(1, 1, 31, 1).bus_id);
    require(manager.start(0, std::string(argv[1]) + "/", "",
                {{"ark_bus_plugin", "bus.toml"}, {"ark_net_plugin", ""}, {"ark_utility_plugin", ""}}),
        "Net DSO startup failed: " + manager.get_startup_failure());
    auto* network = manager.find_module<ark::net_service_manager_module>();
    auto* http = manager.find_module<ark::http_client_module>();
    require(network && http, "Net public interface lookup failed");
    require(network->get_plugin_manager() == &manager && http->get_plugin_manager() == &manager,
        "Net modules lost manager ownership");
    require(network->get_name() == typeid(ark::net_service_manager_module).name() &&
                http->get_name() == typeid(ark::http_client_module).name(),
        "Net registration changed interface RTTI keys");
    require(network->create_server(), "Net manager could not start its configured TCP listener");
    auto server = network->get_inter_net_server();
    require(server && !network->get_intra_net_server(), "Configured Net listener selection changed");
    receiver received;
    require(server->reg_msg_callback(45000, &received, &receiver::on_message), "Net message callback rejected");
    unsigned roots_accepted{}, roots_rejected{};
    require(
        ark::reg_external_root_callback(
            *server, manager, 45001,
            [&](const ark::net_msg* message, ark::conv_id_t session) {
                require(std::this_thread::get_id() == received.owner && session != 0 && message->get_msg_id() == 45001,
                    "External-root acceptance arguments or owner changed");
                ++roots_accepted;
            },
            [&](const ark::net_msg* message, ark::conv_id_t session) {
                require(std::this_thread::get_id() == received.owner && session != 0 && message->get_msg_id() == 45001,
                    "External-root rejection arguments or owner changed");
                // This test's local rejection policy records the refusal.
                // It does not close the session or discard pending output.
                ++roots_rejected;
            }),
        "External-root callback registration failed");
    std::weak_ptr<ark::net> endpoint = server->get_net();
    const asio::ip::tcp::endpoint listener_endpoint(asio::ip::make_address("127.0.0.1"), port);
    const auto live_probe = listener_probe(context, listener_endpoint);
    require(!live_probe, "Live Net listener negative control failed: " + live_probe.message());
    std::atomic<bool> peer_closed{};
    arknet::error_code peer_close_error;
    arknet::tcp_client client;
    client.set_auto_reconnect(false);
    client.bind_disconnect([&] {
        peer_close_error = arknet::get_last_error();
        peer_closed.store(true);
    });
    require(client.start("127.0.0.1", port), "Owned TCP peer failed to start");
    ark::s_msg_header header;
    header.set_message_id(45000);
    header.set_message_length(4);
    std::string frame;
    header.serialize_to_string(frame);
    frame += "test";
    require(client.async_send(frame), "Owned TCP peer could not enqueue its frame");
    require(received.body.empty(), "Net callback fired without a manager update");
    int64_t tick{};
    auto update = [&] { require(manager.update(++tick), "Net manager update failed"); };
    wait_for([&] { return received.body == "test"; }, update);
    auto send_root = [&] {
        ark::s_msg_header root_header;
        root_header.set_message_id(45001);
        std::string root_frame;
        root_header.serialize_to_string(root_frame);
        require(client.async_send(root_frame), "Owned TCP peer could not enqueue its root frame");
    };
    send_root();
    wait_for([&] { return roots_accepted == 1; }, update);
    require(roots_rejected == 0, "Running manager rejected external root");

    arknet::http_server peer(1024, 1024 * 1024, 1);
    std::atomic<bool> answered{};
    std::atomic<bool> pending_received{};
    peer.bind_recv([&](auto& session, arknet::http_request::request_type& request) {
        if (request.target() == "/pending")
        {
            pending_received.store(true);
            return;
        }
        arknet::http_request::response_type response(arknet::http::status::ok, request.version());
        response.body() = "net-http";
        response.prepare_payload();
        session->async_send(std::move(response));
        answered.store(true);
    });
    require(peer.start("127.0.0.1", 0), "Owned HTTP peer failed to start");
    auto independent = http->get("127.0.0.1", peer.get_listen_port(), "/future");
    wait_for([&] { return independent.wait_for(0ms) == std::future_status::ready; }, [] {});
    const auto independent_result = independent.get();
    require(independent_result.first && independent_result.second == "net-http",
        "HTTP DSO future must complete without manager updates");
    bool completed{};
    http->post(
        [&](std::pair<bool, std::string> result) {
            require(std::this_thread::get_id() == received.owner, "HTTP callback left the manager thread");
            require(result.first && result.second == "net-http", "HTTP DSO response changed");
            completed = true;
        },
        "127.0.0.1", peer.get_listen_port(), "/net");
    wait_for([&] { return answered.load(); }, [] {});
    require(!completed, "HTTP callback fired without a manager update");
    wait_for([&] { return completed; }, update);
    auto pending = http->get("127.0.0.1", peer.get_listen_port(), "/pending");
    wait_for([&] { return pending_received.load(); }, [] {});
    require(pending.wait_for(0ms) != std::future_status::ready, "Controlled pending HTTP request completed early");
    // Consume the DSO-created future while its implementation library remains
    // loaded; retaining future state across arbitrary unload is not this test.
    require(http->pre_shut(), "HTTP DSO pre_shut failed");
    require(pending.wait_for(0ms) == std::future_status::ready, "HTTP DSO shutdown did not settle the pending future");
    const auto cancelled = pending.get();
    require(!cancelled.first && cancelled.second == arknet::error_code(asio::error::operation_aborted).message(),
        "HTTP DSO shutdown changed pending-request cancellation");
    peer.stop();
    require(client.is_started() && !peer_closed.load(), "Owned TCP peer must remain live across Net shutdown");
    require(!manager.stop(++tick) && manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::draining,
        "Controlled readiness hold did not retain Net in draining");
    send_root();
    frame.replace(frame.size() - 4, 4, "done");
    require(client.async_send(frame), "Owned TCP peer could not enqueue an ordinary drain callback");
    wait_for([&] { return roots_rejected == 1 && received.body == "done"; }, update);
    require(roots_accepted == 1 && !peer_closed.load(),
        "Root gate admitted new work or closed the ordinary callback session");
    hold.ready = true;
    server.reset();
    ark::test::drain_owner(manager, [&] { return ++tick; }, std::chrono::steady_clock::now() + 5s, "Net actual DSOs");
    require(endpoint.expired(), "Net listener survived normal module unregistration");
    require(manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::stopped,
        "Net resources were retained instead of normally stopped");
    // Keep the peer open until the server closes its accepted socket. Stopping
    // the client first would hide a retained server-side connection.
    wait_for([&] { return peer_closed.load(); }, [] {});
    require(peer_close_error == asio::error::eof || peer_close_error == asio::error::connection_reset,
        "Net shutdown did not remotely close the owned TCP peer: " + peer_close_error.message());
    client.stop();
    require(manager.stop(++tick), "Repeated Net shutdown failed");
    // Binding the same port also tests unrelated kernel port-reuse state. A
    // refused new connection checks listener closure directly, with no retry.
    // The successful probe above is the negative control for a live listener.
    const auto closed_probe = listener_probe(context, listener_endpoint);
    require(closed_probe == asio::error::connection_refused,
        "Net listener closure probe must be refused: " + closed_probe.message());
    std::cout << "Net actual DSOs: interface RTTI lookup, manager-owned TCP/HTTP callbacks, explicit root admission "
                 "and ordinary dispatch during drain, listener refusal, remote "
                 "peer closure and "
                 "normal repeated shutdown passed\n";
}
