// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include <arknet/base/detail/deadline.hpp>
#include <arknet/mysql/pool.hpp>

#include "detail/database_completion_queue.hpp"
#include "detail/database_service_activity.hpp"

namespace ark::detail {

enum class mysql_transaction_outcome
{
    not_started,
    active,
    committed,
    rolled_back,
    unconfirmed
};

struct mysql_transaction_options
{
    std::chrono::milliseconds timeout;
    std::chrono::milliseconds cleanup_timeout;
};

struct mysql_transaction_limits
{
    std::size_t max_transactions;
    std::size_t max_input_bytes;
};

struct mysql_transaction_snapshot
{
    mysql_transaction_outcome outcome{mysql_transaction_outcome::not_started};
    std::optional<arknet::mysql::connection_errc> rollback_code;
    bool rollback_outcome_unknown{};
    std::exception_ptr cleanup_exception;
    std::exception_ptr cancellation_exception;
    std::exception_ptr lease_return_exception;
    bool discard_requested{};
};

struct mysql_transaction_reply;
struct mysql_transaction_state;
struct mysql_transaction_owner;

// Private move-only handle. Destruction requests abandonment; it does not wait
// for, or claim completion of, an asynchronous rollback.
class mysql_transaction
{
public:
    using callback = std::function<void(mysql_transaction_reply)>;
    using request_id = std::uint64_t;

    mysql_transaction() = default;
    mysql_transaction(const mysql_transaction&) = delete;
    mysql_transaction& operator=(const mysql_transaction&) = delete;
    mysql_transaction(mysql_transaction&&) noexcept = default;
    mysql_transaction& operator=(mysql_transaction&& other) noexcept;
    ~mysql_transaction();

    request_id start_query(std::string sql, callback completion);
    request_id start_execute(std::string sql, std::vector<arknet::mysql::field_value> parameters, callback completion);
    request_id start_commit(callback completion);
    request_id start_rollback(callback completion);
    bool cancel(request_id id);
    void abort() noexcept;
    mysql_transaction_snapshot snapshot() const;

private:
    friend struct mysql_transaction_owner;

    explicit mysql_transaction(std::shared_ptr<mysql_transaction_state> state)
        : state_(std::move(state)) {
    }

    std::shared_ptr<mysql_transaction_owner> lock_owner() const;
    std::shared_ptr<mysql_transaction_state> state_;
};

struct mysql_transaction_reply
{
    arknet::mysql::query_result result;
    std::optional<arknet::mysql::connection_result> statement_close;
    std::exception_ptr statement_close_exception;
    std::optional<arknet::mysql::query_result> rollback;
    std::exception_ptr initiation_exception;
    mysql_transaction_snapshot transaction_status;
    std::optional<mysql_transaction> transaction;

    // Business result only. Statement cleanup, rollback and final transaction
    // outcome remain independent diagnostics, even after a successful execute.
    explicit operator bool() const noexcept {
        return !initiation_exception && bool(result);
    }
};

enum class mysql_transaction_phase
{
    acquiring,
    beginning,
    idle,
    query,
    preparing,
    executing,
    closing_statement,
    committing,
    rolling_back,
    cleanup,
    terminal
};

struct mysql_transaction_state
{
    using event = std::variant<arknet::mysql::pool::acquire_result, arknet::mysql::query_result,
        arknet::mysql::prepare_result, arknet::mysql::connection_result, std::exception_ptr>;
    using queue_type = completion_queue<mysql_transaction_reply>;

    std::weak_ptr<mysql_transaction_owner> owner;
    const std::thread::id thread{std::this_thread::get_id()};
    arknet::mysql::pool::lease lease;
    arknet::mysql::prepared_statement statement;
    std::vector<arknet::mysql::field_value> parameters;
    mysql_transaction_phase phase{mysql_transaction_phase::acquiring};
    mysql_transaction_snapshot status;
    std::chrono::steady_clock::time_point deadline;
    std::chrono::milliseconds cleanup_timeout;
    std::optional<std::chrono::steady_clock::time_point> cleanup_deadline;
    queue_type::ticket delivery;
    mysql_transaction_reply reply;
    std::uint64_t logical_id{};
    arknet::mysql::request_id native_id{};
    std::atomic<std::uint64_t> epoch{};
    std::atomic<unsigned> deliveries{};
    std::atomic<bool> abandoned{};
    std::atomic<bool> retired{};
    bool cancellation_requested{};
    bool abort_requested{};
    bool begin_delivery{};
    std::string begin_command{"START TRANSACTION"};
    std::string rollback_command{"ROLLBACK AND NO CHAIN NO RELEASE"};
    std::mutex event_mutex;
    std::optional<event> completed;

