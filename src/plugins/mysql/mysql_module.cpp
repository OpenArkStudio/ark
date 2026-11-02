// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <limits>
#include <optional>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>

#include "ark/module_base.hpp"
#include "mysql/detail/mysql_awaitable.hpp"
#include "mysql/detail/mysql_transaction_awaitable.hpp"
#include "mysql/mysql_plugin.hpp"

namespace ark {

class mysql_module_impl final : public module_base<mysql_module>
{
public:
    mysql_module_impl();
    ~mysql_module_impl() override;
    bool init() override;
    bool update() override;
    bool ready_to_shut() const override;
    bool pre_shut() override;
    bool shut() override;
    bool configure(std::string name, mysql_options options) override;
    task<mysql_reply> query(
        std::string name, std::string sql, std::chrono::milliseconds timeout = std::chrono::seconds(5)) override;
    task<mysql_reply> execute(std::string name, std::string sql, std::vector<arknet::mysql::field_value> parameters,
        std::chrono::milliseconds timeout = std::chrono::seconds(5)) override;
    task<mysql_transaction_reply> begin(std::string name, mysql_transaction_options options = {}) override;
    task<mysql_transaction_reply> query(mysql_transaction_id transaction, std::string sql) override;
    task<mysql_transaction_reply> execute(
        mysql_transaction_id transaction, std::string sql, std::vector<arknet::mysql::field_value> parameters) override;
    task<mysql_transaction_reply> commit(mysql_transaction_id transaction) override;
    task<mysql_transaction_reply> rollback(mysql_transaction_id transaction) override;

private:
    struct state;
    std::shared_ptr<state> state_;
};

struct mysql_module_impl::state
{
    struct connection
    {
        std::optional<detail::mysql_service> queries;
        detail::mysql_transaction_service transactions;
        detail::database_awaitable<detail::mysql_service_reply> query_bridge;
        detail::database_awaitable<detail::mysql_transaction_reply> transaction_bridge;

        connection(mysql_options options, const std::shared_ptr<arknet::manual_runtime>& runtime)
            : transactions(detail::mysql_transaction_service::prepared_t{},
                  {options.max_transactions, options.max_input_bytes}, options.queries)
            , query_bridge(runtime, options.max_operations)
            , transaction_bridge(runtime, options.max_operations) {
            // All connection bookkeeping exists before the native worker starts.
            queries.emplace(std::move(options.pool), options.max_operations, options.max_input_bytes, options.queries);
            transactions.bind_pool(queries->borrow_pool());
        }

        bool quiescent() const {
            return queries->quiescent() && transactions.quiescent() && query_bridge.quiescent() &&
                   transaction_bridge.quiescent();
        }
    };

    struct transaction
    {
        std::shared_ptr<connection> owner;
        detail::mysql_transaction handle;
        bool busy{};

        transaction(std::shared_ptr<connection> node, detail::mysql_transaction value)
            : owner(std::move(node))
            , handle(std::move(value)) {
        }
    };

    struct operation_guard
    {
        transaction& value;

        explicit operation_guard(transaction& item)
            : value(item) {
            if (value.busy)
            {
                throw std::logic_error("A transaction already has an active operation");
            }
            value.busy = true;
        }

        ~operation_guard() {
            value.busy = false;
        }
    };

    const std::thread::id owner{std::this_thread::get_id()};
    std::shared_ptr<arknet::manual_runtime> runtime;
    std::unordered_map<std::string, std::shared_ptr<connection>> connections;
    std::unordered_map<mysql_transaction_id, std::shared_ptr<transaction>> transactions;
    mysql_transaction_id next_id{1};
    bool closed{};

    void check_owner() const {
        if (owner != std::this_thread::get_id())
        {
            throw std::logic_error("MySQL module requires its framework thread");
        }
    }

    void check_open() const {
        check_owner();
        if (closed)
        {
            throw std::logic_error("MySQL module is retiring");
        }
    }

    std::shared_ptr<connection> find(const std::string& name) const {
        check_open();
        const auto found = connections.find(name);
        if (found == connections.end())
        {
            throw std::invalid_argument("Unknown MySQL connection name");
        }
        return found->second;
    }

    std::shared_ptr<transaction> find(mysql_transaction_id id) const {
        check_open();
        const auto found = transactions.find(id);
        if (!id || found == transactions.end())
        {
            throw std::invalid_argument("Unknown MySQL transaction");
        }
        return found->second;
    }

