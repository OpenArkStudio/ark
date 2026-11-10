// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "redis/redis_plugin.hpp"

#if defined(ARKNET_ENABLE_SSL)
#include "mysql/mysql_plugin.hpp"
#endif

namespace {

using namespace std::chrono_literals;
unsigned checks{};

class acceptance_failure : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void require(bool condition, const std::string& message) {
    ++checks;
    if (!condition) {
        throw acceptance_failure(message);
    }
}

std::string environment(const char* name, bool allow_empty = false) {
    const auto* value = std::getenv(name);
    require(value && (allow_empty || *value), std::string("Required test environment variable: ") + name);
    return value;
}

std::uint16_t port(const char* name) {
    const auto text = environment(name);
    unsigned number{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), number);
    require(parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && number > 0 && number <= 65535,
        std::string(name) + " must be an integer between 1 and 65535");
    return static_cast<std::uint16_t>(number);
}

struct workflow_result {
    std::exception_ptr failure;
    unsigned completions{};
    bool completion_on_owner{};
};

struct unwind_workflow {};

void verify_workflow(const workflow_result& result) {
    if (result.failure) {
        std::rethrow_exception(result.failure);
    }
    require(result.completions == 1 && result.completion_on_owner,
        "Workflow completion was not delivered once on its owner");
}

// Register the real implementation exactly as a plugin does, with a consumer's
// manager-owned task group sharing the module's manual_runtime.
template<class module_type>
struct module_owner {
    ark::plugin_manager manager;
    std::unique_ptr<module_type> database_owner;
    module_type& database;
    ark::module consumer;
    ark::task_group* tasks{};
    std::shared_ptr<arknet::manual_runtime> runtime;
    const std::thread::id thread{std::this_thread::get_id()};
    workflow_result& result;
    bool stopped{};

    module_owner(workflow_result& report, std::unique_ptr<module_type> instance)
        : database_owner(std::move(instance))
        , database(*database_owner)
        , result(report) {
        database.set_plugin_manager(&manager);
        database.set_name("database");
        require(manager.add_module("database", &database) && manager.add_module("consumer", &consumer),
            "Cannot install public database test modules");
        require(manager.add_update_module(&database), "Cannot install public database update hook");
        runtime = manager.get_task_runtime();
        tasks = &manager.create_task_group(consumer);
        require(database.init(), "Cannot initialize public database module");
    }

    ~module_owner() {
        if (!stopped) {
            // Never destroy business captures or a group with suspended tasks.
            // Failure paths keep pumping accepted work before unwinding.
            try {
                finish();
            } catch (...) {
                std::cerr << "Public database cleanup could not drain\n" << std::flush;
                std::_Exit(1);
            }
        }
    }

    void check_executor() const {
        require(std::this_thread::get_id() == thread && runtime->get_executor().running_in_this_thread(),
            "Public database coroutine escaped its owner thread/shared manual runtime");
    }

    void start(ark::task<> workflow) {
        require(tasks->start(std::move(workflow),
                    [this](std::exception_ptr error) {
                        result.completion_on_owner =
                            std::this_thread::get_id() == thread && runtime->get_executor().running_in_this_thread();
                        result.failure = std::move(error);
                        ++result.completions;
                    }) != 0,
            "Public database workflow was not accepted");
        require(manager.update(0), "Cannot start public database workflow");
        require(tasks->pending() == 1, "Public database workflow did not suspend for native IO");
    }

    void finish() {
        const auto deadline = std::chrono::steady_clock::now() + 30s;
        // Explicit application completion precedes resource cleanup, including host unwinding.
        while (tasks->pending()) {
            require(std::chrono::steady_clock::now() < deadline, "Public database workflow did not complete");
            require(manager.update(0), "Public database workflow owner update failed");
            std::this_thread::sleep_for(1ms);
        }
        while (!(stopped = manager.stop(0))) {
            if (manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::failed ||
                std::chrono::steady_clock::now() >= deadline) {
                throw acceptance_failure("Public database manager did not drain/stop successfully");
            }
            require(manager.update(0), "Public database manager update failed during drain");
            std::this_thread::sleep_for(1ms);
        }
    }

