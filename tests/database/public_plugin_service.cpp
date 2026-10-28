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
#include <variant>
#include <vector>

#if defined(ARKNET_ENABLE_SSL)
#include <openssl/err.h>
#include <openssl/sslerr.h>
#endif

#include "ark/plugin_manager.hpp"
#include "ark/redis/redis_module.hpp"
#include "ark/task.hpp"

#if defined(ARKNET_ENABLE_SSL)
#include "ark/mysql/mysql_module.hpp"
#endif

namespace {

using namespace std::chrono_literals;

class acceptance_failure : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

void require(bool condition, const char* message) {
    if (!condition)
    {
        throw acceptance_failure(message);
    }
}

std::string environment(const char* name, bool allow_empty = false) {
    const auto* value = std::getenv(name);
    if (!value || (!allow_empty && !*value))
    {
        throw acceptance_failure(std::string("Required test environment variable: ") + name);
    }
    return value;
}

std::uint16_t port(const char* name) {
    const auto text = environment(name);
    unsigned number{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), number);
    require(parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && number > 0 && number <= 65535,
        "Database test port must be an integer between 1 and 65535");
    return static_cast<std::uint16_t>(number);
}

// The executable only knows public interfaces. The actual module allocation,
// implementation and deletion belong to the hidden-symbol plugin's DLL API.
class plugin_host
{
public:
    ark::plugin_manager manager;

    explicit plugin_host(const char* library) {
        ark::logger::init(nullptr);
        ark::logger::instance()->set_log_func([](spdlog::level::level_enum, const std::string&) {});
        require(manager.start(0, ARK_DATABASE_PLUGIN_DIR, "", {{library, ""}}), "Database plugin startup failed");
        require(manager.add_module("service_consumer", &consumer_), "Cannot register the service consumer");
        runtime_ = manager.get_task_runtime();
        tasks_ = &manager.create_task_group(consumer_);
    }

    ~plugin_host() {
        if (!stopped_)
        {
            try
            { drain(); }
            catch (...)
            {
                std::cerr << "Database plugin acceptance could not drain\n" << std::flush;
                std::_Exit(1);
            }
        }
    }

    void check_executor() const {
        require(std::this_thread::get_id() == owner_ && runtime_->get_executor().running_in_this_thread(),
            "Database DSO continuation escaped the owner thread/shared executor");
    }

    void run(ark::task<> workflow) {
        require(tasks_->start(std::move(workflow),
                    [this](std::exception_ptr error) {
                        completion_on_owner_ =
                            std::this_thread::get_id() == owner_ && runtime_->get_executor().running_in_this_thread();
                        failure_ = std::move(error);
                        ++completions_;
                    }) != 0,
            "Database DSO workflow was not admitted");
        // Stop admission before polling. Accepted work is deterministically
        // pending, so no network timing or sleep proves entry into drain.
        require(tasks_->pending() == 1 && !manager.stop(0) &&
                    manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::draining,
            "Manager did not naturally drain its accepted DSO workflow");
        drain();
        if (failure_)
        {
            std::rethrow_exception(failure_);
        }
        require(completions_ == 1 && completion_on_owner_ && tasks_->pending() == 0 && tasks_->closed() &&
                    manager.pending_tasks() == 0,
            "Database DSO completion or task retirement was incomplete");
        require(manager.stop(0), "Database plugin stop was not idempotent");
        manager.remove_module("service_consumer");
    }

private:
    void drain() {
        const auto deadline = std::chrono::steady_clock::now() + 30s;
        while (!(stopped_ = manager.stop(0)))
        {
            require(manager.get_shutdown_state() != ark::plugin_manager::shutdown_state::failed &&
                        std::chrono::steady_clock::now() < deadline,
                "Database plugin did not finish managed/native retirement");
            require(manager.update(0), "Database plugin update failed during drain");
            std::this_thread::sleep_for(1ms);
        }
        require(manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::stopped,
            "Database plugin shutdown did not complete");
    }