    // The IO callback never touches the shared lease or invokes user code.
    void publish(std::uint64_t expected, event response) {
        std::lock_guard lock(event_mutex);
        if (retired || epoch != expected || completed)
        {
            if (auto* acquired = std::get_if<arknet::mysql::pool::acquire_result>(&response))
            {
                acquired->connection.discard();
            }
            return;
        }
        completed.emplace(std::move(response));
    }
};

struct mysql_transaction_owner : std::enable_shared_from_this<mysql_transaction_owner>
{
    using state_type = mysql_transaction_state;
    using phase_type = mysql_transaction_phase;
    using callback = mysql_transaction::callback;
    using request_id = mysql_transaction::request_id;
    using queue_type = state_type::queue_type;
    using clock = std::chrono::steady_clock;

    struct delivery_reservation
    {
        std::shared_ptr<state_type> state;
        std::shared_ptr<std::atomic<std::size_t>> input;
        std::size_t bytes{};

        ~delivery_reservation() {
            input->fetch_sub(bytes);
            --state->deliveries;
        }
    };

    mysql_transaction_owner(std::shared_ptr<arknet::mysql::pool> pool, mysql_transaction_limits limits,
        arknet::mysql::query_options query_limits)
        : mysql_transaction_owner(limits, query_limits) {
        if (!pool)
        {
            throw std::invalid_argument("MySQL transaction service requires an existing pool");
        }
        this->pool = std::move(pool);
    }

    // Allocate the complete passive owner before its enclosing connection starts IO.
    mysql_transaction_owner(mysql_transaction_limits limits, arknet::mysql::query_options query_limits)
        : queue(std::make_shared<queue_type>(limits.max_transactions, limits.max_input_bytes))
        , states(limits.max_transactions)
        , limits(query_limits)
        , max_input_bytes(limits.max_input_bytes) {
    }

    void check_owner() const {
        if (std::this_thread::get_id() != thread)
        {
            throw std::logic_error("MySQL transactions must be driven on their module thread");
        }
    }

    static request_id reject(callback completion, arknet::mysql::connection_errc error) {
        if (completion)
        {
            mysql_transaction_reply reply;
            reply.result.status.code = error;
            completion(std::move(reply));
        }
        return 0;
    }

    bool reserve(const std::shared_ptr<state_type>& state, std::size_t bytes, callback completion) {
        if (bytes > max_input_bytes - retained_input->load())
        {
            return false;
        }
        if (!next_id)
        {
            throw std::overflow_error("MySQL transaction request IDs exhausted");
        }
        auto reservation = std::make_shared<delivery_reservation>();
        reservation->state = state;
        reservation->input = retained_input;
        reservation->bytes = bytes;
        ++state->deliveries;
        retained_input->fetch_add(bytes);
        auto delivery = queue->admit(
            bytes, [reservation, completion = std::move(completion)](mysql_transaction_reply result) mutable {
                if (completion)
                {
                    completion(std::move(result));
                }
            });
        if (!delivery)
        {
            return false;
        }
        state->delivery = std::move(delivery);
        state->logical_id = next_id++;
        return true;
    }