    void verify() {
        finish();
        verify_workflow(result);
        require(tasks->pending() == 0 && tasks->closed(), "Stopped manager retained public database tasks");
        require(manager.stop(0), "Manager stop was not idempotent");
    }
};

ark::task<> redis_workflow(module_owner<ark::redis_module>& owner, ark::task<arknet::redis::result> pending,
    std::string binary, std::optional<arknet::redis::result>& retained) {
    ark::redis_module& database = owner.database;
    retained = co_await std::move(pending);
    owner.check_executor();
    require(bool(*retained) && retained->replies.size() == 3, "Public Redis binary pipeline failed");
    require(
        retained->replies[0].text == binary && retained->replies[1].text.empty() && retained->replies[2].text == "PONG",
        "Public Redis lost owning inputs or ordered binary replies");
    auto second = co_await database.command("primary", {"PING"}, 5s);
    owner.check_executor();
    require(bool(second) && second.replies.size() == 1 && second.replies[0].text == "PONG",
        "Public Redis follow-up command failed before framework cleanup");
    require(retained->replies[0].text == binary, "Public Redis prior result changed after the next command");
}

void check_redis(bool unwind) {
    ark::redis_options options;
    options.pool.connection.host = environment("ARK_DATABASE_REDIS_HOST");
    options.pool.connection.port = std::to_string(port("ARK_DATABASE_REDIS_PORT"));
    if (const auto* user = std::getenv("ARK_DATABASE_REDIS_USER")) {
        options.pool.connection.username = user;
    }
    if (const auto* password = std::getenv("ARK_DATABASE_REDIS_PASSWORD")) {
        options.pool.connection.password = password;
    }
    options.pool.max_connections = 1;
    options.pool.client_configuration.connect_timeout = 3s;
    const std::string binary("public\0redis\r\nvalue", 19);
    std::optional<arknet::redis::result> retained;
    workflow_result report;
    bool unwound{};
    try {
        module_owner<ark::redis_module> owner(report, ark::detail::make_redis_module());
        ark::redis_module& database = owner.database;
        require(database.configure("primary", options), "Cannot configure public Redis module");
        // The module owns endpoint and authentication strings after configure().
        options.pool.connection.host = "invalid.invalid";
        options.pool.connection.username = "changed";
        options.pool.connection.password = "changed";
        std::vector<arknet::redis::command> commands{{"ECHO", binary}, {"ECHO", ""}, {"PING"}};
        auto pending = database.pipeline("primary", commands, 5s);
        commands[0][1] = "changed";
        commands.clear();
        owner.start(redis_workflow(owner, std::move(pending), binary, retained));
        if (unwind) {
            throw unwind_workflow{};
        }
        owner.verify();
    } catch (const unwind_workflow&) {
        unwound = true;
    }
    require(unwound == unwind, "Redis host stack did not unwind as requested");
    verify_workflow(report);
    require(retained && retained->replies[0].text == binary,
        "Public Redis owning result was lost after module/runtime destruction");
}

#if defined(ARKNET_ENABLE_SSL)
void success(const ark::mysql_reply& reply) {
    require(bool(reply), "Public MySQL operation failed (code " +
                             std::to_string(static_cast<int>(reply.result.status.code)) + ", server code " +
                             std::to_string(reply.result.status.server_code) + ")");
    require(!reply.statement_close_exception && (!reply.statement_close || bool(*reply.statement_close)),
        "Public MySQL statement cleanup failed");
}

void success(const ark::mysql_transaction_reply& reply) {
    require(bool(reply), "Public MySQL transaction operation failed (code " +
                             std::to_string(static_cast<int>(reply.result.status.code)) + ", server code " +
                             std::to_string(reply.result.status.server_code) + ")");
    require(!reply.statement_close_exception && (!reply.statement_close || bool(*reply.statement_close)) &&
                !reply.transaction_status.cleanup_exception && !reply.transaction_status.lease_return_exception,
        "Public MySQL transaction cleanup failed");
}

