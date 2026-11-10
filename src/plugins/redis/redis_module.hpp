// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

#include <arknet/redis/pool.hpp>

#include "ark/module.hpp"
#include "ark/task.hpp"

namespace ark {

// All strings, authentication and TLS settings are owned by the configuration.
struct redis_options {
    arknet::redis::pool_options pool;
    std::size_t max_operations{64};
    std::size_t max_input_bytes{1024 * 1024};
    bool tls{};
};

class redis_module : public module {
public:
    // Owner-thread setup. Names are unique; duplicate names or sealed admission
    // return false. Invalid options (including unavailable TLS) throw.
    virtual bool configure(std::string connection, redis_options options) = 0;

    // Factories own inputs immediately. Await on the manager's task executor.
    // Native errors and command_may_have_run are preserved without write replay.
    virtual task<arknet::redis::result> command(std::string connection, arknet::redis::command command,
        std::chrono::milliseconds timeout = std::chrono::seconds(5)) = 0;
    virtual task<arknet::redis::result> pipeline(std::string connection, std::vector<arknet::redis::command> commands,
        std::chrono::milliseconds timeout = std::chrono::seconds(5)) = 0;
};

} // namespace ark
