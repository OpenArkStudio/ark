// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <typeinfo>

#include <arknet/external/beast.hpp>

#include <ark/http/http_server_module.hpp>
#include <ark/plugin_manager.hpp>

namespace {
using namespace std::chrono_literals;
using request_type = arknet::http::request<arknet::http::string_body>;
using response_type = arknet::http::response<arknet::http::string_body>;

void require(bool condition, const std::string& message) {
    if (!condition)
    {
        std::cerr << message << std::endl;
        std::_Exit(1);
    }
}

struct wire_result
{
    arknet::error_code error;
    response_type response;
};

wire_result request(uint16_t port, arknet::http::verb method, const std::string& target, std::string body = {}) {
    asio::io_context context;
    asio::ip::tcp::socket socket(context);
    wire_result result;
    socket.connect({asio::ip::make_address("127.0.0.1"), port}, result.error);
    if (result.error)
    {
        return result;
    }
    request_type request(method, target, 11);
    request.set(arknet::http::field::host, "127.0.0.1");
    request.body() = std::move(body);
    request.prepare_payload();
    arknet::http::write(socket, request, result.error);
    if (!result.error)
    {
        beast::flat_buffer buffer;
        arknet::http::read(socket, buffer, result.response, result.error);
    }
    arknet::error_code ignored;
    socket.shutdown(asio::ip::tcp::socket::shutdown_both, ignored);
    return result;
}

struct route_state
{
    explicit route_state(std::atomic<bool>& retired)
        : retired(retired) {
    }

    ~route_state() {
        require(!active.load(), "HTTP route owner was destroyed while its handler was active");
        retired.store(true);
    }

    std::atomic<bool> active{};
    std::atomic<bool> release{};
    std::atomic<bool> finished{};
    std::atomic<bool>& retired;
};
} // namespace

int main(int argc, char* argv[]) {
    require(argc == 2, "Expected plugin directory");
    asio::io_context context;
    asio::ip::tcp::acceptor reservation(context, {asio::ip::make_address("127.0.0.1"), 0});
    const auto port = reservation.local_endpoint().port();
    reservation.close();
    std::ofstream config("http.toml");
    // The listener owns one context; two session workers allow the controlled
    // blocked route and the rejection response to progress independently.
    config << "[http]\nhost = '127.0.0.1'\nport = " << port << "\nthreads = 3\n";
    config.close();
    ark::logger log;
    ark::logger::init(&log);
    log.set_log_func([](spdlog::level::level_enum, const std::string& text) { std::cout << text << std::endl; });
    ark::plugin_manager manager;
    const bool started = manager.start(0, std::string(argv[1]) + "/", "", {{"ark_http_plugin", "http.toml"}});
    require(started, "HTTP DSO startup failed: " + manager.get_startup_failure());
    auto* server = manager.find_module<ark::http_server_module>();
    require(server && server->get_plugin_manager() == &manager &&
                server->get_name() == typeid(ark::http_server_module).name(),
        "HTTP DSO interface key or manager ownership changed");
    auto& router = server->get_router();
    require(&router == &server->get_router(), "get_router must return the same owned router reference");
    const auto owner_thread = std::this_thread::get_id();
    std::atomic<bool> worker_routed{};
    std::atomic<unsigned> handled{};
    std::atomic<bool> retired{};
    auto state = std::make_shared<route_state>(retired);
    std::weak_ptr weak = state;
    router.add(arknet::http::verb::get, "/users/:id", [&](const auto& route, auto& response) {
        ++handled;
        worker_routed.store(std::this_thread::get_id() != owner_thread);
        response.body() = "user:" + std::string(route.param("id")) + ":" + std::string(route.query);
    });
    router.add(arknet::http::verb::post, "/echo",
        [](const auto& route, auto& response) { response.body() = route.request.body(); });
    router.add(arknet::http::verb::get, "/retire", [state](const auto&, auto& response) {
        state->active.store(true);
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (!state->release.load() && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(1ms);
        }
        require(state->release.load(), "Controlled HTTP route was never released");
        response.body() = "retired";
        state->active.store(false);
        state->finished.store(true);
    });
    require(server->start_server(), "HTTP configured listener failed");
    require(!server->start_server("127.0.0.1", port, 2), "Repeated start must retain its rejection");
    const auto get = request(port, arknet::http::verb::get, "/users/42?active=1");
    require(!get.error && get.response.result() == arknet::http::status::ok &&
                get.response.body() == "user:42:active=1" && get.response[arknet::http::field::server] == "ARK",
        "Actual HTTP route/status/response header changed");
    require(worker_routed.load(), "HTTP routing must remain on arknet IO workers without manager updates");
    const std::string body("route\0body", 10);
    const auto post = request(port, arknet::http::verb::post, "/echo", body);
    require(!post.error && post.response.body() == body, "Actual HTTP binary request/response changed");
    const auto missing = request(port, arknet::http::verb::get, "/missing");
    require(!missing.error && missing.response.result() == arknet::http::status::not_found,
        "Actual missing-route response changed");
    const auto helper = server->make_200<arknet::http::string_body>(
        request_type(arknet::http::verb::get, "/", 11), "ready", "text/plain");
    require(helper.body() == "ready" && helper[arknet::http::field::content_length] == "5",
        "Public HTTP response helper changed");

    wire_result pending_result;
    std::thread pending([&] { pending_result = request(port, arknet::http::verb::get, "/retire"); });
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!state->active.load() && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(1ms);
    }
    require(state->active.load(), "Pending HTTP wire request never entered its route");
    require(!manager.stop(1) && manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::draining,
        "HTTP shutdown must wait for its admitted route");
    require(!server->ready_to_shut(), "Active HTTP route was reported drained");
    const auto rejected = request(port, arknet::http::verb::get, "/users/99");
    require(!rejected.error && rejected.response.result() == arknet::http::status::service_unavailable &&
                !rejected.response.keep_alive() && handled.load() == 1,
        "HTTP stop admission must return 503 without invoking the business route");
    require(!manager.stop(1), "Repeated stop released an active route");
    state->release.store(true);
    pending.join();
    require(!pending_result.error && pending_result.response.result() == arknet::http::status::ok &&
                pending_result.response.body() == "retired",
        "An admitted route lost its response during drain");
    const auto drain_deadline = std::chrono::steady_clock::now() + 5s;
    while (!manager.stop(1) && std::chrono::steady_clock::now() < drain_deadline)
    {
        manager.update(1);
        std::this_thread::sleep_for(1ms);
    }
    require(manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::stopped,
        "HTTP owner-thread shutdown did not complete after its response send");
    require(state->finished.load(), "HTTP stop returned before its running handler retired");
    state.reset();
    require(weak.expired() && retired.load(), "HTTP router retained its handler owner after module unregistration");
    require(manager.stop(2) && manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::stopped,
        "Repeated HTTP stop failed");
    ark::plugin_manager restarted;
    require(restarted.start(0, std::string(argv[1]) + "/", "", {{"ark_http_plugin", "http.toml"}}),
        "HTTP DSO restart failed");
    require(restarted.find_module<ark::http_server_module>()->start_server(), "HTTP endpoint was not reusable");
    require(restarted.stop(1), "Restarted HTTP DSO shutdown failed");
    std::cout << "HTTP actual DSO/wire: interface key, router identity, routes/binary bodies, IO-worker dispatch, "
                 "request admission rejection, accepted-response drain, owner shutdown and endpoint reuse passed\n";
}
