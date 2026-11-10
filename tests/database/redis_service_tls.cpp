// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <future>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>

#include "../network/doctest.h"
#include <arknet/redis/pool.hpp>

#include "detail/database_awaitable.hpp"
#include "redis/detail/redis_service.hpp"

namespace {

using namespace std::chrono_literals;
using service_type = ark::detail::tls_redis_service;
using bridge_type = ark::detail::database_awaitable<arknet::redis::result>;
using result_type = arknet::redis::result;

struct peer_observation {
    arknet::error_code handshake_error;
    arknet::error_code read_error;
    arknet::error_code write_error;
    std::exception_ptr failure;
    bool timed_out{};
};

// A bounded RESP-over-TLS protocol test endpoint, not a Redis server implementation.
class tls_endpoint {
public:
    explicit tls_endpoint(const std::string& certificate = "server.pem") {
        const std::string directory = ARK_DATABASE_REDIS_CERT_DIR;
        context_.use_certificate_chain_file(directory + "/" + certificate);
        context_.use_private_key_file(directory + "/server-key.pem", asio::ssl::context::pem);
        worker_ = std::thread([this] {
            asio::co_spawn(io_, serve(), [this](std::exception_ptr error) {
                observation_.failure = std::move(error);
                finished_.set_value(observation_);
            });
            io_.run();
        });
    }

    ~tls_endpoint() {
        worker_.join();
    }

    std::string port() const {
        return port_;
    }

    unsigned commands() const {
        return commands_.load();
    }

    unsigned business_commands() const {
        return business_commands_.load();
    }

    peer_observation finish() {
        if (completion_.wait_for(6s) != std::future_status::ready) {
            throw std::runtime_error("TLS protocol endpoint did not finish");
        }
        return completion_.get();
    }

private:
    asio::awaitable<void> serve() {
        asio::ssl::stream<asio::ip::tcp::socket> stream(io_, context_);
        asio::steady_timer deadline(io_, 5s);
        deadline.async_wait([&](arknet::error_code error) {
            if (!error) {
                observation_.timed_out = true;
                arknet::error_code ignored;
                acceptor_.cancel(ignored);
                stream.next_layer().close(ignored);
            }
        });
        try {
            co_await acceptor_.async_accept(stream.next_layer(), asio::use_awaitable);
            co_await stream.async_handshake(asio::ssl::stream_base::server,
                asio::redirect_error(asio::use_awaitable, observation_.handshake_error));
            if (!observation_.handshake_error) {
                co_await exchange(stream);
            }
        } catch (...) {
            deadline.cancel();
            throw;
        }
        deadline.cancel();
    }

    asio::awaitable<void> exchange(asio::ssl::stream<asio::ip::tcp::socket>& stream) {
        arknet::redis::detail::resp_decoder decoder;
        bool blocked = false;
        for (;;) {
            arknet::redis::value value;
            const auto status = decoder.next(value);
            if (status == arknet::redis::detail::resp_status::ready) {
                if (value.type != arknet::redis::value_type::array || value.elements.empty()) {
                    throw std::runtime_error("Unexpected protocol command");
                }
                arknet::redis::command arguments;
                for (auto& argument : value.elements) {
                    if (argument.type != arknet::redis::value_type::bulk_string) {
                        throw std::runtime_error("Expected a binary RESP command argument");
                    }
                    arguments.push_back(std::move(argument.text));
                }
                ++commands_;
                std::string response;
                if (arguments == arknet::redis::command{"HELLO", "3"}) {
                    response = "%1\r\n+proto\r\n:3\r\n";
                } else if (arguments == arknet::redis::command{"RESET"}) {
                    if (blocked) {
                        // A released lease may queue RESET behind its cancelled reply slot.
                        // Keep both unanswered until the client closes the transport.
                        continue;
                    }
                    response = "+RESET\r\n";
                } else {
                    if (blocked) {
                        throw std::runtime_error("Unexpected command after blocked TLS test request");
                    }
                    ++business_commands_;
                    if (arguments.size() == 2 && arguments[0] == "ECHO") {
                        response = "$" + std::to_string(arguments[1].size()) + "\r\n" + arguments[1] + "\r\n";
                    } else if (arguments == arknet::redis::command{"PING"}) {
                        response = "+PONG\r\n";
                    } else if (arguments == arknet::redis::command{"BLPOP", "ark-tls-test", "0"}) {
                        // Deliberately send no reply: exercise a genuinely sent cancellation.
                        blocked = true;
                        continue;
                    } else {
                        throw std::runtime_error("Unsupported command in TLS protocol test endpoint");
                    }
                }
                co_await asio::async_write(stream, asio::buffer(response),
                    asio::redirect_error(asio::use_awaitable, observation_.write_error));
                if (observation_.write_error) {
                    co_return;
                }
                continue;
            }
            if (status != arknet::redis::detail::resp_status::need_more) {
                throw std::runtime_error("Invalid RESP input in TLS protocol test endpoint");
            }
            std::array<char, 4096> bytes;
            const auto count = co_await stream.async_read_some(
                asio::buffer(bytes), asio::redirect_error(asio::use_awaitable, observation_.read_error));
            if (observation_.read_error) {
                co_return;
            }
            if (!decoder.append(std::string_view(bytes.data(), count))) {
                throw std::runtime_error("RESP protocol test endpoint limit exceeded");
            }
        }
    }

