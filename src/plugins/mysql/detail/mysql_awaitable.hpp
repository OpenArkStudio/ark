// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

#include <chrono>
#include <cstdint>
#include <exception>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "detail/database_awaitable.hpp"
#include "mysql/detail/mysql_service.hpp"

namespace ark::detail {

// Private operations, not a published database module API. Preserve execution
// and statement-close diagnostics independently, including uncertain outcomes.
// These factories move inputs into the lazy wait immediately. Service and
// bridge must outlive accepted operations and retain their owner-pump contract.
template<class service_type>
module_task<mysql_service_reply> mysql_query(service_type& service, database_awaitable<mysql_service_reply>& bridge,
    std::string sql, std::chrono::milliseconds timeout) {
    return bridge.wait(
        [&service, sql = std::move(sql), timeout](auto completion) mutable {
            return service.start_query(
                std::move(sql),
                [completion = std::move(completion)](mysql_service_result result) mutable {
                    if (auto* error = std::get_if<std::exception_ptr>(&result)) {
                        completion(*error, {});
                    } else {
                        completion({}, std::get<mysql_service_reply>(std::move(result)));
                    }
                },
                timeout);
        },
        [&service](std::uint64_t id) { service.cancel(id); });
}

template<class service_type>
module_task<mysql_service_reply> mysql_execute(service_type& service, database_awaitable<mysql_service_reply>& bridge,
    std::string sql, std::vector<arknet::mysql::field_value> parameters, std::chrono::milliseconds timeout) {
    return bridge.wait(
        [&service, sql = std::move(sql), parameters = std::move(parameters), timeout](auto completion) mutable {
            return service.start_execute(
                std::move(sql), std::move(parameters),
                [completion = std::move(completion)](mysql_service_result result) mutable {
                    if (auto* error = std::get_if<std::exception_ptr>(&result)) {
                        completion(*error, {});
                    } else {
                        completion({}, std::get<mysql_service_reply>(std::move(result)));
                    }
                },
                timeout);
        },
        [&service](std::uint64_t id) { service.cancel(id); });
}

} // namespace ark::detail