    ark::module consumer_;
    std::shared_ptr<arknet::manual_runtime> runtime_;
    ark::task_group* tasks_{};
    const std::thread::id owner_{std::this_thread::get_id()};
    std::exception_ptr failure_;
    unsigned completions_{};
    bool stopped_{};
    bool completion_on_owner_{};
};

ark::task<> redis_workflow(plugin_host& host, ark::redis_module& database, std::string binary,
    std::optional<arknet::redis::result>& retained) {
    std::vector<arknet::redis::command> commands{{"ECHO", binary}, {"ECHO", ""}, {"PING"}};
    retained = co_await database.pipeline("service", std::move(commands), 5s);
    host.check_executor();
    require(bool(*retained) && retained->replies.size() == 3 && retained->replies[0].text == binary &&
                retained->replies[1].text.empty() && retained->replies[2].text == "PONG",
        "Redis DSO lost successful ordered binary replies");
    arknet::redis::command ping{"PING"};
    auto next = co_await database.command("service", std::move(ping), 5s);
    host.check_executor();
    require(
        bool(next) && next.replies.size() == 1 && next.replies[0].text == "PONG" && retained->replies[0].text == binary,
        "Redis DSO follow-up failed or invalidated an owning result");
}

void check_redis() {
    ark::redis_options options;
    options.pool.connection.host = environment("ARK_DATABASE_REDIS_HOST");
    options.pool.connection.port = std::to_string(port("ARK_DATABASE_REDIS_PORT"));
    if (const auto* user = std::getenv("ARK_DATABASE_REDIS_USER"))
    {
        options.pool.connection.username = user;
    }
    if (const auto* password = std::getenv("ARK_DATABASE_REDIS_PASSWORD"))
    {
        options.pool.connection.password = password;
    }
    options.pool.max_connections = 1;
    options.pool.client_configuration.connect_timeout = 3s;
    const std::string binary("dso\0redis\r\nvalue", 16);
    std::optional<arknet::redis::result> retained;
    {
        plugin_host host("ark_database_redis_plugin");
        auto* database = host.manager.find_module<ark::redis_module>();
        require(database != nullptr, "Real Redis DSO did not register its public module");
        require(database->configure("service", options), "Redis DSO configuration failed");
        options.pool.connection.host = "invalid.invalid";
        options.pool.connection.password = "changed";
        host.run(redis_workflow(host, *database, binary, retained));
    }
    require(retained && bool(*retained) && retained->replies[0].text == binary,
        "Redis result did not survive plugin module/manager destruction");
}

#if defined(ARKNET_ENABLE_SSL)
ark::task<> redis_tls_workflow(plugin_host& host, ark::redis_module& database, std::string binary,
    std::optional<arknet::redis::result>& command, std::optional<arknet::redis::result>& pipeline) {
    arknet::redis::command echo{"ECHO", binary};
    command = co_await database.command("service", std::move(echo), 5s);
    host.check_executor();
    require(bool(*command) && command->replies.size() == 1 && command->replies[0].text == binary,
        "Redis TLS DSO command lost its owning binary result");
    std::vector<arknet::redis::command> commands{{"ECHO", binary}, {"ECHO", ""}, {"PING"}};
    pipeline = co_await database.pipeline("service", std::move(commands), 5s);
    host.check_executor();
    require(bool(*pipeline) && pipeline->replies.size() == 3 && pipeline->replies[0].text == binary &&
                pipeline->replies[1].text.empty() && pipeline->replies[2].text == "PONG" &&
                command->replies[0].text == binary,
        "Redis TLS DSO pipeline lost ordered owning results or invalidated the command result");
}

bool certificate_verification_failed(const arknet::redis::result& result) {
    return result.error == arknet::redis::client_errc::connection_failed &&
           result.transport_error.category() == asio::error::get_ssl_category() &&
           ERR_GET_REASON(static_cast<unsigned long>(result.transport_error.value())) ==
               SSL_R_CERTIFICATE_VERIFY_FAILED &&
           !result.command_may_have_run && result.replies.empty();
}