    request_id begin(mysql_transaction_options options, callback completion) {
        check_owner();
        database_service_activity::guard active(activity);
        const auto now = clock::now();
        const auto deadline = arknet::detail::checked_deadline(now, options.timeout);
        if (!deadline || !arknet::detail::checked_deadline(now, options.cleanup_timeout))
        {
            return reject(std::move(completion), arknet::mysql::connection_errc::invalid_options);
        }
        if (!accepting)
        {
            return reject(std::move(completion), arknet::mysql::connection_errc::not_connected);
        }
        for (auto& slot : states)
        {
            if (slot)
            {
                continue;
            }
            auto state = std::make_shared<state_type>();
            state->owner = shared_from_this();
            state->deadline = *deadline;
            state->cleanup_timeout = options.cleanup_timeout;
            state->begin_delivery = true;
            auto handler = std::make_shared<callback>(std::move(completion));
            if (!reserve(state, 0, [handler](auto reply) {
                    if (*handler)
                    {
                        (*handler)(std::move(reply));
                    }
                }))
            {
                return reject(std::move(*handler), arknet::mysql::connection_errc::queue_full);
            }
            slot = state;
            const auto expected = ++state->epoch;
            try
            {
                const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(*deadline - clock::now());
                if (remaining <= std::chrono::milliseconds::zero())
                {
                    arknet::mysql::pool::acquire_result expired;
                    expired.status.code = arknet::mysql::connection_errc::timeout;
                    state->publish(expected, std::move(expired));
                    return state->logical_id;
                }
                state->native_id = pool->async_acquire(
                    [state, expected](
                        arknet::mysql::pool::acquire_result result) { state->publish(expected, std::move(result)); },
                    remaining);
            }
            catch (...)
            { state->publish(expected, std::current_exception()); }
            return state->logical_id;
        }
        return reject(std::move(completion), arknet::mysql::connection_errc::queue_full);
    }

    request_id submit(const std::shared_ptr<state_type>& state, std::string sql, phase_type phase, callback completion,
        std::vector<arknet::mysql::field_value> parameters = {}) {
        check_owner();
        database_service_activity::guard active(activity);
        if (state->retired || state->phase == phase_type::terminal)
        {
            return reject(std::move(completion), arknet::mysql::connection_errc::not_connected);
        }
        if (state->phase != phase_type::idle)
        {
            return reject(std::move(completion), arknet::mysql::connection_errc::already_started);
        }
        if (state->abandoned || clock::now() >= state->deadline)
        {
            state->abandoned = true;
            return reject(std::move(completion), arknet::mysql::connection_errc::timeout);
        }
        std::size_t bytes = sql.capacity();
        if (parameters.capacity() >
            (std::numeric_limits<std::size_t>::max() - bytes) / sizeof(arknet::mysql::field_value))
        {
            return reject(std::move(completion), arknet::mysql::connection_errc::queue_full);
        }
        bytes += parameters.capacity() * sizeof(arknet::mysql::field_value);
        for (const auto& value : parameters)
        {
            if (const auto* text = std::get_if<std::string>(&value))
            {
                if (text->capacity() > std::numeric_limits<std::size_t>::max() - bytes)
                {
                    return reject(std::move(completion), arknet::mysql::connection_errc::queue_full);
                }
                bytes += text->capacity();
            }
        }
        auto handler = std::make_shared<callback>(std::move(completion));
        if (!reserve(state, bytes, [handler](auto reply) {
                if (*handler)
                {
                    (*handler)(std::move(reply));
                }
            }))
        {
            return reject(std::move(*handler), arknet::mysql::connection_errc::queue_full);
        }
        state->reply = {};
        state->begin_delivery = false;
        state->cancellation_requested = false;
        state->abort_requested = false;
        state->parameters = std::move(parameters);
        issue(state, std::move(sql), phase);
        return state->logical_id;
    }

    void issue(const std::shared_ptr<state_type>& state, std::string sql, phase_type phase) {
        state->phase = phase;
        state->native_id = 0;
        const auto expected = ++state->epoch;
        auto options = limits;
        const auto deadline = phase == phase_type::cleanup ? *state->cleanup_deadline : state->deadline;
        options.timeout = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - clock::now());
        if (options.timeout <= std::chrono::milliseconds::zero())
        {
            if (phase == phase_type::closing_statement)
            {
                arknet::mysql::connection_result expired;
                expired.code = arknet::mysql::connection_errc::timeout;
                state->publish(expected, std::move(expired));
            }
            else
            {
                arknet::mysql::query_result expired;
                expired.status.code = arknet::mysql::connection_errc::timeout;
                state->publish(expected, std::move(expired));
            }
            return;
        }
        try
        {
            if (phase == phase_type::preparing)
            {
                state->native_id = state->lease.async_prepare(
                    std::move(sql),
                    [state, expected](
                        arknet::mysql::prepare_result result) { state->publish(expected, std::move(result)); },
                    options);
            }
            else if (phase == phase_type::executing)
            {
                state->native_id = state->lease.async_execute(
                    state->statement, std::move(state->parameters),
                    [state, expected](
                        arknet::mysql::query_result result) { state->publish(expected, std::move(result)); },
                    options);
            }
            else if (phase == phase_type::closing_statement)
            {
                state->native_id = state->lease.async_close(
                    state->statement,
                    [state, expected](
                        arknet::mysql::connection_result result) { state->publish(expected, std::move(result)); },
                    options);
            }
            else
            {
                state->native_id = state->lease.async_query(
                    std::move(sql),
                    [state, expected](
                        arknet::mysql::query_result result) { state->publish(expected, std::move(result)); },
                    options);
            }
        }
        catch (...)
        { state->publish(expected, std::current_exception()); }
    }

