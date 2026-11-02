// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <array>
#include <atomic>
#include <chrono>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "../network/doctest.h"

#include "redis/redis_plugin.hpp"

namespace {

using namespace std::chrono_literals;
using result_type = arknet::redis::result;
using client_errc = arknet::redis::client_errc;

// Bounded loopback RESP protocol peer, not a Redis server implementation.
class redis_peer
{
public:
    explicit redis_peer(bool tls = false)
        : tls_(tls) {
#if defined(ARKNET_ENABLE_SSL)
        if (tls_)
        {
            const std::string directory = ARK_DATABASE_REDIS_CERT_DIR;
            context_.use_certificate_chain_file(directory + "/server.pem");
            context_.use_private_key_file(directory + "/server-key.pem", asio::ssl::context::pem);
        }
#endif
        worker_ = std::thread([this] {
            asio::co_spawn(io_, serve(), [this](std::exception_ptr error) { failure_ = std::move(error); });
            io_.run();
        });
    }

    ~redis_peer() {
        asio::post(io_, [this] {
            arknet::error_code ignored;
            acceptor_.close(ignored);
            socket_.close(ignored);
            deadline_.cancel();
        });
        worker_.join();
    }

    std::string port() const {
        return std::to_string(acceptor_.local_endpoint().port());
    }

    unsigned commands() const {
        return business_commands_.load();
    }

    bool blocked() const {
        return blocked_.load();
    }

private:
    asio::awaitable<void> serve() {
        deadline_.expires_after(5s);
        deadline_.async_wait([this](arknet::error_code error) {
            if (!error)
            {
                arknet::error_code ignored;
                acceptor_.close(ignored);
                socket_.close(ignored);
            }
        });
        arknet::error_code error;
        co_await acceptor_.async_accept(socket_, asio::redirect_error(asio::use_awaitable, error));
        if (error)
        {
            co_return;
        }
#if defined(ARKNET_ENABLE_SSL)
        if (tls_)
        {
            asio::ssl::stream<asio::ip::tcp::socket&> stream(socket_, context_);
            co_await stream.async_handshake(
                asio::ssl::stream_base::server, asio::redirect_error(asio::use_awaitable, error));
            if (!error)
            {
                co_await exchange(stream);
            }
            co_return;
        }
#endif
        co_await exchange(socket_);
    }

    template<class stream_type>
    asio::awaitable<void> exchange(stream_type& stream) {
        arknet::error_code error;
        arknet::redis::detail::resp_decoder decoder;
        for (;;)
        {
            arknet::redis::value value;
            const auto status = decoder.next(value);
            if (status == arknet::redis::detail::resp_status::ready)
            {
                if (value.type != arknet::redis::value_type::array || value.elements.empty())
                {
                    throw std::runtime_error("Expected RESP command array");
                }
                arknet::redis::command arguments;
                for (auto& argument : value.elements)
                {
                    arguments.push_back(std::move(argument.text));
                }
                std::string response;
                if (arguments == arknet::redis::command{"HELLO", "3"})
                {
                    response = "%1\r\n+proto\r\n:3\r\n";
                }
                else if (arguments == arknet::redis::command{"RESET"})
                {
                    response = "+RESET\r\n";
                }
                else
                {
                    ++business_commands_;
                    if (arguments.size() == 2 && arguments[0] == "ECHO")
                    {
                        response = "$" + std::to_string(arguments[1].size()) + "\r\n" + arguments[1] + "\r\n";
                    }
                    else if (arguments == arknet::redis::command{"PING"})
                    {
                        response = "+PONG\r\n";
                    }
                    else if (arguments == arknet::redis::command{"GET", "missing"})
                    {
                        response = "$-1\r\n";
                    }
                    else if (arguments == arknet::redis::command{"FAIL"})
                    {
                        response = "-ERR fixture\r\n";
                    }
                    else if (arguments == arknet::redis::command{"BLOCK"})
                    {
                        blocked_ = true;
                        continue;
                    }
                    else
                    {
                        throw std::runtime_error("Unexpected command in RESP fixture");
                    }
                }
                if (blocked_)
                {
                    continue;
                }
                co_await asio::async_write(
                    stream, asio::buffer(response), asio::redirect_error(asio::use_awaitable, error));
                if (error)
                {
                    co_return;
                }
                continue;
            }
            if (status != arknet::redis::detail::resp_status::need_more)
            {
                throw std::runtime_error("Invalid fixture RESP input");
            }
            std::array<char, 4096> bytes;
            const auto count =
                co_await stream.async_read_some(asio::buffer(bytes), asio::redirect_error(asio::use_awaitable, error));
            if (error)
            {
                co_return;
            }
            if (!decoder.append(std::string_view(bytes.data(), count)))
            {
                throw std::runtime_error("RESP fixture limit reached");
            }
        }
    }