ark::task<> mysql_checks(module_owner<ark::mysql_module>& owner, ark::task<ark::mysql_reply> prepared,
    std::string table, std::string binary, std::optional<ark::mysql_reply>& retained_text,
    std::optional<ark::mysql_reply>& retained_prepared) {
    ark::mysql_module& database = owner.database;
    retained_text = co_await database.query("primary", "SELECT -7 AS repeated, 'owned text' AS repeated, NULL", 5s);
    owner.check_executor();
    success(*retained_text);
    const auto& text = retained_text->result;
    require(text.columns.size() == 3 && text.rows.size() == 1 && text.rows[0].size() == 3 &&
                text.columns[0].name == "repeated" && text.columns[1].name == "repeated",
        "Public MySQL query lost shape or duplicate column names");
    require(std::get<std::int64_t>(text.rows[0][0]) == -7 && std::get<std::string>(text.rows[0][1]) == "owned text" &&
                std::holds_alternative<std::monostate>(text.rows[0][2]),
        "Public MySQL query lost owning typed fields");
    retained_prepared = co_await std::move(prepared);
    owner.check_executor();
    success(*retained_prepared);
    const auto& result = retained_prepared->result;
    require(result.rows.size() == 1 && result.rows[0].size() == 2 && std::get<std::int64_t>(result.rows[0][0]) == -9 &&
                std::get<std::string>(result.rows[0][1]) == binary,
        "Public MySQL prepared query lost owning inputs/binary results");
    require(retained_prepared->statement_close && bool(*retained_prepared->statement_close),
        "Public MySQL prepared statement close was not reported");
    for (const bool commit : {true, false}) {
        auto begun = co_await database.begin("primary", {10s, 3s});
        owner.check_executor();
        success(begun);
        require(begun.transaction && begun.transaction_status.outcome == ark::mysql_transaction_outcome::active,
            "Public MySQL transaction did not begin");
        auto queried = co_await database.query(begun.transaction, "SELECT CONNECTION_ID()");
        owner.check_executor();
        success(queried);
        require(queried.result.rows.size() == 1, "Public MySQL transaction query failed");
        std::vector<arknet::mysql::field_value> values{std::int64_t{commit ? 1 : 2}, binary};
        auto inserted = co_await database.execute(
            begun.transaction, "INSERT INTO " + table + " (id, payload) VALUES (?, ?)", std::move(values));
        owner.check_executor();
        success(inserted);
        require(inserted.result.affected_rows == 1 && inserted.statement_close && bool(*inserted.statement_close),
            "Public MySQL transaction prepared insert/cleanup failed");
        auto ended =
            commit ? co_await database.commit(begun.transaction) : co_await database.rollback(begun.transaction);
        owner.check_executor();
        success(ended);
        require(ended.transaction_status.outcome ==
                    (commit ? ark::mysql_transaction_outcome::committed : ark::mysql_transaction_outcome::rolled_back),
            "Public MySQL commit/rollback terminal outcome changed");
    }
    auto persisted = co_await database.query("primary", "SELECT id, payload FROM " + table + " ORDER BY id", 5s);
    owner.check_executor();
    success(persisted);
    require(persisted.result.rows.size() == 1 && persisted.result.rows[0].size() == 2 &&
                std::get<std::int64_t>(persisted.result.rows[0][0]) == 1 &&
                std::get<std::string>(persisted.result.rows[0][1]) == binary,
        "Public MySQL commit/rollback visibility or binary persistence failed");
    require(std::get<std::string>(retained_text->result.rows[0][1]) == "owned text" &&
                std::get<std::string>(retained_prepared->result.rows[0][1]) == binary,
        "Public MySQL prior results changed after transaction reuse");
}