    bool cancel(const std::shared_ptr<state_type>& state, request_id id) {
        check_owner();
        if (!id || id != state->logical_id || state->phase == phase_type::idle ||
            state->phase == phase_type::terminal || state->phase == phase_type::cleanup ||
            state->cancellation_requested)
        {
            return false;
        }
        bool native_completed{};
        {
            std::lock_guard lock(state->event_mutex);
            native_completed = state->completed.has_value();
            if (native_completed && state->phase != phase_type::preparing && state->phase != phase_type::executing &&
                state->phase != phase_type::closing_statement)
            {
                return false;
            }
        }
        state->abort_requested = true;
        state->cancellation_requested = true;
        // A prepared step is not terminal when one native stage completes.
        // Cancel its pending owner transition without cancelling a stale ID.
        if (native_completed)
        {
            return true;
        }
        try
        {
            if (state->phase == phase_type::acquiring)
            {
                pool->cancel(state->native_id);
            }
            else
            {
                state->lease.cancel(state->native_id);
            }
        }
        catch (...)
        { state->status.cancellation_exception = std::current_exception(); }
        return true;
    }

    void release(const std::shared_ptr<state_type>& state, bool discard) {
        state->status.discard_requested = discard;
        try
        {
            if (discard)
            {
                state->lease.discard();
            }
            else
            {
                state->lease.release();
            }
        }
        catch (...)
        { state->status.lease_return_exception = std::current_exception(); }
        state->phase = phase_type::terminal;
        state->statement = {};
        std::vector<arknet::mysql::field_value>{}.swap(state->parameters);
    }

    void deliver(const std::shared_ptr<state_type>& state) {
        state->statement = {};
        std::vector<arknet::mysql::field_value>{}.swap(state->parameters);
        if (state->delivery)
        {
            state->reply.transaction_status = state->status;
            if (state->begin_delivery && state->status.outcome == mysql_transaction_outcome::active)
            {
                state->reply.transaction.emplace(mysql_transaction(state));
            }
            queue->complete(std::exchange(state->delivery, {}), std::move(state->reply));
        }
    }

    void cleanup(const std::shared_ptr<state_type>& state) {
        state->abort_requested = true;
        if (!state->cleanup_deadline)
        {
            state->cleanup_deadline = arknet::detail::checked_deadline(clock::now(), state->cleanup_timeout);
        }
        if (!state->lease || !state->cleanup_deadline)
        {
            state->status.outcome = mysql_transaction_outcome::unconfirmed;
            release(state, true);
            deliver(state);
            return;
        }
        issue(state, std::move(state->rollback_command), phase_type::cleanup);
    }

    void close_statement(const std::shared_ptr<state_type>& state) {
        // An uncertain command cannot be replayed or followed by cleanup SQL.
        // Discarding its connection also disposes of its server-side statement.
        if (state->reply.result.status.outcome_unknown)
        {
            state->status.outcome = mysql_transaction_outcome::unconfirmed;
            release(state, true);
            deliver(state);
            return;
        }
        issue(state, {}, phase_type::closing_statement);
    }

