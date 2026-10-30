// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <arknet/http/http_server.hpp>
#include <arknet/http/request.hpp>

#include <ark/net/http_client_module.hpp>
#include <ark/plugin_manager.hpp>
#include "owner_drain.hpp"

namespace {
using namespace std::chrono_literals;
using http_result = std::pair<bool, std::string>;

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
    require(ready(), "Timed out waiting for HTTP DSO drain");
}

class controlled_peer
{
public:
    controlled_peer() {
        server_.bind_recv([this](auto& session, arknet::http_request::request_type& request) {
            const std::string target(request.target());
            auto reply = [session, version = request.version(), target] {
                const bool first = target == "/callback-first";
                arknet::http_request::response_type response(
                    first ? arknet::http::status::service_unavailable : arknet::http::status::ok, version);
                response.body() =
                    target == "/managed" ? std::string("managed\0owned", 13) : std::string("retry\0owned", 11);
                response.prepare_payload();
                session->async_send(std::move(response));
            };
            std::lock_guard lock(mutex_);
            require(replies_.emplace(target, std::move(reply)).second, "Unexpected duplicate HTTP wire request");
            targets_.push_back(target);
        });
    }

    ~controlled_peer() {
        server_.stop();
        replies_.clear();
    }

    void start() {
        require(server_.start("127.0.0.1", 0), "Controlled HTTP peer did not start");
    }

    std::uint16_t port() const {
        return server_.get_listen_port();
    }

    bool received(const std::string& target) {
        std::lock_guard lock(mutex_);
        return replies_.contains(target);
    }

    void reply(const std::string& target) {
        std::function<void()> send;
        {
            std::lock_guard lock(mutex_);
            auto entry = replies_.find(target);
            require(entry != replies_.end(), "Controlled response released before request publication");
            send = std::move(entry->second);
            replies_.erase(entry);
        }
        send();
    }

    std::size_t count() {
        std::lock_guard lock(mutex_);
        return targets_.size();
    }

private:
    std::mutex mutex_;
    std::map<std::string, std::function<void()>> replies_;
    std::vector<std::string> targets_;
    arknet::http_server server_{1024, 1024 * 1024, 1};
};

ark::task<> accepted_request(ark::plugin_manager& manager, ark::http_client_module& client, asio::steady_timer& resume,
    std::uint16_t port, bool& entered, bool& submitted, std::future<http_result>& result) {
    entered = true;
    arknet::error_code error;
    co_await resume.async_wait(asio::redirect_error(asio::use_awaitable, error));
    require(error == asio::error::operation_aborted, "Accepted task resumed without the explicit release");
    require(manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::draining,
        "Accepted task did not resume during manager drain");
    require(manager.get_task_runtime()->get_executor().running_in_this_thread(),
        "Host coroutine left the shared owner task executor");
    // This virtual call enters the actual Net DSO, whose own Asio executor
    // context check must recognize the host-side accepted task continuation.
    result = client.get("127.0.0.1", port, "/managed");
    submitted = true;
    co_return;
}
} // namespace