ark::task<> redis_tls_rejection_workflow(
    plugin_host& host, ark::redis_module& database, std::optional<arknet::redis::result>& retained) {
    arknet::redis::command ping{"PING"};
    retained = co_await database.command("service", std::move(ping), 5s);
    host.check_executor();
    require(certificate_verification_failed(*retained),
        "Redis TLS DSO did not preserve certificate verification rejection before command submission");
}

void check_redis_tls(std::string_view mode) {
    ark::redis_options options;
    options.tls = true;
    options.pool.connection.host =
        environment(mode == "redis-tls-wrong-host" ? "ARK_DATABASE_REDIS_WRONG_HOST" : "ARK_DATABASE_REDIS_HOST");
    options.pool.connection.port = std::to_string(port("ARK_DATABASE_REDIS_PORT"));
    options.pool.connection.password = environment("ARK_DATABASE_REDIS_PASSWORD");
    if (const auto* user = std::getenv("ARK_DATABASE_REDIS_USER"))
    {
        options.pool.connection.username = user;
    }
    options.pool.certificate_authority =
        environment(mode == "redis-tls-untrusted-ca" ? "ARK_DATABASE_REDIS_UNTRUSTED_CA" : "ARK_DATABASE_REDIS_CA");
    options.pool.max_connections = 1;
    options.pool.client_configuration.connect_timeout = 3s;
    const std::string binary("tls\0redis\r\nvalue", 16);
    std::optional<arknet::redis::result> command, pipeline, rejected;
    {
        plugin_host host("ark_database_redis_plugin");
        auto* database = host.manager.find_module<ark::redis_module>();
        require(database != nullptr, "Real Redis TLS DSO did not register its public module");
        require(database->configure("service", options), "Redis TLS DSO configuration failed");
        options.pool.connection.host = "invalid.invalid";
        options.pool.connection.password = "changed";
        options.pool.certificate_authority = "missing-certificate.pem";
        if (mode == "redis-tls")
        {
            host.run(redis_tls_workflow(host, *database, binary, command, pipeline));
        }
        else
        {
            host.run(redis_tls_rejection_workflow(host, *database, rejected));
        }
    }
    if (mode == "redis-tls")
    {
        require(command && pipeline && bool(*command) && bool(*pipeline) && command->replies[0].text == binary &&
                    pipeline->replies[0].text == binary && pipeline->replies[1].text.empty() &&
                    pipeline->replies[2].text == "PONG",
            "Redis TLS owning command/pipeline results did not survive host destruction");
    }
    else
    {
        require(rejected && certificate_verification_failed(*rejected),
            "Redis TLS owning verification error did not survive host destruction");
        std::cout << "Native connection_failed / TLS certificate verification failed; command_may_have_run=false; "
                     "replies=0\n";
    }
}