    void advance(const std::shared_ptr<state_type>& state, state_type::event response) {
        state->native_id = 0;
        if (auto* prepared = std::get_if<arknet::mysql::prepare_result>(&response))
        {
            if (!*prepared)
            {
                arknet::mysql::query_result failed;
                failed.status = std::move(prepared->status);
                state->phase = phase_type::query;
                advance(state, std::move(failed));
                return;
            }
            state->statement = std::move(prepared->statement);
            if (state->abort_requested || state->abandoned || clock::now() >= state->deadline)
            {
                state->reply.result.status.code = state->abort_requested || state->abandoned
                                                      ? arknet::mysql::connection_errc::cancelled
                                                      : arknet::mysql::connection_errc::timeout;
                close_statement(state);
            }
            else
            {
                issue(state, {}, phase_type::executing);
            }
            return;
        }
        if (auto* closed = std::get_if<arknet::mysql::connection_result>(&response))
        {
            state->reply.statement_close.emplace(std::move(*closed));
            if (!*state->reply.statement_close)
            {
                state->status.outcome = mysql_transaction_outcome::unconfirmed;
                release(state, true);
                deliver(state);
            }
            else
            {
                state->phase = phase_type::query;
                advance(state, std::move(state->reply.result));
            }
            return;
        }
        if (auto* acquired = std::get_if<arknet::mysql::pool::acquire_result>(&response))
        {
            if (!*acquired)
            {
                state->reply.result.status = std::move(acquired->status);
                if (state->reply.result.status.code == arknet::mysql::connection_errc::none)
                {
                    state->reply.result.status.code = arknet::mysql::connection_errc::not_connected;
                }
                release(state, true);
                deliver(state);
                return;
            }
            state->lease = std::move(acquired->connection);
            if (state->abort_requested || state->abandoned || clock::now() >= state->deadline)
            {
                state->reply.result.status.code = state->abort_requested ? arknet::mysql::connection_errc::cancelled
                                                                         : arknet::mysql::connection_errc::timeout;
                release(state, false);
                deliver(state);
                return;
            }
            issue(state, std::move(state->begin_command), phase_type::beginning);
            return;
        }
        const bool cleaning = state->phase == phase_type::cleanup;
        if (auto* error = std::get_if<std::exception_ptr>(&response))
        {
            if (state->phase == phase_type::closing_statement)
            {
                state->reply.statement_close_exception = *error;
            }
            else if (cleaning)
            {
                state->status.cleanup_exception = *error;
            }
            else
            {
                state->reply.initiation_exception = *error;
            }
            state->status.outcome = state->phase == phase_type::acquiring ? mysql_transaction_outcome::not_started
                                                                          : mysql_transaction_outcome::unconfirmed;
            release(state, true);
            deliver(state);
            return;
        }
        auto result = std::get<arknet::mysql::query_result>(std::move(response));
        if (state->phase == phase_type::executing)
        {
            state->reply.result = std::move(result);
            close_statement(state);
            return;
        }
        // SERVER_STATUS_IN_TRANS is bit 0 in MySQL OK/EOF status flags.
        const bool in_transaction = (result.status_flags & 1u) != 0;
        if (cleaning)
        {
            state->status.outcome = result && !in_transaction ? mysql_transaction_outcome::rolled_back
                                                              : mysql_transaction_outcome::unconfirmed;
            state->status.rollback_code = result.status.code;
            state->status.rollback_outcome_unknown = result.status.outcome_unknown;
            state->reply.rollback.emplace(std::move(result));
            release(state, state->status.outcome != mysql_transaction_outcome::rolled_back);
            deliver(state);
            return;
        }
        state->reply.result = std::move(result);
        if (state->phase == phase_type::beginning && state->reply.result && in_transaction)
        {
            state->status.outcome = mysql_transaction_outcome::active;
            if (state->abort_requested || state->abandoned || clock::now() >= state->deadline)
            {
                cleanup(state);
            }
            else
            {
                state->phase = phase_type::idle;
                deliver(state);
            }
            return;
        }
        if (state->phase == phase_type::committing && state->reply.result && !in_transaction)
        {
            state->status.outcome = mysql_transaction_outcome::committed;
            release(state, false);
            deliver(state);
            return;
        }
        if (state->phase == phase_type::rolling_back)
        {
            state->status.outcome = state->reply.result && !in_transaction ? mysql_transaction_outcome::rolled_back
                                                                           : mysql_transaction_outcome::unconfirmed;
            release(state, state->status.outcome != mysql_transaction_outcome::rolled_back);
            deliver(state);
            return;
        }
        if (state->phase == phase_type::query && state->reply.result && in_transaction)
        {
            if (state->abort_requested || state->abandoned || clock::now() >= state->deadline)
            {
                cleanup(state);
            }
            else
            {
                state->phase = phase_type::idle;
                deliver(state);
            }
            return;
        }
        if (!state->reply.result && !state->reply.result.status.outcome_unknown && state->lease &&
            state->phase != phase_type::beginning)
        {
            cleanup(state);
            return;
        }
        state->status.outcome = mysql_transaction_outcome::unconfirmed;
        release(state, true);
        deliver(state);
    }