    static mysql_reply adapt(detail::mysql_service_reply reply) {
        return {std::move(reply.result), std::move(reply.statement_close), std::move(reply.statement_close_exception)};
    }

    static mysql_transaction_reply adapt(detail::mysql_transaction_reply reply, mysql_transaction_id id) {
        const auto& status = reply.transaction_status;
        mysql_transaction_status snapshot{static_cast<mysql_transaction_outcome>(status.outcome), status.rollback_code,
            status.rollback_outcome_unknown, status.cleanup_exception, status.cancellation_exception,
            status.lease_return_exception, status.discard_requested};
        return {std::move(reply.result), std::move(reply.statement_close), std::move(reply.statement_close_exception),
            std::move(reply.rollback), std::move(reply.initiation_exception), std::move(snapshot), id};
    }

    static task<mysql_reply> run_query(std::shared_ptr<state> shared, std::shared_ptr<connection> node, std::string sql,
        std::chrono::milliseconds timeout) {
        shared->check_open();
        co_return adapt(co_await detail::mysql_query(*node->queries, node->query_bridge, std::move(sql), timeout));
    }

    static task<mysql_reply> run_execute(std::shared_ptr<state> shared, std::shared_ptr<connection> node,
        std::string sql, std::vector<arknet::mysql::field_value> parameters, std::chrono::milliseconds timeout) {
        shared->check_open();
        co_return adapt(co_await detail::mysql_execute(
            *node->queries, node->query_bridge, std::move(sql), std::move(parameters), timeout));
    }

    static task<mysql_transaction_reply> run_begin(
        std::shared_ptr<state> shared, std::shared_ptr<connection> node, mysql_transaction_options options) {
        shared->check_open();
        if (shared->next_id == std::numeric_limits<mysql_transaction_id>::max())
        {
            throw std::overflow_error("MySQL transaction identifiers exhausted");
        }
        const auto id = shared->next_id++;
        auto reply = co_await detail::mysql_transaction_begin(node->transactions, node->transaction_bridge,
            detail::mysql_transaction_options{options.timeout, options.cleanup_timeout});
        mysql_transaction_id admitted{};
        if (reply.transaction)
        {
            // Allocation failure abandons the handle through its normal deadline
            // cleanup. No private transaction object escapes the plugin module.
            auto entry = std::make_shared<transaction>(node, std::move(*reply.transaction));
            shared->transactions.emplace(id, std::move(entry));
            admitted = id;
        }
        co_return adapt(std::move(reply), admitted);
    }
    enum class step
    {
        query,
        execute,
        commit,
        rollback
    };