    asio::io_context io_;
    asio::ip::tcp::acceptor acceptor_{io_, {asio::ip::address_v4::loopback(), 0}};
    asio::ip::tcp::socket socket_{io_};
    asio::steady_timer deadline_{io_};
#if defined(ARKNET_ENABLE_SSL)
    asio::ssl::context context_{asio::ssl::context::tls_server};
#endif
    bool tls_{};
    std::atomic<unsigned> business_commands_{};
    std::atomic<bool> blocked_{};
    std::exception_ptr failure_;
    std::thread worker_;
};

struct module_owner
{
    ark::plugin_manager manager;
    std::unique_ptr<ark::redis_module> redis_owner{ark::detail::make_redis_module()};
    ark::redis_module& redis{*redis_owner};
    ark::module consumer;
    ark::task_group* tasks{};
    std::optional<result_type> result;
    std::exception_ptr error;
    unsigned completed{};

    module_owner() {
        redis.set_plugin_manager(&manager);
        redis.set_name("redis");
        if (!manager.add_module("redis", &redis) || !manager.add_module("consumer", &consumer))
        {
            throw std::runtime_error("Cannot install Redis test modules");
        }
        // ARK_REGISTER_MODULE installs this update hook before module init.
        if (!manager.add_update_module(&redis))
        {
            throw std::runtime_error("Cannot register Redis update hook");
        }
        tasks = &manager.create_task_group(consumer);
    }

    ~module_owner() {
        const auto deadline = std::chrono::steady_clock::now() + 4s;
        while (!manager.stop(0) && std::chrono::steady_clock::now() < deadline)
        {
            manager.update(0);
            std::this_thread::sleep_for(1ms);
        }
    }

    ark::redis_options options(const redis_peer& peer) const {
        ark::redis_options options;
        options.pool.connection.port = peer.port();
        options.pool.max_connections = 1;
        options.pool.client_configuration.drain_timeout = 100ms;
        options.pool.client_configuration.connect_timeout = 1s;
        return options;
    }

    ark::task<> collect(ark::task<result_type> pending) {
        result.emplace(co_await std::move(pending));
        CHECK(manager.get_task_runtime()->get_executor().running_in_this_thread());
    }

    ark::task_group::task_id start(ark::task<result_type> operation) {
        return tasks->start(collect(std::move(operation)), [this](std::exception_ptr failure) {
            error = std::move(failure);
            ++completed;
        });
    }

    template<class predicate_type>
    bool pump_until(predicate_type predicate) {
        const auto deadline = std::chrono::steady_clock::now() + 3s;
        while (!predicate())
        {
            if (std::chrono::steady_clock::now() >= deadline)
            {
                return false;
            }
            manager.update(0);
            std::this_thread::sleep_for(1ms);
        }
        return true;
    }
};

} // namespace

TEST_CASE("Public Redis factories own inputs and native ordered results") {
    redis_peer peer;
    module_owner owner;
    REQUIRE(owner.redis.configure("main", owner.options(peer))); // Before Redis init.
    CHECK_FALSE(owner.redis.configure("main", owner.options(peer)));
    REQUIRE(owner.redis.init());
    REQUIRE(owner.redis.init()); // Init does not duplicate the macro-installed update hook.
    const std::string binary("owned\0value", 11);
    std::vector<arknet::redis::command> commands{{"ECHO", binary}, {"GET", "missing"}, {"FAIL"}};
    auto operation = owner.redis.pipeline("main", commands);
    commands.front()[1] = "changed";
    CHECK(peer.commands() == 0);
    REQUIRE(owner.start(std::move(operation)) != 0);
    REQUIRE(owner.pump_until([&] { return owner.completed == 1; }));
    CHECK_FALSE(owner.error);
    REQUIRE(owner.result);
    CHECK(owner.result->error == client_errc::none);
    REQUIRE(owner.result->replies.size() == 3);
    CHECK(owner.result->replies[0].text == binary);
    CHECK(owner.result->replies[1].type == arknet::redis::value_type::null);
    CHECK(owner.result->replies[2].type == arknet::redis::value_type::error);
    auto retained = std::move(*owner.result);
    REQUIRE(owner.manager.stop(0));
    CHECK(retained.replies[0].text == binary);
    CHECK(peer.commands() == 3);
}

