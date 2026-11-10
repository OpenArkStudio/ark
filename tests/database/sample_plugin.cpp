// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <chrono>
#include <concepts>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>

#include "ark/plugin_manager.hpp"

#ifdef ARK_DATABASE_SAMPLE_MYSQL
#include "../../examples/mysql_sample/mysql_sample_plugin.hpp"
using sample_module = ark::mysql_sample_module;
using sample_plugin = ark::mysql_sample_plugin;
using database_module = ark::mysql_module;
#else
#include "../../examples/redis_sample/redis_sample_plugin.hpp"
using sample_module = ark::redis_sample_module;
using sample_plugin = ark::redis_sample_plugin;
using database_module = ark::redis_module;
#endif

static_assert(std::derived_from<sample_module, ark::module_base<ark::module>>);
static_assert(std::derived_from<sample_plugin, ark::plugin_base<sample_plugin>>);
// The legacy registration macro did not add these samples to the update list.
static_assert(std::same_as<decltype(&sample_module::update), decltype(&ark::module::update)>);

namespace {

class rejecting_database : public ark::module_base<database_module> {
public:
#ifdef ARK_DATABASE_SAMPLE_MYSQL
    bool configure(std::string name, ark::mysql_options) override {
        ++configuration_calls;
        connection_name = std::move(name);
        return false;
    }

    ark::task<ark::mysql_reply> query(std::string, std::string, std::chrono::milliseconds) override {
        throw std::logic_error("Unexpected MySQL sample query");
    }

    ark::task<ark::mysql_reply> execute(
        std::string, std::string, std::vector<arknet::mysql::field_value>, std::chrono::milliseconds) override {
        throw std::logic_error("Unexpected MySQL sample execute");
    }

    ark::task<ark::mysql_transaction_reply> begin(std::string, ark::mysql_transaction_options) override {
        throw std::logic_error("Unexpected MySQL sample transaction");
    }

    ark::task<ark::mysql_transaction_reply> query(ark::mysql_transaction_id, std::string) override {
        throw std::logic_error("Unexpected MySQL sample transaction query");
    }

    ark::task<ark::mysql_transaction_reply> execute(
        ark::mysql_transaction_id, std::string, std::vector<arknet::mysql::field_value>) override {
        throw std::logic_error("Unexpected MySQL sample transaction execute");
    }

    ark::task<ark::mysql_transaction_reply> commit(ark::mysql_transaction_id) override {
        throw std::logic_error("Unexpected MySQL sample commit");
    }

    ark::task<ark::mysql_transaction_reply> rollback(ark::mysql_transaction_id) override {
        throw std::logic_error("Unexpected MySQL sample rollback");
    }
#else
    bool configure(std::string name, ark::redis_options) override {
        ++configuration_calls;
        connection_name = std::move(name);
        return false;
    }

    ark::task<arknet::redis::result> command(std::string, arknet::redis::command, std::chrono::milliseconds) override {
        throw std::logic_error("Unexpected Redis sample command");
    }

    ark::task<arknet::redis::result> pipeline(
        std::string, std::vector<arknet::redis::command>, std::chrono::milliseconds) override {
        throw std::logic_error("Unexpected Redis sample pipeline");
    }
#endif

    unsigned configuration_calls{};
    std::string connection_name;
};

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::_Exit(1);
    }
}

} // namespace

int main() {
    ark::logger log;
    ark::logger::init(&log);
    log.set_log_func([](spdlog::level::level_enum, const std::string&) {});
    ark::plugin_manager manager;
    rejecting_database database;
    const std::string provider_key = typeid(database_module).name();
    database.set_plugin_manager(&manager);
    database.set_name(provider_key);
    require(manager.add_module(provider_key, &database), "Could not register the fixture database interface");
    // A local provider rejects configuration before coroutine or network work,
    // while exercising interface lookup from the real sample assembly.
    require(!manager.start(0, ARK_DATABASE_SAMPLE_DIR, "", {{ARK_DATABASE_SAMPLE_LIBRARY, ""}}),
        "Sample ignored the database configuration failure");
    const auto failure = manager.get_startup_failure();
    require(failure.starts_with("init: "), "Sample failed before its module init: " + failure);
    const auto key = failure.substr(6);
    require(key.find(std::string(ARK_DATABASE_SAMPLE_NAME) + "_module") != std::string::npos,
        "Sample registration key lost its concrete module name: " + key);
    require(manager.find_module<database_module>() == &database, "Fixture database registration changed");
    require(database.configuration_calls == 1, "Sample must configure its resolved database interface once");
#ifdef ARK_DATABASE_SAMPLE_MYSQL
    require(database.connection_name == "primary", "MySQL sample connection name changed");
#else
    require(database.connection_name == "sample", "Redis sample connection name changed");
#endif
    require(manager.pending_tasks() == 0, "Rejected configuration must not start managed business work");
    require(manager.stop(1), "Sample exit/unregistration failed: " + manager.get_shutdown_failure());
    require(manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::stopped,
        "Sample exit did not reach the stopped state");
    manager.remove_module(provider_key);
    require(manager.stop(2), "Repeated sample stop failed");
    std::cout << ARK_DATABASE_SAMPLE_NAME << ": actual entry, module init, rejected configuration and exit passed\n";
}