ark::task<> mysql_workflow(plugin_host& host, ark::mysql_module& database, std::string binary,
    std::optional<ark::mysql_reply>& text, std::optional<ark::mysql_reply>& prepared) {
    text = co_await database.query("service", "SELECT -7, 'owned text', NULL", 5s);
    host.check_executor();
    require(bool(*text) && text->result.rows.size() == 1 && text->result.rows[0].size() == 3 &&
                std::get<std::int64_t>(text->result.rows[0][0]) == -7 &&
                std::get<std::string>(text->result.rows[0][1]) == "owned text" &&
                std::holds_alternative<std::monostate>(text->result.rows[0][2]),
        "MySQL DSO text query lost successful typed owning fields");
    std::vector<arknet::mysql::field_value> parameters{std::int64_t{-9}, binary};
    prepared = co_await database.execute("service", "SELECT CAST(? AS SIGNED), ?", std::move(parameters), 5s);
    host.check_executor();
    require(bool(*prepared) && prepared->result.rows.size() == 1 && prepared->result.rows[0].size() == 2 &&
                std::get<std::int64_t>(prepared->result.rows[0][0]) == -9 &&
                std::get<std::string>(prepared->result.rows[0][1]) == binary && prepared->statement_close &&
                bool(*prepared->statement_close) && !prepared->statement_close_exception,
        "MySQL DSO prepared binary result or statement cleanup failed");
    for (const bool commit : {true, false})
    {
        auto begun = co_await database.begin("service", {10s, 3s});
        host.check_executor();
        require(bool(begun) && begun.transaction &&
                    begun.transaction_status.outcome == ark::mysql_transaction_outcome::active,
            "MySQL DSO transaction did not begin");
        auto selected = co_await database.query(begun.transaction, "SELECT -11");
        host.check_executor();
        require(bool(selected) && selected.result.rows.size() == 1 && selected.result.rows[0].size() == 1 &&
                    std::get<std::int64_t>(selected.result.rows[0][0]) == -11,
            "MySQL DSO transaction query failed");
        auto ended =
            commit ? co_await database.commit(begun.transaction) : co_await database.rollback(begun.transaction);
        host.check_executor();
        require(
            bool(ended) && ended.transaction_status.outcome == (commit ? ark::mysql_transaction_outcome::committed
                                                                       : ark::mysql_transaction_outcome::rolled_back),
            "MySQL DSO commit/rollback did not confirm its terminal outcome");
    }
}

void check_mysql() {
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
    const std::string binary("dso\0mysql\r\nvalue", 16);
    std::optional<ark::mysql_reply> text, prepared;
    {
        plugin_host host("ark_database_mysql_plugin");
        auto* database = host.manager.find_module<ark::mysql_module>();
        require(database != nullptr, "Real MySQL DSO did not register its public module");
        require(database->configure("service", options), "MySQL DSO configuration failed");
        options.pool.connection.host = "invalid.invalid";
        options.pool.connection.password = "changed";
        options.pool.connection.ca_file = "missing-certificate.pem";
        host.run(mysql_workflow(host, *database, binary, text, prepared));
    }
    require(text && prepared && std::get<std::string>(text->result.rows[0][1]) == "owned text" &&
                std::get<std::string>(prepared->result.rows[0][1]) == binary,
        "MySQL results did not survive plugin module/manager destruction");
}

template<class reply_type>
void mysql_success(plugin_host& host, const reply_type& reply) {
    host.check_executor();
    require(bool(reply) && !reply.statement_close_exception && (!reply.statement_close || bool(*reply.statement_close)),
        "MySQL DSO write workflow operation or statement cleanup failed");
}

std::uint64_t mysql_integer(const arknet::mysql::query_result& result) {
    require(result.rows.size() == 1 && result.rows[0].size() == 1, "MySQL DSO scalar result has wrong shape");
    const auto& field = result.rows[0][0];
    if (const auto* value = std::get_if<std::uint64_t>(&field))
    {
        return *value;
    }
    const auto* value = std::get_if<std::int64_t>(&field);
    require(value && *value >= 0, "MySQL DSO scalar result is not a nonnegative integer");
    return static_cast<std::uint64_t>(*value);
}

