// Copyright (c) 2026 OpenArkStudio. SPDX-License-Identifier: Apache-2.0

#include <atomic>
#include <chrono>
#include <future>
#include <thread>

#include "doctest.h"
#include <arknet/external/asio.hpp>
#include <arknet/external/beast.hpp>

#include "ark/plugin_manager.hpp"
#include "http/http_plugin.hpp"
#include "net/http_client_factory.hpp"

namespace {

using namespace std::chrono_literals;

std::uint16_t admission_port() {
    asio::io_context context;
    asio::ip::tcp::acceptor acceptor(context, {asio::ip::make_address("127.0.0.1"), 0});
    return acceptor.local_endpoint().port();
}

class release_route
{
public:
    explicit release_route(std::promise<void>& promise)
        : promise_(promise) {
    }

    ~release_route() {
        release();
    }

    void release() {
        if (!released_)
        {
            promise_.set_value();
            released_ = true;
        }
    }

private:
    std::promise<void>& promise_;
    bool released_ = false;
};

} // namespace

TEST_CASE("HTTP stop admission drains an accepted route before manager cleanup") {
    std::promise<void> entered;
    auto entered_future = entered.get_future();
    std::promise<void> release;
    auto released = release.get_future().share();
    std::atomic<unsigned> routed{0};
    std::atomic<bool> route_timed_out{false};
    auto server = ark::detail::make_http_server_module();
    server->get_router().add(arknet::http::verb::get, "/accepted", [&](const auto&, auto& response) {
        if (++routed == 1)
        {
            entered.set_value();
            route_timed_out = released.wait_for(5s) != std::future_status::ready;
        }
        response.body() = "accepted";
    });
    const auto port = admission_port();
    REQUIRE(server->start_server("127.0.0.1", port, 3));
    release_route route_guard(release);
    auto client = ark::detail::make_http_client_module();
    ark::plugin_manager manager;
    REQUIRE(manager.add_module("http-admission", server.get()));
    auto accepted = client->get("127.0.0.1", port, "/accepted");
    REQUIRE(entered_future.wait_for(5s) == std::future_status::ready);

    CHECK_FALSE(manager.stop(0));
    CHECK(manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::draining);
    CHECK_FALSE(server->ready_to_shut());
    CHECK(server->stop_accepting());
    auto rejected = client->get("127.0.0.1", port, "/accepted");
    REQUIRE(rejected.wait_for(5s) == std::future_status::ready);
    const auto rejection = rejected.get();
    CHECK_FALSE(rejection.first);
    CHECK(rejection.second == "Service Unavailable");
    CHECK(routed.load() == 1);
    CHECK_FALSE(server->start_server("127.0.0.1", admission_port(), 1));

    route_guard.release();
    REQUIRE(accepted.wait_for(5s) == std::future_status::ready);
    const auto response = accepted.get();
    CHECK(response.first);
    CHECK(response.second == "accepted");
    CHECK_FALSE(route_timed_out.load());
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!server->ready_to_shut() && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(1ms);
    }
    REQUIRE(server->ready_to_shut());
    CHECK(manager.stop(0));
    CHECK(manager.stop(0));
    CHECK(manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::stopped);

    auto replacement = ark::detail::make_http_server_module();
    REQUIRE(replacement->start_server("127.0.0.1", port, 1));
    CHECK(replacement->pre_shut());
}

TEST_CASE("HTTP admission drain releases a route after its peer disconnects") {
    std::promise<void> entered;
    auto entered_future = entered.get_future();
    std::promise<void> release;
    auto released = release.get_future().share();
    std::atomic<bool> route_timed_out{false};
    auto server = ark::detail::make_http_server_module();
    server->get_router().add(arknet::http::verb::get, "/disconnect", [&](const auto&, auto& response) {
        entered.set_value();
        route_timed_out = released.wait_for(5s) != std::future_status::ready;
        response.body() = "response after peer disconnect";
    });
    const auto port = admission_port();
    REQUIRE(server->start_server("127.0.0.1", port, 1));
    release_route route_guard(release);
    ark::plugin_manager manager;
    REQUIRE(manager.add_module("http-admission", server.get()));
    asio::io_context context;
    asio::ip::tcp::socket socket(context);
    socket.connect({asio::ip::make_address("127.0.0.1"), port});
    arknet::http::request<arknet::http::empty_body> request(arknet::http::verb::get, "/disconnect", 11);
    request.set(arknet::http::field::host, "127.0.0.1");
    arknet::http::write(socket, request);
    REQUIRE(entered_future.wait_for(5s) == std::future_status::ready);
    CHECK_FALSE(manager.stop(0));
    CHECK_FALSE(server->ready_to_shut());
    socket.set_option(asio::socket_base::linger(true, 0));
    socket.close();
    route_guard.release();
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!server->ready_to_shut() && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(1ms);
    }
    REQUIRE(server->ready_to_shut());
    CHECK_FALSE(route_timed_out.load());
    CHECK(manager.stop(0));
    CHECK(manager.stop(0));
}