    static task<mysql_transaction_reply> run_step(std::shared_ptr<state> shared, std::shared_ptr<transaction> entry,
        mysql_transaction_id id, step action, std::string sql = {},
        std::vector<arknet::mysql::field_value> parameters = {}) {
        shared->check_open();
        operation_guard active(*entry);
        auto& bridge = entry->owner->transaction_bridge;
        detail::mysql_transaction_reply reply;
        switch (action)
        {
        case step::query:
            reply = co_await detail::mysql_transaction_query(entry->handle, bridge, std::move(sql));
            break;
        case step::execute:
            reply = co_await detail::mysql_transaction_execute(
                entry->handle, bridge, std::move(sql), std::move(parameters));
            break;
        case step::commit:
            reply = co_await detail::mysql_transaction_commit(entry->handle, bridge);
            break;
        case step::rollback:
            reply = co_await detail::mysql_transaction_rollback(entry->handle, bridge);
            break;
        }
        co_return adapt(std::move(reply), id);
    }
};

mysql_module_impl::mysql_module_impl()
    : state_(std::make_shared<state>()) {
}

mysql_module_impl::~mysql_module_impl() = default;

bool mysql_module_impl::init() {
    state_->check_open();
    if (!get_plugin_manager())
    {
        return false;
    }
    state_->runtime = get_plugin_manager()->get_task_runtime();
    return true;
}

bool mysql_module_impl::configure(std::string name, mysql_options options) {
    auto shared = state_;
    shared->check_open();
    if (!get_plugin_manager() || shared->connections.contains(name))
    {
        return false;
    }
    if (name.empty() || !options.max_operations || !options.max_transactions || !options.max_input_bytes)
    {
        throw std::invalid_argument("MySQL requires a connection name and positive limits");
    }
    if (get_plugin_manager()->get_shutdown_state() != plugin_manager::shutdown_state::running)
    {
        return false;
    }
    shared->runtime = get_plugin_manager()->get_task_runtime();
    // Allocate the destination node and buckets before starting native IO.
    auto [entry, inserted] = shared->connections.try_emplace(std::move(name), nullptr);
    if (!inserted)
    {
        return false;
    }
    try
    {
        auto node = std::make_shared<state::connection>(std::move(options), shared->runtime);
        entry->second = std::move(node);
    }
    catch (...)
    {
        // This unpublished entry is still empty; preserve same-name retry.
        shared->connections.erase(entry);
        throw;
    }
    return true;
}

bool mysql_module_impl::update() {
    auto shared = state_;
    shared->check_owner();
    for (const auto& [name, node] : shared->connections)
    {
        node->queries->update();
        node->transactions.update();
        node->query_bridge.dispatch();
        node->transaction_bridge.dispatch();
    }
    for (auto iter = shared->transactions.begin(); iter != shared->transactions.end();)
    {
        const auto& entry = iter->second;
        if (!entry->busy && entry->handle.snapshot().outcome != detail::mysql_transaction_outcome::active)
        {
            iter = shared->transactions.erase(iter);
        }
        else
        {
            ++iter;
        }
    }
    return true;
}

bool mysql_module_impl::ready_to_shut() const {
    state_->check_owner();
    for (const auto& [name, node] : state_->connections)
    {
        if (!node->quiescent())
        {
            return false;
        }
    }
    return true;
}

bool mysql_module_impl::pre_shut() {
    auto shared = state_;
    shared->check_owner();
    if (!ready_to_shut())
    {
        return false;
    }
    shared->closed = true;
    for (const auto& [name, node] : shared->connections)
    {
        node->query_bridge.stop_accepting();
        node->transaction_bridge.stop_accepting();
        node->queries->stop_accepting();
        node->transactions.stop_accepting();
    }
    for (const auto& [name, node] : shared->connections)
    {
        if (!node->transactions.logically_drained() || !node->queries->logically_drained() ||
            !node->queries->retire_native())
        {
            return false;
        }
    }
    return true;
}

bool mysql_module_impl::shut() {
    auto shared = state_;
    shared->check_owner();
    if (!shared->closed)
    {
        return false;
    }
    for (const auto& [name, node] : shared->connections)
    {
        if (!node->queries->native_retired())
        {
            return false;
        }
    }
    shared->transactions.clear();
    shared->connections.clear();
    return true;
}

task<mysql_reply> mysql_module_impl::query(std::string name, std::string sql, std::chrono::milliseconds timeout) {
    return state::run_query(state_, state_->find(name), std::move(sql), timeout);
}

task<mysql_reply> mysql_module_impl::execute(std::string name, std::string sql,
    std::vector<arknet::mysql::field_value> parameters, std::chrono::milliseconds timeout) {
    return state::run_execute(state_, state_->find(name), std::move(sql), std::move(parameters), timeout);
}

task<mysql_transaction_reply> mysql_module_impl::begin(std::string name, mysql_transaction_options options) {
    return state::run_begin(state_, state_->find(name), options);
}

task<mysql_transaction_reply> mysql_module_impl::query(mysql_transaction_id id, std::string sql) {
    return state::run_step(state_, state_->find(id), id, state::step::query, std::move(sql));
}

task<mysql_transaction_reply> mysql_module_impl::execute(
    mysql_transaction_id id, std::string sql, std::vector<arknet::mysql::field_value> parameters) {
    return state::run_step(state_, state_->find(id), id, state::step::execute, std::move(sql), std::move(parameters));
}

task<mysql_transaction_reply> mysql_module_impl::commit(mysql_transaction_id id) {
    return state::run_step(state_, state_->find(id), id, state::step::commit);
}

task<mysql_transaction_reply> mysql_module_impl::rollback(mysql_transaction_id id) {
    return state::run_step(state_, state_->find(id), id, state::step::rollback);
}

namespace detail {

std::unique_ptr<mysql_module> make_mysql_module() {
    return std::make_unique<mysql_module_impl>();
}

} // namespace detail

} // namespace ark