TEST_CASE("Public Redis rejects foreign executors before native submission") {
    redis_peer peer;
    module_owner owner;
    REQUIRE(owner.redis.configure("main", owner.options(peer)));
    REQUIRE(owner.redis.init());
    auto foreign_runtime = std::make_shared<arknet::manual_runtime>();
    ark::task_group foreign(foreign_runtime);
    std::exception_ptr failure;
    REQUIRE(foreign.start(owner.collect(owner.redis.command("main", {"PING"})),
                [&](std::exception_ptr error) { failure = std::move(error); }) != 0);
    foreign.poll();
    CHECK(foreign.pending() == 0);
    REQUIRE(failure);
    CHECK_THROWS_AS(std::rethrow_exception(failure), std::logic_error);
    CHECK(peer.commands() == 0);
    CHECK(owner.redis.ready_to_shut());
}

TEST_CASE("Public Redis bounds retained input and bridge admission") {
    redis_peer peer;
    module_owner owner;
    auto options = owner.options(peer);
    options.max_operations = 1;
    options.max_input_bytes = 512;
    REQUIRE(owner.redis.configure("main", options));
    REQUIRE(owner.redis.init());
    REQUIRE(owner.start(owner.redis.command("main", {"ECHO", std::string(1024, 'x')})) != 0);
    REQUIRE(owner.pump_until([&] { return owner.completed == 1; }));
    REQUIRE(owner.result);
    CHECK(owner.result->error == client_errc::queue_full);
    CHECK(peer.commands() == 0);
    owner.result.reset();
    REQUIRE(owner.start(owner.redis.command("main", {"BLOCK"})) != 0);
    REQUIRE(owner.pump_until([&] { return peer.blocked(); }));
    REQUIRE(owner.start(owner.redis.command("main", {"PING"})) != 0);
    REQUIRE(owner.pump_until([&] { return owner.completed == 2; }));
    REQUIRE(owner.error);
    CHECK_THROWS_AS(std::rethrow_exception(owner.error), std::length_error);
    CHECK(peer.commands() == 1);
    owner.tasks->cancel_all();
    REQUIRE(owner.pump_until([&] { return owner.tasks->pending() == 0; }));
    REQUIRE(owner.result);
    CHECK(owner.result->error == client_errc::cancelled);
    CHECK(owner.result->command_may_have_run);
}

TEST_CASE("Public Redis cancellation preserves sent and waiting outcomes") {
    redis_peer peer;
    module_owner owner;
    REQUIRE(owner.redis.configure("main", owner.options(peer)));
    REQUIRE(owner.redis.init());
    const auto sent = owner.start(owner.redis.command("main", {"BLOCK"}));
    REQUIRE(sent != 0);
    REQUIRE(owner.pump_until([&] { return peer.blocked(); }));
    const auto waiting = owner.start(owner.redis.command("main", {"ECHO", "unsent"}));
    REQUIRE(waiting != 0);
    owner.manager.update(0);
    CHECK_FALSE(owner.redis.ready_to_shut());
    REQUIRE(owner.tasks->cancel(waiting));
    REQUIRE(owner.pump_until([&] { return owner.completed == 1; }));
    CHECK_FALSE(owner.error);
    REQUIRE(owner.result);
    CHECK(owner.result->error == client_errc::cancelled);
    CHECK_FALSE(owner.result->command_may_have_run);
    REQUIRE(owner.tasks->cancel(sent));
    REQUIRE(owner.pump_until([&] { return owner.completed == 2; }));
    CHECK_FALSE(owner.error);
    CHECK(owner.result->error == client_errc::cancelled);
    CHECK(owner.result->command_may_have_run);
    CHECK(peer.commands() == 1);
}

