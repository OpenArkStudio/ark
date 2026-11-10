// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <thread>

#include <arknet/mysql/pool.hpp>

// Test-only transparent TCP relay: it never parses or decrypts MySQL/TLS.
class mysql_transaction_relay {
public:
    explicit mysql_transaction_relay(std::uint16_t upstream)
        : upstream_(upstream)
        , port_(acceptor_.local_endpoint().port())
        , worker_([this] {
            deadline_.expires_after(std::chrono::seconds(8));
            deadline_.async_wait([this](arknet::error_code error) {
                if (!error) {
                    timed_out_ = true;
                    close();
                }
            });
            asio::co_spawn(io_, connect(), [this](std::exception_ptr error) {
                if (error) {
                    failed_ = true;
                    close();
                }
            });
            io_.run();
        }) {
    }

    ~mysql_transaction_relay() {
        asio::post(io_, [this] { close(); });
        worker_.join();
    }

    std::uint16_t port() const {
        return port_;
    }

    void drop_responses() {
        dropping_ = true;
    }

    std::size_t discarded_bytes() const {
        return discarded_bytes_;
    }

    bool failed() const {
        return failed_ || timed_out_;
    }

private:
    void close() {
        arknet::error_code ignored;
        acceptor_.close(ignored);
        downstream_.close(ignored);
        upstream_socket_.close(ignored);
        deadline_.cancel();
    }

    asio::awaitable<void> connect() {
        co_await acceptor_.async_accept(downstream_, asio::use_awaitable);
        co_await upstream_socket_.async_connect({asio::ip::address_v4::loopback(), upstream_}, asio::use_awaitable);
        for (const bool responses : {false, true}) {
            asio::co_spawn(io_, forward(responses), [this](std::exception_ptr error) {
                if (error) {
                    failed_ = true;
                }
                close();
            });
        }
    }

    asio::awaitable<void> forward(bool responses) {
        auto& source = responses ? upstream_socket_ : downstream_;
        auto& destination = responses ? downstream_ : upstream_socket_;
        std::array<char, 16384> bytes;
        for (;;) {
            arknet::error_code error;
            const auto count =
                co_await source.async_read_some(asio::buffer(bytes), asio::redirect_error(asio::use_awaitable, error));
            if (error) {
                co_return;
            }
            if (responses && dropping_) {
                discarded_bytes_ += count;
                continue;
            }
            co_await asio::async_write(
                destination, asio::buffer(bytes.data(), count), asio::redirect_error(asio::use_awaitable, error));
            if (error) {
                co_return;
            }
        }
    }

    asio::io_context io_;
    asio::ip::tcp::acceptor acceptor_{io_, {asio::ip::address_v4::loopback(), 0}};
    asio::ip::tcp::socket downstream_{io_};
    asio::ip::tcp::socket upstream_socket_{io_};
    asio::steady_timer deadline_{io_};
    std::atomic<bool> dropping_{};
    std::atomic<bool> failed_{};
    std::atomic<bool> timed_out_{};
    std::atomic<std::size_t> discarded_bytes_{};
    const std::uint16_t upstream_;
    const std::uint16_t port_;
    std::thread worker_;
};