    std::size_t update(std::size_t maximum) {
        check_owner();
        database_service_activity::guard active(activity);
        const auto inspected = std::min(maximum, states.size());
        for (std::size_t index = 0; index < inspected; ++index)
        {
            auto state = states[progress_cursor];
            progress_cursor = (progress_cursor + 1) % states.size();
            if (!state || state->phase == phase_type::terminal)
            {
                continue;
            }
            std::optional<state_type::event> response;
            {
                std::lock_guard lock(state->event_mutex);
                response.swap(state->completed);
            }
            if (response)
            {
                advance(state, std::move(*response));
            }
            else if (state->phase == phase_type::idle && (state->abandoned || clock::now() >= state->deadline))
            {
                cleanup(state);
            }
            // In-flight native calls already own their remaining deadline.
            // Cancelling them here would race timeout into a user cancellation.
            else if (state->phase != phase_type::idle && state->phase != phase_type::cleanup && state->abandoned)
            {
                cancel(state, state->logical_id);
            }
        }
        const auto dispatched = queue->dispatch(maximum);
        const auto reaped = std::min(maximum, states.size());
        for (std::size_t index = 0; index < reaped; ++index)
        {
            auto& state = states[reap_cursor];
            reap_cursor = (reap_cursor + 1) % states.size();
            if (state && state->phase == phase_type::terminal && state->deliveries == 0)
            {
                state.reset();
            }
        }
        return dispatched;
    }

    // Seal only new transactions; accepted handles must finish their workflow.
    void stop_accepting() {
        check_owner();
        accepting = false;
    }

    // Logical completion only. The supplied pool remains borrowed and its owner
    // is responsible for any native close/join barrier after this observation.
    bool logically_drained() const {
        check_owner();
        return !accepting && quiescent();
    }

    bool quiescent() const {
        check_owner();
        if (closed || activity->depth != 0 || !queue->quiescent())
        {
            return false;
        }
        return std::none_of(states.begin(), states.end(), [](const auto& state) { return bool(state); });
    }

    void close() {
        check_owner();
        database_service_activity::guard active(activity);
        accepting = false;
        closed = true;
        for (auto& state : states)
        {
            if (state)
            {
                state->retired = true;
                if (state->phase != phase_type::terminal)
                {
                    state->status.outcome = mysql_transaction_outcome::unconfirmed;
                    release(state, true);
                }
            }
        }
        queue->stop();
        states.clear();
    }

    const std::thread::id thread{std::this_thread::get_id()};
    std::shared_ptr<arknet::mysql::pool> pool;
    std::shared_ptr<queue_type> queue;
    std::vector<std::shared_ptr<state_type>> states;
    arknet::mysql::query_options limits;
    const std::size_t max_input_bytes;
    std::shared_ptr<std::atomic<std::size_t>> retained_input{std::make_shared<std::atomic<std::size_t>>(0)};
    request_id next_id{1};
    std::size_t progress_cursor{};
    std::size_t reap_cursor{};
    bool accepting{true};
    bool closed{};
    std::shared_ptr<database_service_activity> activity{std::make_shared<database_service_activity>()};
};

inline mysql_transaction& mysql_transaction::operator=(mysql_transaction&& other) noexcept {
    if (this != &other)
    {
        abort();
        state_ = std::move(other.state_);
    }
    return *this;
}

inline mysql_transaction::~mysql_transaction() {
    abort();
}

inline void mysql_transaction::abort() noexcept {
    if (state_)
    {
        state_->abandoned = true;
    }
}

inline std::shared_ptr<mysql_transaction_owner> mysql_transaction::lock_owner() const {
    if (state_ && std::this_thread::get_id() != state_->thread)
    {
        throw std::logic_error("MySQL transaction handles must be used on their module thread");
    }
    return state_ ? state_->owner.lock() : nullptr;
}

inline mysql_transaction::request_id mysql_transaction::start_query(std::string sql, callback completion) {
    if (auto owner = lock_owner())
    {
        return owner->submit(state_, std::move(sql), mysql_transaction_phase::query, std::move(completion));
    }
    return mysql_transaction_owner::reject(std::move(completion), arknet::mysql::connection_errc::not_connected);
}

