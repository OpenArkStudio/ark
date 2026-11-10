// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <cstdlib>
#include <stdexcept>

#include "mysql_sample_module.hpp"
#include "mysql_sample_plugin.hpp"

namespace ark {
namespace {

task<> run_mysql_sample(mysql_module& database) {
    auto reply = co_await database.query("primary", "SELECT 1");
    if (!reply) {
        throw std::runtime_error(reply.result.status.message);
    }
    ARK_LOG_INFO("MySQL query completed: {} rows", reply.result.rows.size());
    std::vector<arknet::mysql::field_value> parameters{std::int64_t{42}};
    auto prepared = co_await database.execute("primary", "SELECT ?", std::move(parameters));
    if (!prepared) {
        throw std::runtime_error(prepared.result.status.message);
    }
}

} // namespace

std::unique_ptr<mysql_sample_module> make_mysql_sample_module() {
    return std::make_unique<mysql_sample_module>();
}

bool mysql_sample_module::init() {
    mysql_module_ptr_ = find_module<mysql_module>();
    if (!mysql_module_ptr_) {
        return false;
    }
    mysql_options options;
    if (const auto* value = std::getenv("ARK_MYSQL_HOST")) {
        options.pool.connection.host = value;
    }
    if (const auto* value = std::getenv("ARK_MYSQL_USER")) {
        options.pool.connection.username = value;
    }
    if (const auto* value = std::getenv("ARK_MYSQL_PASSWORD")) {
        options.pool.connection.password = value;
    }
    if (const auto* value = std::getenv("ARK_MYSQL_DATABASE")) {
        options.pool.connection.database = value;
    }
    if (const auto* value = std::getenv("ARK_MYSQL_CA")) {
        options.pool.connection.ca_file = value;
    }
    // Native TLS verification remains enabled. A password is never logged.
    return mysql_module_ptr_->configure("primary", std::move(options));
}

bool mysql_sample_module::post_init() {
    auto& tasks = get_plugin_manager()->create_task_group(*this);
    return tasks.start(run_mysql_sample(*mysql_module_ptr_), [](std::exception_ptr error) {
        if (error) {
            ARK_LOG_ERROR("MySQL sample coroutine failed");
        }
    }) != 0;
}

} // namespace ark
