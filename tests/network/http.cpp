// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "doctest.h"
#include <arknet/http/http_server.hpp>
#include <arknet/http/request.hpp>

#include "ark/plugin_manager.hpp"
#include "http/http_plugin.hpp"
#include "net/http_client_factory.hpp"

namespace {

using namespace std::chrono_literals;
using http_request = arknet::http_request::request_type;
using http_response = arknet::http_request::response_type;
using http_result = std::pair<bool, std::string>;

class http_plugin_configuration
{
public:
    explicit http_plugin_configuration(const std::string& text)
        : previous_(ark::http_plugin::get_plugin_conf())
        , path_(std::filesystem::temp_directory_path() /
                ("ark-http-plugin-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                    ".toml")) {
        std::ofstream output(path_, std::ios::binary);
        output.exceptions(std::ios::badbit | std::ios::failbit);
        output << text;
        output.close();
        plugin_.set_plugin_conf(path_.string());
    }

    ~http_plugin_configuration() {
        plugin_.set_plugin_conf(previous_);
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }

private:
    std::string previous_;
    std::filesystem::path path_;
    ark::http_plugin plugin_;
};

template<class predicate_type, class pump_type>
bool wait_http(predicate_type predicate, pump_type pump, std::chrono::milliseconds timeout = 5s) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    do
    {
        pump();
        if (predicate())
        {
            return true;
        }
        std::this_thread::sleep_for(1ms);
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

template<class predicate_type>
bool wait_http(predicate_type predicate, std::chrono::milliseconds timeout = 5s) {
    return wait_http(std::move(predicate), [] {}, timeout);
}

http_result await_http(std::future<http_result>& future) {
    REQUIRE(future.wait_for(5s) == std::future_status::ready);
    return future.get();
}

class http_peer
{
public:
    explicit http_peer(bool reply = true)
        : reply_(reply) {
        server_.bind_recv([this](auto& session, http_request& request) {
            auto reply = [this, session, request] {
                http_response response(arknet::http::status::ok, request.version());
                response.keep_alive(request.keep_alive());
                if (respond)
                {
                    respond(request, response);
                }
                else
                {
                    response.body() = request.body();
                }
                response.prepare_payload();
                session->async_send(std::move(response));
            };
            if (reply_)
            {
                {
                    std::lock_guard lock(mutex_);
                    requests_.push_back(request);
                }
                reply();
            }
            else
            {
                std::lock_guard lock(mutex_);
                replies_.push_back(std::move(reply));
                requests_.push_back(request);
            }
        });
    }

    ~http_peer() {
        server_.stop();
        replies_.clear();
    }

    bool start() {
        return server_.start("127.0.0.1", 0);
    }

    std::uint16_t port() const {
        return server_.get_listen_port();
    }

    std::vector<http_request> requests() {
        std::lock_guard lock(mutex_);
        return requests_;
    }

    std::size_t count() {
        std::lock_guard lock(mutex_);
        return requests_.size();
    }

    void reply_pending() {
        std::vector<std::function<void()>> replies;
        {
            std::lock_guard lock(mutex_);
            replies.swap(replies_);
        }
        for (auto& reply : replies)
        {
            reply();
        }
    }

    std::function<void(const http_request&, http_response&)> respond;

private:
    const bool reply_;
    std::mutex mutex_;
    std::vector<http_request> requests_;
    std::vector<std::function<void()>> replies_;
    arknet::http_server server_{1024, 16 * 1024 * 1024, 2};
};

struct http_callbacks
{
    void receive(http_result result) {
        if (std::this_thread::get_id() != update_thread)
        {
            wrong_thread.store(true);
        }
        std::lock_guard lock(mutex);
        results.push_back(std::move(result));
    }

    std::vector<http_result> result_snapshot() {
        std::lock_guard lock(mutex);
        return results;
    }

    const std::thread::id update_thread = std::this_thread::get_id();
    std::atomic<bool> wrong_thread{false};
    std::mutex mutex;
    std::vector<http_result> results;
};

ark::task<> managed_http_request(
    ark::http_client_module& client, std::uint16_t port, std::future<http_result>& result) {
    // Rejection delivery must not inherit the task executor's continuation scope.
    client.update();
    result = client.get("127.0.0.1", port, "/managed-follow-up");
    co_return;
}

std::uint16_t http_port() {
    asio::io_context context;
    asio::ip::tcp::acceptor acceptor(context, {asio::ip::make_address("127.0.0.1"), 0});
    return acceptor.local_endpoint().port();
}
} // namespace

TEST_CASE("HTTP interface factories own independent request runtimes") {
    http_peer peer;
    REQUIRE(peer.start());
    auto stopped = ark::detail::make_http_client_module();
    auto active = ark::detail::make_http_client_module();
    REQUIRE(stopped->pre_shut());
    auto rejected = stopped->get("127.0.0.1", peer.port(), "/stopped");
    CHECK_FALSE(await_http(rejected).first);
    auto accepted = active->get("127.0.0.1", peer.port(), "/active");
    CHECK(await_http(accepted).first);
    CHECK(peer.count() == 1);
}

TEST_CASE("HTTP concurrent futures keep each request and response independent") {
    http_peer peer;
    peer.respond = [](const http_request& request, http_response& response) {
        response.body() =
            std::string(request.method_string()) + ":" + std::string(request.target()) + ":" + request.body();
    };
    REQUIRE(peer.start());
    auto owned_client = ark::detail::make_http_client_module();
    auto& client = *owned_client;
    std::vector<std::future<http_result>> futures;
    std::vector<std::string> expected;
    for (int index = 0; index < 48; ++index)
    {
        const auto target = "/request/" + std::to_string(index);
        if (index % 2 == 0)
        {
            const auto body = "body-" + std::to_string(index);
            futures.push_back(client.post("localhost", peer.port(), target, nullptr, &body));
            expected.push_back("POST:" + target + ":" + body);
        }
        else
        {
            futures.push_back(client.get("localhost", peer.port(), target));
            expected.push_back("GET:" + target + ":");
        }
    }
    for (std::size_t index = 0; index < futures.size(); ++index)
    {
        const auto result = await_http(futures[index]);
        CHECK(result.first);
        CHECK(result.second == expected[index]);
    }
    CHECK(peer.count() == futures.size());
}

TEST_CASE("HTTP requests own binary bodies query parameters and headers") {
    http_peer peer;
    REQUIRE(peer.start());
    auto owned_client = ark::detail::make_http_client_module();
    auto& client = *owned_client;
    std::string body("first\0second", 12);
    const auto original_body = body;
    std::map<std::string, std::string> parameters{{"reserved", "a&= b"}, {"utf8", "\xe4\xb8\xad"}};
    std::vector<std::string> cookies{"session=original", "locale=en"};
    std::string content_type = "application/octet-stream";
    auto future =
        client.post("localhost", peer.port(), "/echo?existing=yes", &parameters, &body, &content_type, &cookies);
    body = "changed";
    parameters.clear();
    cookies.clear();
    content_type.clear();
    const auto result = await_http(future);
    CHECK(result.first);
    CHECK(result.second == original_body);
    const auto requests = peer.requests();
    REQUIRE(requests.size() == 1);
    CHECK(requests.front().target() == "/echo?existing=yes&reserved=a%26%3D%20b&utf8=%E4%B8%AD");
    CHECK(requests.front().body() == original_body);
    CHECK(requests.front()[arknet::http::field::content_type] == "application/octet-stream");
    CHECK(requests.front()[arknet::http::field::cookie] == "session=original; locale=en");
    CHECK(requests.front()[arknet::http::field::host] == "localhost:" + std::to_string(peer.port()));
    CHECK(requests.front()[arknet::http::field::content_length] == "12");
    CHECK_FALSE(requests.front().keep_alive());

    const std::map<std::string, std::string> empty_parameters;
    auto empty_query = client.get("127.0.0.1", peer.port(), "/unchanged", &empty_parameters);
    CHECK(await_http(empty_query).first);
    CHECK(peer.requests().back().target() == "/unchanged");
    auto empty_target = client.get("127.0.0.1", peer.port(), "");
    CHECK(await_http(empty_target).first);
    CHECK(peer.requests().back().target() == "/");
}

TEST_CASE("HTTP callbacks run once on the Update thread for both service overloads") {
    http_peer peer;
    REQUIRE(peer.start());
    http_callbacks callbacks;
    auto owned_client = ark::detail::make_http_client_module();
    auto& client = *owned_client;
    const std::string body = "callback-body";
    for (int index = 0; index < 12; ++index)
    {
        auto callback = [&](http_result result) { callbacks.receive(std::move(result)); };
        if (index % 2 == 0)
        {
            client.post(callback, "localhost", peer.port(), "/callback", nullptr, &body);
        }
        else
        {
            client.post(callback, "localhost", std::to_string(peer.port()), "/callback", nullptr, &body);
        }
    }
    REQUIRE(wait_http([&] { return peer.count() == 12; }));
    std::this_thread::sleep_for(100ms);
    CHECK(callbacks.result_snapshot().empty());
    REQUIRE(wait_http([&] { return callbacks.result_snapshot().size() == 12; }, [&] { client.update(); }));
    for (const auto& result : callbacks.result_snapshot())
    {
        CHECK(result.first);
        CHECK(result.second == body);
    }
    CHECK_FALSE(callbacks.wrong_thread.load());
    for (int index = 0; index < 3; ++index)
    {
        client.update();
    }
    CHECK(callbacks.result_snapshot().size() == 12);
}

TEST_CASE("HTTP preserves non-200 failure reasons and arbitrary request verbs") {
    http_peer peer;
    peer.respond = [](const http_request& request, http_response& response) {
        response.result(arknet::http::status::not_found);
        response.reason("Missing business route");
        response.body() = std::string(request.method_string()) + " error body";
    };
    REQUIRE(peer.start());
    auto owned_client = ark::detail::make_http_client_module();
    auto& client = *owned_client;
    auto missing = client.get("127.0.0.1", peer.port(), "/missing");
    auto put = client.async_request(arknet::http::verb::put, "127.0.0.1", peer.port(), "/put");
    auto remove = client.async_request(arknet::http::verb::delete_, "127.0.0.1", peer.port(), "/delete");
    for (auto* future : {&missing, &put, &remove})
    {
        const auto result = await_http(*future);
        CHECK_FALSE(result.first);
        CHECK(result.second == "Missing business route");
    }
    REQUIRE(peer.count() == 3);
}

TEST_CASE("HTTP IO timeout completes futures and queues callback failure exactly once") {
    http_peer peer(false);
    REQUIRE(peer.start());
    http_callbacks callbacks;
    auto owned_client = ark::detail::make_http_client_module();
    auto& client = *owned_client;
    const auto start = std::chrono::steady_clock::now();
    auto future = client.get("127.0.0.1", peer.port(), "/stall", nullptr, nullptr, 1, 1);
    client.post([&](http_result result) { callbacks.receive(std::move(result)); }, "127.0.0.1", peer.port(), "/stall",
        nullptr, nullptr, nullptr, nullptr, 1, 1);
    REQUIRE(wait_http([&] { return peer.count() == 2; }));
    const auto result = await_http(future);
    CHECK_FALSE(result.first);
    CHECK(result.second == arknet::error_code(asio::error::timed_out).message());
    CHECK(std::chrono::steady_clock::now() - start >= 900ms);
    CHECK(callbacks.result_snapshot().empty());
    REQUIRE(wait_http([&] { return callbacks.result_snapshot().size() == 1; }, [&] { client.update(); }));
    CHECK_FALSE(callbacks.result_snapshot().front().first);
    CHECK(callbacks.result_snapshot().front().second == result.second);
    CHECK_FALSE(callbacks.wrong_thread.load());
    client.pre_shut();
    client.update();
    CHECK(callbacks.result_snapshot().size() == 1);
}

TEST_CASE("HTTP shutdown cancels all pending requests and settles futures without Update") {
    http_peer peer(false);
    REQUIRE(peer.start());
    http_callbacks callbacks;
    auto owned_client = ark::detail::make_http_client_module();
    auto& client = *owned_client;
    std::vector<std::future<http_result>> futures;
    for (int index = 0; index < 24; ++index)
    {
        futures.push_back(client.get("127.0.0.1", peer.port(), "/pending"));
    }
    for (int index = 0; index < 8; ++index)
    {
        client.post(
            [&](http_result result) { callbacks.receive(std::move(result)); }, "127.0.0.1", peer.port(), "/pending");
    }
    REQUIRE(wait_http([&] { return peer.count() == 32; }));
    const auto start = std::chrono::steady_clock::now();
    REQUIRE(client.pre_shut());
    CHECK(std::chrono::steady_clock::now() - start < 2s);
    CHECK(callbacks.result_snapshot().empty());
    const auto aborted = arknet::error_code(asio::error::operation_aborted).message();
    for (auto& future : futures)
    {
        CHECK(future.wait_for(0ms) == std::future_status::ready);
        const auto result = await_http(future);
        CHECK_FALSE(result.first);
        CHECK(result.second == aborted);
    }
    client.update();
    REQUIRE(callbacks.result_snapshot().size() == 8);
    for (const auto& result : callbacks.result_snapshot())
    {
        CHECK_FALSE(result.first);
        CHECK(result.second == aborted);
    }
    CHECK_FALSE(callbacks.wrong_thread.load());
    CHECK(client.pre_shut());
    auto rejected = client.get("127.0.0.1", peer.port(), "/after-shutdown");
    CHECK(rejected.wait_for(0ms) == std::future_status::ready);
    CHECK(await_http(rejected).second == aborted);
    client.post(
        [&](http_result result) { callbacks.receive(std::move(result)); }, "127.0.0.1", peer.port(), "/after-shutdown");
    CHECK(callbacks.result_snapshot().size() == 8);
    client.update();
    REQUIRE(callbacks.result_snapshot().size() == 9);
    CHECK(callbacks.result_snapshot().back().second == aborted);
    CHECK(peer.count() == 32);

    std::future<http_result> destroyed;
    {
        auto owned = ark::detail::make_http_client_module();
        destroyed = owned->get("127.0.0.1", peer.port(), "/destroyed");
        REQUIRE(wait_http([&] { return peer.count() == 33; }));
    }
    CHECK(destroyed.wait_for(0ms) == std::future_status::ready);
    CHECK(await_http(destroyed).second == aborted);
}

TEST_CASE("HTTP admission stop rejects roots while accepted requests and callbacks drain") {
    http_peer peer(false);
    REQUIRE(peer.start());
    auto owned_client = ark::detail::make_http_client_module();
    auto& client = *owned_client;
    auto lifetime = std::make_shared<int>(42);
    std::weak_ptr<int> retained = lifetime;
    std::future<http_result> follow_up;
    unsigned completed = 0;
    auto accepted = client.get("127.0.0.1", peer.port(), "/accepted-future");
    client.post(
        [&, lifetime](http_result result) {
            CHECK(result.first);
            CHECK_FALSE(client.ready_to_shut());
            CHECK(*lifetime == 42);
            ++completed;
            follow_up = client.get("127.0.0.1", peer.port(), "/callback-follow-up");
        },
        "127.0.0.1", peer.port(), "/accepted-callback");
    lifetime.reset();
    REQUIRE(wait_http([&] { return peer.count() == 2; }));
    CHECK(client.stop_accepting());
    CHECK(client.stop_accepting());
    CHECK_FALSE(client.ready_to_shut());
    CHECK_FALSE(retained.expired());
    const auto aborted = arknet::error_code(asio::error::operation_aborted).message();
    auto get = client.get("127.0.0.1", peer.port(), "/rejected-get");
    auto post = client.post("127.0.0.1", peer.port(), "/rejected-post");
    auto request = client.async_request(arknet::http::verb::put, "127.0.0.1", peer.port(), "/rejected-request");
    for (auto* future : {&get, &post, &request})
    {
        REQUIRE(future->wait_for(0ms) == std::future_status::ready);
        CHECK(await_http(*future) == http_result{false, aborted});
    }
    unsigned rejected = 0;
    auto rejection = [&](http_result result) {
        CHECK(result == http_result{false, aborted});
        CHECK_FALSE(client.ready_to_shut());
        auto nested = client.get("127.0.0.1", peer.port(), "/rejection-follow-up");
        CHECK(await_http(nested) == http_result{false, aborted});
        ++rejected;
    };
    client.post(rejection, "127.0.0.1", peer.port(), "/rejected-callback-port");
    client.post(rejection, "127.0.0.1", std::to_string(peer.port()), "/rejected-callback-service");
    CHECK(rejected == 0);
    REQUIRE(client.update());
    CHECK(rejected == 2);
    CHECK(completed == 0);
    CHECK(peer.count() == 2);
    peer.reply_pending();
    CHECK(await_http(accepted).first);
    REQUIRE(wait_http([&] { return completed == 1; }, [&] { client.update(); }));
    CHECK(retained.expired());
    REQUIRE(wait_http([&] { return peer.count() == 3; }));
    CHECK_FALSE(client.ready_to_shut());
    peer.reply_pending();
    CHECK(await_http(follow_up).first);
    REQUIRE(wait_http([&] { return client.ready_to_shut(); }));
    CHECK(client.pre_shut());
    CHECK(client.pre_shut());
    CHECK(client.ready_to_shut());
    CHECK(peer.count() == 3);
}

TEST_CASE("HTTP manager drain permits provider work from an accepted managed task") {
    http_peer peer(false);
    REQUIRE(peer.start());
    auto owned_client = ark::detail::make_http_client_module();
    auto& client = *owned_client;
    ark::plugin_manager manager;
    client.set_name("http-client");
    client.set_plugin_manager(&manager);
    REQUIRE(manager.add_module(client.get_name(), &client));
    auto& tasks = manager.create_task_group(client);
    std::future<http_result> accepted;
    REQUIRE(tasks.start(managed_http_request(client, peer.port(), accepted)) != 0);
    CHECK_FALSE(manager.stop(1));
    CHECK(manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::draining);
    auto rejected = client.get("127.0.0.1", peer.port(), "/external-root");
    CHECK_FALSE(await_http(rejected).first);
    unsigned rejected_callbacks = 0;
    client.post(
        [&](http_result result) {
            CHECK_FALSE(result.first);
            auto nested = client.get("127.0.0.1", peer.port(), "/rejected-from-task-delivery");
            REQUIRE(nested.wait_for(0ms) == std::future_status::ready);
            CHECK_FALSE(await_http(nested).first);
            ++rejected_callbacks;
        },
        "127.0.0.1", peer.port(), "/rejected-callback");
    REQUIRE(manager.update(2));
    CHECK(rejected_callbacks == 1);
    REQUIRE(accepted.valid());
    REQUIRE(wait_http([&] { return peer.count() == 1; }));
    CHECK(manager.pending_tasks() == 0);
    CHECK_FALSE(manager.stop(3));
    CHECK(manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::draining);
    CHECK_FALSE(client.ready_to_shut());
    peer.reply_pending();
    CHECK(await_http(accepted).first);
    REQUIRE(wait_http([&] { return manager.stop(4); }));
    CHECK(manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::stopped);
    CHECK(manager.stop(5));
    CHECK_FALSE(manager.update(6));
    CHECK(peer.count() == 1);
}

TEST_CASE("HTTP drain permission requires the manager poll instead of raw runtime or group polls") {
    for (bool group_poll : {false, true})
    {
        auto owned_client = ark::detail::make_http_client_module();
        auto& client = *owned_client;
        ark::plugin_manager manager;
        client.set_name("http-client");
        client.set_plugin_manager(&manager);
        REQUIRE(manager.add_module(client.get_name(), &client));
        auto& tasks = manager.create_task_group(client);
        std::future<http_result> future;
        REQUIRE(tasks.start(managed_http_request(client, 1, future)) != 0);
        CHECK_FALSE(manager.stop(1));
        CHECK_FALSE(manager.is_polling_tasks());
        if (group_poll)
        {
            tasks.poll();
        }
        else
        {
            manager.get_task_runtime()->poll();
        }
        REQUIRE(future.valid());
        REQUIRE(future.wait_for(0ms) == std::future_status::ready);
        const auto result = future.get();
        CHECK_FALSE(result.first);
        CHECK(result.second == arknet::error_code(asio::error::operation_aborted).message());
        CHECK_FALSE(manager.is_polling_tasks());
        CHECK(manager.pending_tasks() == 0);
        CHECK(client.ready_to_shut());
        CHECK(manager.stop(2));
    }
}

TEST_CASE("HTTP manager timeout retains requests and callback owners for later updates") {
    http_peer peer(false);
    REQUIRE(peer.start());
    auto owned_client = ark::detail::make_http_client_module();
    auto& client = *owned_client;
    ark::plugin_manager manager;
    client.set_name("http-client");
    client.set_plugin_manager(&manager);
    REQUIRE(manager.add_module(client.get_name(), &client));
    REQUIRE(manager.add_update_module(&client));
    auto lifetime = std::make_shared<int>(1);
    std::weak_ptr<int> retained = lifetime;
    unsigned delivered = 0;
    client.post(
        [&, lifetime](http_result result) {
            CHECK(result.first);
            CHECK(*lifetime == 1);
            CHECK_FALSE(client.ready_to_shut());
            ++delivered;
        },
        "127.0.0.1", peer.port(), "/timeout-retention");
    lifetime.reset();
    REQUIRE(wait_http([&] { return peer.count() == 1; }));
    CHECK_FALSE(manager.stop(1));
    CHECK_FALSE(manager.stop_until(2, std::chrono::steady_clock::now()));
    CHECK(manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::timed_out);
    CHECK_FALSE(retained.expired());
    CHECK_FALSE(client.ready_to_shut());
    CHECK(delivered == 0);
    peer.reply_pending();
    REQUIRE(wait_http([&] { return delivered == 1; }, [&] { CHECK(manager.update(3)); }));
    CHECK(retained.expired());
    REQUIRE(wait_http([&] { return client.ready_to_shut(); }));
    CHECK_FALSE(manager.stop(4));
    CHECK(manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::timed_out);
    CHECK(client.pre_shut());
}

TEST_CASE("HTTP manager failure retains IO results and callback receivers without automatic dispatch") {
    struct failed_readiness : ark::module
    {
        bool ready_to_shut() const override {
            throw std::runtime_error("readiness failure");
        }
    } failure;

    http_peer peer(false);
    REQUIRE(peer.start());
    auto owned_client = ark::detail::make_http_client_module();
    auto& client = *owned_client;
    ark::plugin_manager manager;
    client.set_name("http-client");
    client.set_plugin_manager(&manager);
    REQUIRE(manager.add_module(client.get_name(), &client));
    REQUIRE(manager.add_update_module(&client));
    REQUIRE(manager.add_module("failure", &failure));
    auto lifetime = std::make_shared<int>(1);
    std::weak_ptr<int> retained = lifetime;
    unsigned delivered = 0;
    auto future = client.get("127.0.0.1", peer.port(), "/failed-manager-future");
    client.post(
        [&, lifetime](http_result result) {
            CHECK(result.first);
            CHECK(*lifetime == 1);
            CHECK_FALSE(client.ready_to_shut());
            ++delivered;
        },
        "127.0.0.1", peer.port(), "/failed-manager-callback");
    lifetime.reset();
    REQUIRE(wait_http([&] { return peer.count() == 2; }));
    CHECK_FALSE(manager.stop(1));
    CHECK(manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::failed);
    CHECK_FALSE(client.ready_to_shut());
    CHECK_FALSE(retained.expired());
    peer.reply_pending();
    CHECK(await_http(future).first);
    CHECK_FALSE(manager.update(2));
    CHECK(delivered == 0);
    CHECK_FALSE(retained.expired());
    REQUIRE(wait_http([&] { return delivered == 1; }, [&] { client.update(); }));
    CHECK(retained.expired());
    REQUIRE(wait_http([&] { return client.ready_to_shut(); }));
    CHECK_FALSE(manager.stop(3));
    CHECK(client.pre_shut());
}

TEST_CASE("HTTP rejected callback exceptions retain later results and cannot gain admission") {
    auto owned_client = ark::detail::make_http_client_module();
    auto& client = *owned_client;
    CHECK(client.stop_accepting());
    auto lifetime = std::make_shared<int>(1);
    std::weak_ptr<int> retained = lifetime;
    unsigned delivered = 0;
    client.post(
        [&](http_result result) {
            CHECK_FALSE(result.first);
            CHECK_FALSE(client.ready_to_shut());
            auto nested = client.get("127.0.0.1", 1, "/must-reject");
            REQUIRE(nested.wait_for(0ms) == std::future_status::ready);
            CHECK_FALSE(await_http(nested).first);
            throw std::runtime_error("callback failure");
        },
        "127.0.0.1", 1, "/rejected-throw");
    client.post(
        [&, lifetime](http_result result) {
            CHECK_FALSE(result.first);
            CHECK_FALSE(client.ready_to_shut());
            CHECK(*lifetime == 1);
            ++delivered;
        },
        "127.0.0.1", 1, "/retained-result");
    lifetime.reset();
    CHECK_THROWS_AS(client.update(), std::runtime_error);
    CHECK_FALSE(client.ready_to_shut());
    CHECK_FALSE(retained.expired());
    CHECK(delivered == 0);
    CHECK(client.update());
    CHECK(delivered == 1);
    CHECK(retained.expired());
    CHECK(client.ready_to_shut());
    auto rejected = client.get("127.0.0.1", 1, "/after-exception");
    CHECK_FALSE(await_http(rejected).first);
    CHECK(client.update());
    CHECK(delivered == 1);
}

TEST_CASE("HTTP accepted callback exceptions release active ownership and restore admission scope") {
    http_peer peer;
    REQUIRE(peer.start());
    auto owned_client = ark::detail::make_http_client_module();
    auto& client = *owned_client;
    auto lifetime = std::make_shared<int>(1);
    std::weak_ptr<int> retained = lifetime;
    unsigned delivered = 0;
    client.post(
        [&, lifetime](http_result result) {
            CHECK(result.first);
            CHECK_FALSE(client.ready_to_shut());
            CHECK(*lifetime == 1);
            ++delivered;
            throw std::runtime_error("accepted callback failure");
        },
        "127.0.0.1", peer.port(), "/throw");
    lifetime.reset();
    REQUIRE(wait_http([&] { return peer.count() == 1; }));
    CHECK(client.stop_accepting());
    bool caught = false;
    REQUIRE(wait_http([&] { return caught; },
        [&] {
            try
            { client.update(); }
            catch (const std::runtime_error&)
            { caught = true; }
        }));
    CHECK(delivered == 1);
    CHECK(retained.expired());
    REQUIRE(wait_http([&] { return client.ready_to_shut(); }));
    auto rejected = client.get("127.0.0.1", peer.port(), "/after-accepted-exception");
    CHECK_FALSE(await_http(rejected).first);
    CHECK(peer.count() == 1);
}

TEST_CASE("HTTP validates host headers target and timeout before network IO") {
    http_peer peer;
    REQUIRE(peer.start());
    auto owned_client = ark::detail::make_http_client_module();
    auto& client = *owned_client;
    const std::string invalid_header = "application/json\r\nInjected: yes";
    auto host = client.get("invalid/host", peer.port(), "/");
    auto header = client.post("127.0.0.1", peer.port(), "/", nullptr, nullptr, &invalid_header);
    auto target = client.get("127.0.0.1", peer.port(), "/invalid path");
    auto timeout = client.get("127.0.0.1", peer.port(), "/", nullptr, nullptr, 0, 1);
    const std::string trace_body = "trace-body";
    auto trace =
        client.async_request(arknet::http::verb::trace, "127.0.0.1", peer.port(), "/", nullptr, nullptr, &trace_body);
    for (auto* future : {&host, &header, &target, &timeout, &trace})
    {
        const auto result = await_http(*future);
        CHECK_FALSE(result.first);
        CHECK(result.second == arknet::error_code(asio::error::invalid_argument).message());
    }
    CHECK(peer.count() == 0);
}

TEST_CASE("ARK HTTP server uses native routing and stops without signal delivery") {
    const auto port = http_port();
    http_plugin_configuration config("[http]\nhost='127.0.0.1'\nport=" + std::to_string(port) + "\nthreads=2\n");
    auto owned_server = ark::detail::make_http_server_module();
    auto& server = *owned_server;
    REQUIRE(server.init());
    server.get_router()
        .add(arknet::http::verb::get, "/users/:id",
            [](const auto& context, auto& response) {
                response.set(arknet::http::field::content_type, "text/plain");
                response.body() = "user:" + std::string(context.param("id")) + ":" + std::string(context.query);
            })
        .add(arknet::http::verb::post, "/echo",
            [](const auto& context, auto& response) { response.body() = context.request.body(); });
    REQUIRE(server.start_server());
    CHECK_FALSE(server.start_server("127.0.0.1", port, 2));
    auto owned_client = ark::detail::make_http_client_module();
    auto& client = *owned_client;
    auto get = client.get("127.0.0.1", port, "/users/42?active=1");
    const auto result = await_http(get);
    CHECK(result.first);
    CHECK(result.second == "user:42:active=1");
    const std::string body("route\0body", 10);
    auto post = client.post("127.0.0.1", port, "/echo", nullptr, &body);
    CHECK(await_http(post).second == body);
    auto missing = client.get("127.0.0.1", port, "/missing");
    const auto missing_result = await_http(missing);
    CHECK_FALSE(missing_result.first);
    CHECK(missing_result.second == "Not Found");
    auto method = client.async_request(arknet::http::verb::delete_, "127.0.0.1", port, "/users/42");
    const auto method_result = await_http(method);
    CHECK_FALSE(method_result.first);
    CHECK(method_result.second == "Method Not Allowed");

    http_request request(arknet::http::verb::get, "/", 11);
    request.keep_alive(true);
    const auto response = server.make_200<arknet::http::string_body>(request, "ready", "text/plain");
    CHECK(response.result() == arknet::http::status::ok);
    CHECK(response.body() == "ready");
    CHECK(response[arknet::http::field::server] == "ARK");
    CHECK(response[arknet::http::field::content_length] == "5");
    CHECK(response.keep_alive());

    auto owned_duplicate = ark::detail::make_http_server_module();
    auto& duplicate = *owned_duplicate;
    CHECK_FALSE(duplicate.start_server("127.0.0.1", port, 1));
    CHECK(duplicate.pre_shut());
    CHECK(server.pre_shut());
    REQUIRE(server.start_server("127.0.0.1", port, 1));
    auto restarted = client.get("127.0.0.1", port, "/users/7");
    CHECK(await_http(restarted).second == "user:7:");
    CHECK(server.pre_shut());
}

TEST_CASE("ARK HTTP module rejects a mistyped TOML setting during initialization") {
    http_plugin_configuration config("[http]\nhost='127.0.0.1'\nport=9090\nthread=4\n");
    auto owned_server = ark::detail::make_http_server_module();
    auto& server = *owned_server;
    CHECK_THROWS_AS(server.init(), std::runtime_error);
}