inline mysql_transaction::request_id mysql_transaction::start_execute(
    std::string sql, std::vector<arknet::mysql::field_value> parameters, callback completion) {
    if (auto owner = lock_owner())
    {
        return owner->submit(
            state_, std::move(sql), mysql_transaction_phase::preparing, std::move(completion), std::move(parameters));
    }
    return mysql_transaction_owner::reject(std::move(completion), arknet::mysql::connection_errc::not_connected);
}

inline mysql_transaction::request_id mysql_transaction::start_commit(callback completion) {
    if (auto owner = lock_owner())
    {
        return owner->submit(
            state_, "COMMIT AND NO CHAIN NO RELEASE", mysql_transaction_phase::committing, std::move(completion));
    }
    return mysql_transaction_owner::reject(std::move(completion), arknet::mysql::connection_errc::not_connected);
}

inline mysql_transaction::request_id mysql_transaction::start_rollback(callback completion) {
    if (auto owner = lock_owner())
    {
        return owner->submit(
            state_, "ROLLBACK AND NO CHAIN NO RELEASE", mysql_transaction_phase::rolling_back, std::move(completion));
    }
    return mysql_transaction_owner::reject(std::move(completion), arknet::mysql::connection_errc::not_connected);
}

inline bool mysql_transaction::cancel(request_id id) {
    if (auto owner = lock_owner())
    {
        return owner->cancel(state_, id);
    }
    return false;
}

inline mysql_transaction_snapshot mysql_transaction::snapshot() const {
    if (auto owner = lock_owner())
    {
        owner->check_owner();
    }
    return state_ ? state_->status : mysql_transaction_snapshot{};
}

// Development-only owner of transaction admission and delivery. The caller
// supplies an existing native pool; no new pool or worker is created here.
class mysql_transaction_service
{
public:
    using callback = mysql_transaction::callback;
    using request_id = mysql_transaction::request_id;

    mysql_transaction_service(std::shared_ptr<arknet::mysql::pool> pool, mysql_transaction_limits limits,
        arknet::mysql::query_options query_limits = {})
        : owner_(std::make_shared<mysql_transaction_owner>(std::move(pool), limits, query_limits)) {
    }

    // Internal connection-construction channel. Bind before publishing or starting work.
    struct prepared_t
    {
    };

    mysql_transaction_service(
        prepared_t, mysql_transaction_limits limits, arknet::mysql::query_options query_limits = {})
        : owner_(std::make_shared<mysql_transaction_owner>(limits, query_limits)) {
    }

    void bind_pool(std::shared_ptr<arknet::mysql::pool> pool) noexcept {
        assert(pool && !owner_->pool && owner_->thread == std::this_thread::get_id());
        assert(
            std::none_of(owner_->states.begin(), owner_->states.end(), [](const auto& state) { return bool(state); }));
        owner_->pool = std::move(pool);
    }

    ~mysql_transaction_service() {
        owner_->close();
    }

    mysql_transaction_service(const mysql_transaction_service&) = delete;
    mysql_transaction_service& operator=(const mysql_transaction_service&) = delete;

    request_id start(mysql_transaction_options options, callback completion) {
        // An inline refusal callback may destroy the facade.
        auto owner = owner_;
        return owner->begin(options, std::move(completion));
    }

    void stop_accepting() {
        owner_->stop_accepting();
    }

    bool logically_drained() const {
        return owner_->logically_drained();
    }

    bool quiescent() const {
        return owner_->quiescent();
    }

    bool cancel(request_id id) {
        owner_->check_owner();
        for (const auto& state : owner_->states)
        {
            if (state && state->logical_id == id)
            {
                return owner_->cancel(state, id);
            }
        }
        return false;
    }

    std::size_t update(std::size_t maximum = 64) {
        auto owner = owner_;
        return owner->update(maximum);
    }

    std::size_t active() const {
        owner_->check_owner();
        std::size_t count{};
        for (const auto& state : owner_->states)
        {
            count += bool(state);
        }
        return count;
    }

    std::size_t retained_input_bytes() const {
        owner_->check_owner();
        return owner_->retained_input->load();
    }

private:
    friend struct database_service_test_access;

    std::shared_ptr<mysql_transaction_owner> owner_;
};

} // namespace ark::detail
