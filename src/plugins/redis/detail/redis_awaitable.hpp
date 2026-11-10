// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

#include <chrono>
#include <cstdint>
#include <exception>
#include <utility>
#include <variant>
#include <vector>

#include "detail/database_awaitable.hpp"
#include "redis/detail/redis_service.hpp"

namespace ark::detail {

// Private operations for plain/TLS services with the same callback contract.
// These factories move inputs into the lazy wait immediately, before polling.
// Service and bridge must outlive every accepted operation; the owner still
// pumps service.update(), bridge.dispatch(), then tasks.poll().
template<class service_type>
module_task<arknet::redis::result> redis_command(service_type& service,
    database_awaitable<arknet::redis::result>& bridge, arknet::redis::command command,
    std::chrono::milliseconds timeout) {
    return bridge.wait(
        [&service, command = std::move(command), timeout](auto completion) mutable {
            return service.start(
                std::move(command),
                [completion = std::move(completion)](redis_service_result result) mutable {
                    if (auto* error = std::get_if<std::exception_ptr>(&result)) {
                        completion(*error, {});
                    } else {
                        completion({}, std::get<arknet::redis::result>(std::move(result)));
                    }
                },
                timeout);
        },
        [&service](std::uint64_t id) { service.cancel(id); });
}

template<class service_type>
module_task<arknet::redis::result> redis_pipeline(service_type& service,
    database_awaitable<arknet::redis::result>& bridge, std::vector<arknet::redis::command> commands,
    std::chrono::milliseconds timeout) {
    return bridge.wait(
        [&service, commands = std::move(commands), timeout](auto completion) mutable {
            return service.start_pipeline(
                std::move(commands),
                [completion = std::move(completion)](redis_service_result result) mutable {
                    if (auto* error = std::get_if<std::exception_ptr>(&result)) {
                        completion(*error, {});
                    } else {
                        completion({}, std::get<arknet::redis::result>(std::move(result)));
                    }
                },
                timeout);
        },
        [&service](std::uint64_t id) { service.cancel(id); });
}

} // namespace ark::detail