ark::task<> mysql_write_checks(plugin_host& host, ark::mysql_module& database, std::string table, std::string binary,
    std::optional<ark::mysql_reply>& retained, std::optional<ark::mysql_transaction_reply>& retained_transaction) {
    auto writer = co_await database.query("service", "SELECT CONNECTION_ID()", 5s);
    mysql_success(host, writer);
    auto observer = co_await database.query("observer", "SELECT CONNECTION_ID()", 5s);
    mysql_success(host, observer);
    require(mysql_integer(writer.result) != mysql_integer(observer.result),
        "MySQL DSO observer did not use an independent server connection");
    auto autocommit = co_await database.query("observer", "SELECT @@autocommit", 5s);
    mysql_success(host, autocommit);
    require(mysql_integer(autocommit.result) == 1, "MySQL DSO observer is not in autocommit mode");

    std::vector<arknet::mysql::field_value> initial{std::int64_t{0}, binary};
    auto written = co_await database.execute(
        "service", "INSERT INTO " + table + " (id, payload) VALUES (?, ?)", std::move(initial), 5s);
    mysql_success(host, written);
    require(written.result.affected_rows == 1 && written.statement_close,
        "MySQL DSO parameterized autocommit write failed");
    retained = co_await database.query("observer", "SELECT payload FROM " + table + " WHERE id=0", 5s);
    mysql_success(host, *retained);
    require(retained->result.rows.size() == 1 && retained->result.rows[0].size() == 1 &&
                std::get<std::string>(retained->result.rows[0][0]) == binary,
        "MySQL DSO observer did not see the parameterized autocommit write");

    for (const bool commit : {true, false})
    {
        auto begun = co_await database.begin("service", {15s, 3s});
        mysql_success(host, begun);
        require(begun.transaction && begun.transaction_status.outcome == ark::mysql_transaction_outcome::active,
            "MySQL DSO write transaction did not begin");
        const auto id = commit ? 1 : 2;
        const auto predicate = " WHERE id=" + std::to_string(id);
        std::exception_ptr failure;
        bool ended{};
        try
        {
            std::vector<arknet::mysql::field_value> values{std::int64_t{id}, binary};
            auto inserted = co_await database.execute(
                begun.transaction, "INSERT INTO " + table + " (id, payload) VALUES (?, ?)", std::move(values));
            mysql_success(host, inserted);
            require(inserted.result.affected_rows == 1 && inserted.statement_close,
                "MySQL DSO parameterized transaction write failed");
            retained_transaction =
                co_await database.query(begun.transaction, "SELECT payload FROM " + table + predicate);
            mysql_success(host, *retained_transaction);
            require(retained_transaction->result.rows.size() == 1 && retained_transaction->result.rows[0].size() == 1 &&
                        std::get<std::string>(retained_transaction->result.rows[0][0]) == binary,
                "MySQL DSO transaction did not read its own binary write");
            auto hidden = co_await database.query("observer", "SELECT COUNT(*) FROM " + table + predicate, 5s);
            mysql_success(host, hidden);
            require(mysql_integer(hidden.result) == 0, "MySQL DSO observer saw an uncommitted write");
            auto terminal =
                commit ? co_await database.commit(begun.transaction) : co_await database.rollback(begun.transaction);
            mysql_success(host, terminal);
            ended = true;
            require(terminal.transaction_status.outcome == (commit ? ark::mysql_transaction_outcome::committed
                                                                   : ark::mysql_transaction_outcome::rolled_back) &&
                        !terminal.transaction_status.cleanup_exception &&
                        !terminal.transaction_status.lease_return_exception,
                "MySQL DSO write transaction did not confirm commit/rollback");
            auto visible = co_await database.query("observer", "SELECT COUNT(*) FROM " + table + predicate, 5s);
            mysql_success(host, visible);
            require(mysql_integer(visible.result) == (commit ? 1 : 0),
                "MySQL DSO observer did not see confirmed commit/rollback visibility");
        }
        catch (...)
        { failure = std::current_exception(); }
        if (failure)
        {
            if (!ended)
            {
                // Explicit cleanup only; cancellation is not rollback evidence.
                co_await database.rollback(begun.transaction);
                host.check_executor();
            }
            std::rethrow_exception(failure);
        }
    }
    auto committed = co_await database.query("observer", "SELECT payload FROM " + table + " WHERE id=1", 5s);
    mysql_success(host, committed);
    require(committed.result.rows.size() == 1 && committed.result.rows[0].size() == 1 &&
                std::get<std::string>(committed.result.rows[0][0]) == binary &&
                std::get<std::string>(retained->result.rows[0][0]) == binary &&
                std::get<std::string>(retained_transaction->result.rows[0][0]) == binary,
        "MySQL DSO committed data or retained owning results changed after later operations");
}