    asio::io_context io_;
    asio::ssl::context context_{asio::ssl::context::tls_server};
    asio::ip::tcp::acceptor acceptor_{io_, {asio::ip::address_v4::loopback(), 0}};
    const std::string port_{std::to_string(acceptor_.local_endpoint().port())};
    std::atomic<unsigned> commands_{};
    std::atomic<unsigned> business_commands_{};
    peer_observation observation_;
    std::promise<peer_observation> finished_;
    std::future<peer_observation> completion_ = finished_.get_future();
    std::thread worker_;
};

template<class predicate_type>
bool wait_until(predicate_type predicate, std::chrono::milliseconds maximum = 3s) {
    const auto deadline = std::chrono::steady_clock::now() + maximum;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

arknet::redis::pool_options options_for(const tls_endpoint& endpoint) {
    arknet::redis::pool_options options;
    options.max_connections = 1;
    options.max_waiters = 4;
    options.acquire_timeout = 1500ms;
    options.reset_timeout = 1500ms;
    options.connection.host = "127.0.0.1";
    options.connection.port = endpoint.port();
    options.client_configuration.connect_timeout = 1500ms;
    options.client_configuration.command_timeout = 1500ms;
    options.client_configuration.drain_timeout = 1500ms;
    options.certificate_authority = std::string(ARK_DATABASE_REDIS_CERT_DIR) + "/ca.pem";
    return options;
}

result_type take_result(ark::detail::redis_service_result result) {
    if (auto* error = std::get_if<std::exception_ptr>(&result)) {
        std::rethrow_exception(*error);
    }
    return std::get<result_type>(std::move(result));
}

void check_peer(const peer_observation& observation) {
    if (observation.failure) {
        std::rethrow_exception(observation.failure);
    }
    CHECK_FALSE(observation.timed_out);
}

void check_rejected(const std::string& certificate, const std::string& authority) {
    tls_endpoint endpoint(certificate);
    auto options = options_for(endpoint);
    options.certificate_authority = std::string(ARK_DATABASE_REDIS_CERT_DIR) + "/" + authority;
    service_type service(options);
    const auto owner = std::this_thread::get_id();
    std::optional<result_type> response;
    std::thread::id delivered_on;
    const auto id = service.start(
        {"PING"},
        [&](auto result) {
            delivered_on = std::this_thread::get_id();
            response = take_result(std::move(result));
        },
        2s);
    REQUIRE(id != 0);
    REQUIRE(wait_until([&] { return !service.is_pending(id); }));
    CHECK_FALSE(response.has_value());
    service.update();
    REQUIRE(response.has_value());
    CHECK(delivered_on == owner);
    CHECK_FALSE(static_cast<bool>(*response));
    CHECK(static_cast<bool>(response->transport_error));
    CHECK_FALSE(response->command_may_have_run);
    CHECK(response->replies.empty());
    service.stop();
    const auto observation = endpoint.finish();
    check_peer(observation);
    CHECK((static_cast<bool>(observation.handshake_error) || static_cast<bool>(observation.read_error)));
    // Observe the endpoint after it finished, not just the client's exception.
    CHECK(endpoint.commands() == 0);
    CHECK(endpoint.business_commands() == 0);
}

ark::detail::module_task<> issue_command(service_type& service, bridge_type& bridge, arknet::redis::command arguments,
    std::optional<result_type>& response, std::thread::id& resumed_on) {
    response = co_await bridge.wait(
        [&service, arguments = std::move(arguments)](auto complete) mutable {
            return service.start(
                std::move(arguments),
                [complete = std::move(complete)](ark::detail::redis_service_result value) mutable {
                    if (auto* error = std::get_if<std::exception_ptr>(&value)) {
                        complete(*error, {});
                    } else {
                        complete({}, std::get<result_type>(std::move(value)));
                    }
                },
                2s);
        },
        [&service](std::uint64_t id) { service.cancel(id); });
    resumed_on = std::this_thread::get_id();
}

} // namespace

TEST_CASE("Redis TLS pipeline owns binary results and dispatches only from owner update") {
    tls_endpoint endpoint;
    const auto owner = std::this_thread::get_id();
    const std::string binary("tls\0\r\n\xff", 7);
    std::optional<result_type> retained;
    {
        service_type service(options_for(endpoint));
        std::thread::id delivered_on;
        const auto id = service.start_pipeline(
            {{"ECHO", binary}, {"PING"}},
            [&](auto result) {
                delivered_on = std::this_thread::get_id();
                retained = take_result(std::move(result));
            },
            2s);
        REQUIRE(id != 0);
        REQUIRE(wait_until([&] { return !service.is_pending(id); }));
        CHECK_FALSE(retained.has_value());
        CHECK(endpoint.business_commands() == 2);
        CHECK(service.update() == 1);
        REQUIRE(retained.has_value());
        CHECK(delivered_on == owner);
        REQUIRE(static_cast<bool>(*retained));
        REQUIRE(retained->replies.size() == 2);
        CHECK(retained->replies[0].text == binary);
        CHECK(retained->replies[1].text == "PONG");
        service.stop();
    }
    check_peer(endpoint.finish());
    REQUIRE(retained.has_value());
    CHECK(retained->replies[0].text == binary);
}

TEST_CASE("Redis TLS rejects an unrelated trust certificate before sending HELLO or business commands") {
    check_rejected("server.pem", "ipv6/ca.pem");
}

TEST_CASE("Redis TLS rejects a trusted certificate with the wrong hostname before sending any command") {
    check_rejected("wrong.pem", "ca.pem");
}

TEST_CASE("Redis TLS sent cancellation resumes an owning coroutine result on the module executor") {
    tls_endpoint endpoint;
    service_type service(options_for(endpoint));
    auto runtime = std::make_shared<arknet::manual_runtime>();
    bridge_type bridge(runtime);
    ark::detail::module_task_group group(runtime);
    std::optional<result_type> response;
    std::thread::id resumed_on;
    std::exception_ptr task_error;
    const auto owner = std::this_thread::get_id();
    const auto task = group.start(issue_command(service, bridge, {"BLPOP", "ark-tls-test", "0"}, response, resumed_on),
        [&](std::exception_ptr error) { task_error = std::move(error); });
    group.poll();
    const auto sent = wait_until([&] { return endpoint.business_commands() == 1; });
    const auto cancellation_requested = group.cancel(task);
    CHECK_FALSE(response.has_value());
    // Running the coroutine executor alone cannot bypass the service/bridge delivery stages.
    group.poll();
    CHECK_FALSE(response.has_value());
    const auto drained = wait_until([&] {
        service.update();
        bridge.dispatch();
        group.poll();
        return group.pending() == 0 && bridge.pending() == 0;
    });
    if (!drained) {
        // A failed test cannot legally destroy a still-suspended coroutine owner.
        std::fputs("Redis TLS coroutine did not drain before the test deadline\n", stderr);
        std::_Exit(1);
    }
    group.close();
    service.stop();
    check_peer(endpoint.finish());
    REQUIRE(sent);
    CHECK(task != 0);
    CHECK(cancellation_requested);
    CHECK_FALSE(static_cast<bool>(task_error));
    CHECK(group.closed());
    REQUIRE(response.has_value());
    CHECK(response->error == arknet::redis::client_errc::cancelled);
    CHECK(response->command_may_have_run);
    CHECK(resumed_on == owner);
}
