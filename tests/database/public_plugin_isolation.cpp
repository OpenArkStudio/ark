// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "ark/mysql/mysql_module.hpp"
#include "ark/plugin_manager.hpp"
#include "ark/task.hpp"

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

template<class reply_type>
void mysql_success(plugin_host& host, const reply_type& reply) {
    host.check_executor();
    require(bool(reply) && !reply.statement_close_exception && (!reply.statement_close || bool(*reply.statement_close)),
        "MySQL isolation operation failed");
}

std::uint64_t integer(const arknet::mysql::field_value& field) {
    if (const auto* value = std::get_if<std::uint64_t>(&field))
    {
        return *value;
    }
    const auto* value = std::get_if<std::int64_t>(&field);
    require(value && *value >= 0, "MySQL isolation field is not a nonnegative integer");
    return static_cast<std::uint64_t>(*value);
}

std::uint64_t connection_id(const arknet::mysql::query_result& result) {
    require(result.rows.size() == 1 && result.rows[0].size() == 1, "Connection ID result has wrong shape");
    return integer(result.rows[0][0]);
}

void rows(const arknet::mysql::query_result& result, std::uint64_t reader_id, bool changed) {
    require(result.rows.size() == (changed ? 2 : 1), "Isolation read returned an unexpected row count");
    for (std::size_t index = 0; index < result.rows.size(); ++index)
    {
        const auto& row = result.rows[index];
        require(row.size() == 3 && integer(row[0]) == index + 1 && integer(row[2]) == reader_id,
            "Isolation read lost its row order or pinned server connection");
        require(std::get<std::string>(row[1]) == (index == 0 ? (changed ? "committed" : "original") : "new"),
            "Isolation read returned an unexpected committed value");
    }
}

void session(const arknet::mysql::query_result& result, std::uint64_t expected_id, bool repeatable) {
    const auto expected_level = repeatable ? "REPEATABLE-READ" : "READ-COMMITTED";
    require(result.rows.size() == 1 && result.rows[0].size() == 3 && integer(result.rows[0][0]) == expected_id &&
                std::get<std::string>(result.rows[0][1]) == expected_level &&
                std::get<std::string>(result.rows[0][2]) == expected_level,
        "Isolation begin did not retain its connection and expected server/session level after query lease reset");
}

void terminal(plugin_host& host, const ark::mysql_transaction_reply& reply, ark::mysql_transaction_outcome expected) {
    mysql_success(host, reply);
    require(reply.transaction_status.outcome == expected && !reply.transaction_status.cleanup_exception &&
                !reply.transaction_status.lease_return_exception,
        "Isolation transaction did not confirm its terminal outcome");
}

ark::task<> isolation_case(plugin_host& host, ark::mysql_module& database, std::string table, bool repeatable,
    std::vector<ark::mysql_transaction_reply>& retained_snapshots, std::vector<ark::mysql_reply>& retained_after) {
    // Returned query leases reset session state. Deliberately set the opposite
    // level, then verify begin() uses the explicitly configured server default.
    auto reader_session = co_await database.query("reader", "SELECT CONNECTION_ID()", 5s);
    mysql_success(host, reader_session);
    const auto reader_id = connection_id(reader_session.result);
    auto writer_session = co_await database.query("writer", "SELECT CONNECTION_ID()", 5s);
    mysql_success(host, writer_session);
    const auto writer_id = connection_id(writer_session.result);
    require(reader_id != writer_id, "Isolation clients share a server connection");
    auto setting = co_await database.query("reader",
        repeatable ? "SET SESSION TRANSACTION ISOLATION LEVEL READ COMMITTED"
                   : "SET SESSION TRANSACTION ISOLATION LEVEL REPEATABLE READ",
        5s);
    mysql_success(host, setting);

    auto reader = co_await database.begin("reader", {20s, 3s});
    mysql_success(host, reader);
    require(reader.transaction && reader.transaction_status.outcome == ark::mysql_transaction_outcome::active,
        "Isolation reader did not begin");
    ark::mysql_transaction_id writer{};
    bool reader_ended{};
    bool writer_ended{};
    std::exception_ptr failure;
    const auto select = "SELECT id, payload, CONNECTION_ID() FROM " + table + " ORDER BY id";
    try
    {
        const std::string inspect =
            "SELECT CONNECTION_ID(), @@SESSION.transaction_isolation, @@GLOBAL.transaction_isolation";
        auto level = co_await database.query(reader.transaction, inspect);
        mysql_success(host, level);
        session(level.result, reader_id, repeatable);
        auto first = co_await database.query(reader.transaction, select);
        mysql_success(host, first);
        rows(first.result, reader_id, false); // Establish the consistent-read snapshot.
        retained_snapshots.push_back(std::move(first));

        auto begun = co_await database.begin("writer", {20s, 3s});
        mysql_success(host, begun);
        writer = begun.transaction;
        require(writer && begun.transaction_status.outcome == ark::mysql_transaction_outcome::active,
            "Isolation writer did not begin");
        auto pinned = co_await database.query(writer, inspect);
        mysql_success(host, pinned);
        session(pinned.result, writer_id, repeatable);
        auto updated = co_await database.query(writer, "UPDATE " + table + " SET payload='committed' WHERE id=1");
        mysql_success(host, updated);
        require(updated.result.affected_rows == 1, "Isolation writer did not update the original row");
        auto inserted = co_await database.query(writer, "INSERT INTO " + table + " VALUES (2, 'new')");
        mysql_success(host, inserted);
        require(inserted.result.affected_rows == 1, "Isolation writer did not insert its new row");
        auto uncommitted = co_await database.query(reader.transaction, select);
        mysql_success(host, uncommitted);
        rows(uncommitted.result, reader_id, false);

        auto committed = co_await database.commit(writer);
        terminal(host, committed, ark::mysql_transaction_outcome::committed);
        writer_ended = true;
        auto later = co_await database.query(reader.transaction, select);
        mysql_success(host, later);
        rows(later.result, reader_id, !repeatable);
        // This owned initial result must still describe the original snapshot.
        rows(retained_snapshots.back().result, reader_id, false);
        auto ended = co_await database.commit(reader.transaction);
        terminal(host, ended, ark::mysql_transaction_outcome::committed);
        reader_ended = true;
        auto after = co_await database.query("reader", select, 5s);
        mysql_success(host, after);
        rows(after.result, reader_id, true);
        retained_after.push_back(std::move(after));
        std::cout << (repeatable ? "REPEATABLE READ" : "READ COMMITTED") << ": independent reader=" << reader_id
                  << " writer=" << writer_id << ", uncommitted data hidden, committed visibility verified\n";
    }
    catch (...)
    { failure = std::current_exception(); }
    if (failure)
    {
        if (writer && !writer_ended)
        {
            co_await database.rollback(writer);
            host.check_executor();
        }
        if (!reader_ended)
        {
            co_await database.rollback(reader.transaction);
            host.check_executor();
        }
        std::rethrow_exception(failure);
    }
}