ark::task<> mysql_write_workflow(plugin_host& host, ark::mysql_module& database, std::string table, std::string binary,
    std::optional<ark::mysql_reply>& retained, std::optional<ark::mysql_transaction_reply>& retained_transaction) {
    auto created = co_await database.query(
        "service", "CREATE TABLE " + table + " (id BIGINT PRIMARY KEY, payload VARBINARY(128)) ENGINE=InnoDB", 5s);
    mysql_success(host, created);
    std::exception_ptr failure;
    try
    { co_await mysql_write_checks(host, database, table, std::move(binary), retained, retained_transaction); }
    catch (...)
    { failure = std::current_exception(); }
    auto dropped = co_await database.query("observer", "DROP TABLE " + table, 5s);
    mysql_success(host, dropped);
    if (failure)
    {
        std::rethrow_exception(failure);
    }
}

void check_mysql_transactions() {
    ark::mysql_options options;
    options.pool.connection.host = environment("ARK_DATABASE_MYSQL_HOST");
    require(options.pool.connection.host == "127.0.0.1",
        "MySQL DSO write acceptance requires an owned IPv4 loopback service");
    options.pool.connection.port = port("ARK_DATABASE_MYSQL_PORT");
    options.pool.connection.username = environment("ARK_DATABASE_MYSQL_USER");
    options.pool.connection.password = environment("ARK_DATABASE_MYSQL_PASSWORD", true);
    options.pool.connection.database = environment("ARK_DATABASE_MYSQL_DATABASE");
    options.pool.connection.ca_file = environment("ARK_DATABASE_MYSQL_CA");
    options.pool.connection.tls = arknet::mysql::tls_mode::required;
    options.pool.connection.timeout = 5s;
    options.pool.max_connections = 1;
    const std::string binary("dso\0write'\\\r\nvalue", 18);
    const auto table =
        "ark_dso_transaction_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::optional<ark::mysql_reply> retained;
    std::optional<ark::mysql_transaction_reply> retained_transaction;
    {
        plugin_host host("ark_database_mysql_plugin");
        auto* database = host.manager.find_module<ark::mysql_module>();
        require(database != nullptr, "Real MySQL DSO did not register its public module");
        require(database->configure("service", options) && database->configure("observer", options),
            "MySQL DSO write/observer configuration failed");
        host.run(mysql_write_workflow(host, *database, table, binary, retained, retained_transaction));
    }
    require(retained && retained_transaction && bool(*retained) && bool(*retained_transaction) &&
                std::get<std::string>(retained->result.rows[0][0]) == binary &&
                std::get<std::string>(retained_transaction->result.rows[0][0]) == binary,
        "MySQL DSO write results did not survive plugin module/manager/runtime destruction");
}
#endif

} // namespace

int main(int argc, char** argv) {
    try
    {
        require(argc == 2, "Usage: ark_database_public_plugin_service_tests "
                           "redis|mysql|mysql-transactions|redis-tls|redis-tls-untrusted-ca|redis-tls-wrong-host");
        const std::string_view protocol(argv[1]);
        if (protocol == "redis")
        {
            check_redis();
        }
#if defined(ARKNET_ENABLE_SSL)
        else if (protocol == "redis-tls" || protocol == "redis-tls-untrusted-ca" || protocol == "redis-tls-wrong-host")
        {
            check_redis_tls(protocol);
        }
        else if (protocol == "mysql")
        {
            check_mysql();
        }
        else if (protocol == "mysql-transactions")
        {
            check_mysql_transactions();
        }
#endif
        else
        {
            throw acceptance_failure("Requested database plugin protocol is not enabled");
        }
        std::cout << protocol << " real DSO/service, owner delivery, drain and owning-result checks passed\n";
        return 0;
    }
    catch (const acceptance_failure& error)
    { std::cerr << error.what() << '\n'; }
    catch (...)
    {
        // Do not print credentials or server-controlled native exception text.
        std::cerr << "Database plugin service acceptance raised an unexpected exception\n";
    }
    return 1;
}