TEST_CASE("Manager stop cancels a sent Redis command and retires its native outcome before cleanup") {
    redis_peer peer;
    module_owner owner;
    REQUIRE(owner.redis.configure("main", owner.options(peer)));
    REQUIRE(owner.redis.init());
    REQUIRE(owner.start(owner.redis.command("main", {"BLOCK"})) != 0);
    REQUIRE(owner.pump_until([&] { return peer.blocked(); }));
    CHECK_FALSE(owner.manager.stop(0));
    CHECK(owner.manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::stopping);
    CHECK(owner.tasks->start([]() -> ark::task<> { co_return; }()) == 0);
    REQUIRE(owner.pump_until([&] { return owner.completed == 1 && owner.tasks->pending() == 0; }));
    CHECK_FALSE(owner.error);
    REQUIRE(owner.result);
    CHECK(owner.result->error == client_errc::cancelled);
    CHECK(owner.result->command_may_have_run);
    REQUIRE(owner.pump_until([&] { return owner.manager.stop(0); }));
    CHECK(peer.commands() == 1);
    CHECK_FALSE(owner.redis.configure("late", owner.options(peer)));
}

TEST_CASE("Unstarted Redis task pins state across facade destruction and rejects after retirement") {
    ark::plugin_manager manager;
    auto redis = ark::detail::make_redis_module();
    redis->set_plugin_manager(&manager);
    REQUIRE(redis->configure("main", {}));
    auto operation = redis->command("main", {"PING"});
    REQUIRE(redis->pre_shut());
    REQUIRE(redis->shut());
    redis.reset();
    std::exception_ptr failure;
    auto collect = [](ark::task<result_type> pending) -> ark::task<> { co_await std::move(pending); };
    ark::task_group tasks(manager.get_task_runtime());
    REQUIRE(
        tasks.start(collect(std::move(operation)), [&](std::exception_ptr error) { failure = std::move(error); }) != 0);
    tasks.poll();
    CHECK(tasks.pending() == 0);
    REQUIRE(failure);
    CHECK_THROWS_AS(std::rethrow_exception(failure), std::logic_error);
}

TEST_CASE("Public Redis preserves native response limits and missing connection errors") {
    redis_peer peer;
    module_owner owner;
    auto options = owner.options(peer);
    options.pool.client_configuration.max_pipeline_response_bytes = 4096;
    REQUIRE(owner.redis.configure("main", options));
    REQUIRE(owner.redis.init());
    REQUIRE(owner.start(owner.redis.command("absent", {"PING"})) != 0);
    REQUIRE(owner.pump_until([&] { return owner.completed == 1; }));
    REQUIRE(owner.result);
    CHECK(owner.result->error == client_errc::not_connected);
    CHECK(peer.commands() == 0);
    REQUIRE(owner.start(owner.redis.command("main", {"ECHO", std::string(8192, 'x')})) != 0);
    REQUIRE(owner.pump_until([&] { return owner.completed == 2; }));
    CHECK_FALSE(owner.error);
    REQUIRE(owner.result);
    CHECK(owner.result->error == client_errc::response_limit);
    CHECK(owner.result->command_may_have_run);
}

#if !defined(ARKNET_ENABLE_SSL)
TEST_CASE("Public Redis rejects TLS configuration in a build without SSL") {
    module_owner owner;
    ark::redis_options options;
    options.tls = true;
    CHECK_THROWS_AS(owner.redis.configure("tls", options), std::invalid_argument);
    CHECK(owner.redis.ready_to_shut());
}
#endif

#if defined(ARKNET_ENABLE_SSL)
TEST_CASE("Public Redis TLS uses owning pool configuration and native results") {
    redis_peer peer(true);
    module_owner owner;
    auto options = owner.options(peer);
    options.tls = true;
    // Match the IPv4-only peer. The certificate includes the 127.0.0.1 IP SAN;
    // this test verifies TLS ownership, not dual-stack hostname resolution.
    options.pool.connection.host = "127.0.0.1";
    options.pool.certificate_authority = std::string(ARK_DATABASE_REDIS_CERT_DIR) + "/ca.pem";
    REQUIRE(owner.redis.configure("secure", options));
    REQUIRE(owner.redis.init());
    // Mutating the caller's TLS configuration cannot change the owned pool.
    options.pool.certificate_authority = "missing-certificate.pem";
    REQUIRE(owner.start(owner.redis.command("secure", {"ECHO", "verified TLS"})) != 0);
    REQUIRE(owner.pump_until([&] { return owner.completed == 1; }));
    CHECK_FALSE(owner.error);
    REQUIRE(owner.result);
    CAPTURE(owner.result->error);
    CAPTURE(owner.result->transport_error.message());
    CHECK(owner.result->error == client_errc::none);
    REQUIRE(owner.result->replies.size() == 1);
    CHECK(owner.result->replies.front().text == "verified TLS");
    CHECK(peer.commands() == 1);
    REQUIRE(owner.manager.stop(0));
}
#endif
