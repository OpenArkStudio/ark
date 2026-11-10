// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <stdexcept>

#include "redis_sample_module.hpp"
#include "redis_sample_plugin.hpp"

namespace ark {

std::unique_ptr<redis_sample_module> make_redis_sample_module() {
    return std::make_unique<redis_sample_module>();
}

bool redis_sample_module::init() {
    redis_module_ = find_module<redis_module>();
    if (!redis_module_) {
        return false;
    }
    tasks_ = &get_plugin_manager()->create_task_group(*this);
    redis_options options;
    options.pool.connection.host = "127.0.0.1";
    options.pool.connection.port = "6379";
    return redis_module_->configure("sample", std::move(options));
}

bool redis_sample_module::post_init() {
    return tasks_->start(run(), [](std::exception_ptr error) {
        if (error) {
            try {
                std::rethrow_exception(error);
            } catch (const std::exception& failure) {
                ARK_LOG_ERROR("Redis sample failed: {}", failure.what());
            }
        }
    }) != 0;
}

task<> redis_sample_module::run() {
    auto written = co_await redis_module_->command("sample", {"SET", "test", "123"});
    if (!written) {
        throw std::runtime_error("Redis SET failed; inspect native error before retrying a write");
    }
    auto read = co_await redis_module_->command("sample", {"GET", "test"});
    if (!read || read.replies.empty()) {
        throw std::runtime_error("Redis GET failed");
    }
    ARK_LOG_INFO("Redis GET succeeded, value={}", read.replies.front().text);
}

} // namespace ark