int main(int argc, char* argv[]) {
    require(argc == 2, "Expected plugin directory");
    std::ofstream config("bus.toml");
    config << "[bus]\nrelations = []\n[[bus.applications]]\nname = 'game'\ntype = 31\n[[bus.processes]]\n"
              "bus_id = '1.1.31.1'\nserver_endpoint = 'tcp://127.0.0.1:1'\nintranet_endpoint = ''\n"
              "threads = 1\nmax_connections = 10\n[bus.registry]\nhost = '127.0.0.1'\nport = 0\n"
              "service_name = 'test'\ncheck_interval = '10s'\ncheck_timeout = '3s'\n";
    config.close();
    ark::logger log;
    ark::logger::init(&log);
    log.set_log_func([](spdlog::level::level_enum, const std::string& text) { std::cout << text << std::endl; });
    controlled_peer peer;
    peer.start();
    ark::plugin_manager manager;
    manager.set_bus_id(ark::bus_addr(1, 1, 31, 1).bus_id);
    require(manager.start(0, std::string(argv[1]) + "/", "",
                {{"ark_bus_plugin", "bus.toml"}, {"ark_net_plugin", ""}, {"ark_utility_plugin", ""}}),
        "Actual Net DSO startup failed: " + manager.get_startup_failure());
    auto* client = manager.find_module<ark::http_client_module>();
    require(client && client->get_plugin_manager() == &manager, "Actual HTTP module lost manager ownership");
    const auto owner = std::this_thread::get_id();
    int64_t tick = 0;
    auto update = [&] { require(manager.update(++tick), "HTTP drain owner update failed"); };
    asio::steady_timer resume(manager.get_task_runtime()->get_executor(), std::chrono::steady_clock::time_point::max());
    bool entered = false;
    bool submitted = false;
    bool task_completed = false;
    std::future<http_result> future;
    auto& tasks = manager.create_task_group(*client);
    require(tasks.start(accepted_request(manager, *client, resume, peer.port(), entered, submitted, future),
                [&](std::exception_ptr error) {
                    require(!error, "Accepted host task threw during DSO HTTP submission");
                    task_completed = true;
                }) != 0,
        "Host managed task was not admitted before stop");
    wait_for([&] { return entered; }, update);
    require(!submitted && tasks.pending() == 1, "Host task did not suspend before shutdown");

    auto receiver = std::make_shared<int>(42);
    std::weak_ptr<int> retained = receiver;
    bool retried = false;
    bool callback_completed = false;
    http_result callback_result;
    client->post(
        [&, receiver](http_result result) {
            require(std::this_thread::get_id() == owner, "Accepted HTTP callback left the owner thread");
            require(!result.first && result.second == "Service Unavailable", "Controlled first response changed");
            require(!client->ready_to_shut(), "HTTP callback was absent from active readiness accounting");
            require(manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::draining,
                "HTTP callback retry ran outside drain");
            client->post(
                [&, receiver](http_result retried_result) {
                    require(std::this_thread::get_id() == owner, "Retried callback left the owner thread");
                    require(*receiver == 42 && !client->ready_to_shut(), "Active retry receiver was released early");
                    callback_result = std::move(retried_result);
                    callback_completed = true;
                },
                "127.0.0.1", peer.port(), "/callback-retry");
            retried = true;
        },
        "127.0.0.1", peer.port(), "/callback-first");
    receiver.reset();
    wait_for([&] { return peer.received("/callback-first"); }, [] {});
    require(!manager.stop(++tick) && manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::draining,
        "Manager did not retain the suspended accepted task during admission stop");
    require(!retained.expired(), "Accepted callback receiver was released at admission stop");
    auto rejected = client->get("127.0.0.1", peer.port(), "/external-root");
    require(rejected.wait_for(0ms) == std::future_status::ready, "New external HTTP root was not rejected immediately");
    const auto rejected_result = rejected.get();
    require(!rejected_result.first &&
                rejected_result.second == arknet::error_code(asio::error::operation_aborted).message(),
        "New external HTTP root was admitted during drain");

    require(resume.cancel() == 1, "Suspended task release did not cancel exactly one timer wait");
    wait_for([&] { return submitted && task_completed; }, update);
    require(future.valid(), "Accepted host task did not obtain a DSO-created future");
    if (future.wait_for(0ms) == std::future_status::ready)
    {
        const auto early = future.get();
        require(false,
            "Actual Net DSO completed the accepted task request before its controlled wire response: " + early.second);
    }
    wait_for([&] { return peer.received("/managed"); }, [] {});
    require(!manager.stop(++tick) && !client->ready_to_shut(), "Manager tore down before accepted native IO completed");
    peer.reply("/callback-first");
    wait_for([&] { return retried; }, update);
    wait_for([&] { return peer.received("/callback-retry"); }, [] {});
    require(!retained.expired() && !callback_completed, "Retry callback ownership was lost during native IO");
    require(!manager.stop(++tick), "Manager tore down before the callback continuation completed");

    peer.reply("/managed");
    wait_for([&] { return future.wait_for(0ms) == std::future_status::ready; }, [] {});
    // Consume the DSO-created future before unload; keep only its owning value.
    const auto result = future.get();
    require(result.first && result.second == std::string("managed\0owned", 13),
        "Accepted host coroutine did not obtain its real HTTP wire response through the Net DSO");
    peer.reply("/callback-retry");
    wait_for([&] { return callback_completed; }, update);
    require(callback_result.first && callback_result.second == std::string("retry\0owned", 11),
        "Accepted callback continuation did not obtain its real HTTP wire response");
    require(retained.expired(), "Completed callback capture survived drain");
    require(peer.count() == 3, "An external root reached the peer or an accepted continuation was duplicated");
    ark::test::drain_owner(
        manager, [&] { return ++tick; }, std::chrono::steady_clock::now() + 5s, "HTTP-client actual Net DSOs");
    client = nullptr;
    require(manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::stopped && manager.stop(++tick),
        "Actual Net DSO normal/repeated shutdown did not complete");
    require(
        result.second == std::string("managed\0owned", 13) && callback_result.second == std::string("retry\0owned", 11),
        "Owning HTTP result values changed after plugin unload");
    std::cout << "Actual Net/Bus/Utility DSOs: accepted host coroutine resumed after admission stop, shared owner "
                 "executor HTTP request, external-root rejection, callback retry, binary owning results, receiver "
                 "release and manager drain/unload passed\n";
}