ark::task<> mysql_workflow(module_owner<ark::mysql_module>& owner, ark::task<ark::mysql_reply> prepared,
    std::string table, std::string binary, std::optional<ark::mysql_reply>& retained_text,
    std::optional<ark::mysql_reply>& retained_prepared) {
    ark::mysql_module& database = owner.database;
    auto created = co_await database.query(
        "primary", "CREATE TABLE " + table + " (id BIGINT PRIMARY KEY, payload VARBINARY(128)) ENGINE=InnoDB", 5s);
    owner.check_executor();
    success(created);
    std::exception_ptr error;
    try {
        co_await mysql_checks(owner, std::move(prepared), table, std::move(binary), retained_text, retained_prepared);
    } catch (...) {
        error = std::current_exception();
    }
    auto removed = co_await database.query("primary", "DROP TABLE " + table, 5s);
    owner.check_executor();
    success(removed);
    if (error) {
        std::rethrow_exception(error);
    }
}

void check_mysql(bool unwind) {
    ark::mysql_options options;
    options.pool.connection.host = environment("ARK_DATABASE_MYSQL_HOST");
    options.pool.connection.port = port("ARK_DATABASE_MYSQL_PORT");
    options.pool.connection.username = environment("ARK_DATABASE_MYSQL_USER");
    options.pool.connection.password = environment("ARK_DATABASE_MYSQL_PASSWORD", true);
    options.pool.connection.database = environment("ARK_DATABASE_MYSQL_DATABASE");
    options.pool.connection.ca_file = environment("ARK_DATABASE_MYSQL_CA");
    options.pool.connection.tls = arknet::mysql::tls_mode::required;
    options.pool.connection.timeout = 5s;
    options.pool.max_connections = 1;
    const std::string binary("public\0mysql\r\nvalue", 19);
    const auto table =
        "ark_public_service_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::optional<ark::mysql_reply> retained_text, retained_prepared;
    workflow_result report;
    bool unwound{};
    try {
        module_owner<ark::mysql_module> owner(report, ark::detail::make_mysql_module());
        ark::mysql_module& database = owner.database;
        require(database.configure("primary", options), "Cannot configure public MySQL module");
        options.pool.connection.host = "invalid.invalid";
        options.pool.connection.username = "changed";
        options.pool.connection.password = "changed";
        options.pool.connection.ca_file = "missing-certificate.pem";
        std::vector<arknet::mysql::field_value> parameters{std::int64_t{-9}, binary};
        auto prepared = database.execute("primary", "SELECT CAST(? AS SIGNED), ?", parameters, 5s);
        parameters[0] = std::int64_t{123};
        parameters[1] = std::string("changed");
        owner.start(mysql_workflow(owner, std::move(prepared), table, binary, retained_text, retained_prepared));
        if (unwind) {
            throw unwind_workflow{};
        }
        owner.verify();
    } catch (const unwind_workflow&) {
        unwound = true;
    }
    require(unwound == unwind, "MySQL host stack did not unwind as requested");
    verify_workflow(report);
    require(retained_text && std::get<std::string>(retained_text->result.rows[0][1]) == "owned text" &&
                retained_prepared && std::get<std::string>(retained_prepared->result.rows[0][1]) == binary,
        "Public MySQL owning results were lost after module/runtime destruction");
}
#endif

} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "Usage: ark_database_public_service_tests redis|mysql");
        const std::string_view protocol(argv[1]);
        if (protocol == "redis") {
            check_redis(false);
            check_redis(true);
        }
#if defined(ARKNET_ENABLE_SSL)
        else if (protocol == "mysql") {
            check_mysql(false);
            check_mysql(true);
        }
#endif
        else {
            throw acceptance_failure("Requested public database protocol is not enabled");
        }
        std::cout << protocol << " public module real-service checks passed: " << checks << '\n';
        return 0;
    } catch (const acceptance_failure& error) {
        std::cerr << error.what() << '\n';
    } catch (...) {
        // Native diagnostics can contain server-controlled text. Do not emit
        // endpoint/authentication configuration or arbitrary exception messages.
        std::cerr << "Public database service acceptance raised an unexpected exception\n";
    }
    return 1;
}