ark::task<> isolation_workflow(plugin_host& host, ark::mysql_module& database, std::string table, bool repeatable,
    std::vector<ark::mysql_transaction_reply>& retained_snapshots, std::vector<ark::mysql_reply>& retained_after) {
    auto created = co_await database.query(
        "writer", "CREATE TABLE " + table + " (id BIGINT PRIMARY KEY, payload VARCHAR(32)) ENGINE=InnoDB", 5s);
    mysql_success(host, created);
    std::exception_ptr failure;
    try
    {
        auto initial = co_await database.query("writer", "INSERT INTO " + table + " VALUES (1, 'original')", 5s);
        mysql_success(host, initial);
        co_await isolation_case(host, database, table, repeatable, retained_snapshots, retained_after);
    }
    catch (...)
    { failure = std::current_exception(); }
    auto dropped = co_await database.query("writer", "DROP TABLE " + table, 5s);
    mysql_success(host, dropped);
    if (failure)
    {
        std::rethrow_exception(failure);
    }
}

void check_isolation(bool repeatable) {
    ark::mysql_options options;
    options.pool.connection.host = environment("ARK_DATABASE_MYSQL_HOST");
    require(options.pool.connection.host == "127.0.0.1", "MySQL isolation acceptance requires owned IPv4 loopback");
    options.pool.connection.port = port("ARK_DATABASE_MYSQL_PORT");
    options.pool.connection.username = environment("ARK_DATABASE_MYSQL_USER");
    options.pool.connection.password = environment("ARK_DATABASE_MYSQL_PASSWORD");
    options.pool.connection.database = environment("ARK_DATABASE_MYSQL_DATABASE");
    options.pool.connection.ca_file = environment("ARK_DATABASE_MYSQL_CA");
    options.pool.connection.tls = arknet::mysql::tls_mode::required;
    options.pool.connection.timeout = 5s;
    options.pool.max_connections = 1;
    const auto table =
        "ark_dso_isolation_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::vector<ark::mysql_transaction_reply> retained_snapshots;
    std::vector<ark::mysql_reply> retained_after;
    {
        plugin_host host("ark_database_mysql_plugin");
        auto* database = host.manager.find_module<ark::mysql_module>();
        require(database && database->configure("reader", options) && database->configure("writer", options),
            "Cannot configure public MySQL isolation clients");
        host.run(isolation_workflow(host, *database, table, repeatable, retained_snapshots, retained_after));
    }
    require(retained_snapshots.size() == 1 && retained_after.size() == 1,
        "Isolation owning results did not survive module/manager/runtime destruction");
    for (std::size_t index = 0; index < 1; ++index)
    {
        require(bool(retained_snapshots[index]) && bool(retained_after[index]), "Isolation retained result failed");
        const auto reader_id = integer(retained_snapshots[index].result.rows.at(0).at(2));
        rows(retained_snapshots[index].result, reader_id, false);
        rows(retained_after[index].result, reader_id, true);
    }
}

} // namespace

int main(int argc, char** argv) {
    try
    {
        require(argc == 2, "Usage: ark_database_public_plugin_isolation_tests repeatable-read|read-committed");
        const std::string_view expected(argv[1]);
        require(expected == "repeatable-read" || expected == "read-committed", "Unknown expected isolation level");
        check_isolation(expected == "repeatable-read");
        std::cout
            << "MySQL isolation real DSO/service, owner delivery, natural drain and owning-result checks passed\n";
        return 0;
    }
    catch (const acceptance_failure& error)
    { std::cerr << error.what() << '\n'; }
    catch (...)
    { std::cerr << "MySQL isolation acceptance raised an unexpected exception\n"; }
    return 1;
}
