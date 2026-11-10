// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>

#include "redis/detail/redis_service.hpp"

namespace {

struct release_service {
    std::unique_ptr<ark::detail::redis_service>* owner{};
    bool* destroyed{};

    ~release_service() {
        owner->reset();
        *destroyed = true;
    }
};

} // namespace

int main() {
    try {
        const auto missing_lease = ark::detail::redis_acquire_failure({});
        if (missing_lease.error != arknet::redis::client_errc::not_connected || missing_lease.command_may_have_run) {
            throw std::runtime_error("A failed acquire with no lease was reported as command success");
        }
        arknet::redis::pool::acquire_result rejected;
        rejected.error = arknet::redis::client_errc::handshake_failed;
        rejected.transport_error = asio::error::connection_aborted;
        arknet::redis::value reply;
        reply.type = arknet::redis::value_type::error;
        reply.text = "ERR authentication fixture";
        rejected.replies.push_back(std::move(reply));
        const auto failure = ark::detail::redis_acquire_failure(std::move(rejected));
        if (failure.error != arknet::redis::client_errc::handshake_failed ||
            failure.transport_error != asio::error::connection_aborted || failure.replies.size() != 1 ||
            failure.replies[0].type != arknet::redis::value_type::error ||
            failure.replies[0].text != "ERR authentication fixture" || failure.command_may_have_run) {
            throw std::runtime_error("Acquire failure lost its original error or owning replies");
        }
        arknet::redis::pool_options options;
        options.connection.host = "127.0.0.1";
        options.connection.port = "1";
        options.max_connections = 1;
        auto service = std::make_unique<ark::detail::redis_service>(options);
        bool destroyed{};
        unsigned callbacks{};
        auto capture = std::make_shared<release_service>();
        capture->owner = &service;
        capture->destroyed = &destroyed;
        if (!service->submit({"PING"}, [capture, &callbacks](auto) { ++callbacks; }, std::chrono::seconds(1))) {
            throw std::runtime_error("Stop-capture operation was not admitted");
        }
        capture.reset();
        // stop() removes the queued business capture. Its destructor releases
        // the service while that same stop() call is still active.
        service->stop();
        if (service || !destroyed || callbacks) {
            throw std::runtime_error("Stop did not release its capture without invoking business code");
        }
        std::cout << "private Redis stop capture can destroy its service safely\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
