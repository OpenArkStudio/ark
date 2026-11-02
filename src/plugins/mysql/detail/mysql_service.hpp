// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

#include <chrono>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <arknet/base/detail/deadline.hpp>
#include <arknet/mysql/pool.hpp>

#include "detail/database_completion_queue.hpp"
#include "detail/database_request_control.hpp"
#include "detail/database_service_activity.hpp"

namespace ark::detail {

// Internal bridge; the published plugin API remains unchanged.
// Keep execution and statement cleanup outcomes separate:
// a failed close must not turn a successful write into a failed/replayed write.
struct mysql_service_reply
{
    arknet::mysql::query_result result;
    std::optional<arknet::mysql::connection_result> statement_close;
    std::exception_ptr statement_close_exception;
};

using mysql_service_result = std::variant<mysql_service_reply, std::exception_ptr>;

class mysql_service
{
    using queue_type = completion_queue<mysql_service_result>;
    using control_type = database_request_control;
    struct operation;

    struct request_registry
    {
        std::mutex mutex;
        std::unordered_map<std::uint64_t, std::weak_ptr<operation>> active;
        std::uint64_t next_id{1};
        database_service_diagnostics diagnostics;
    };

    struct operation : std::enable_shared_from_this<operation>
    {
        control_type control;
        std::weak_ptr<request_registry> registry;
        std::uint64_t id{};
        std::shared_ptr<arknet::mysql::pool> pool;
        std::shared_ptr<queue_type> queue;
        queue_type::ticket ticket;
        std::chrono::steady_clock::time_point deadline;
        arknet::mysql::query_options limits;
        std::string sql;
        std::vector<arknet::mysql::field_value> parameters;
        arknet::mysql::pool::lease lease;
        arknet::mysql::prepared_statement statement;
        mysql_service_reply reply;
        bool prepared{};

        void forward_cancel(std::optional<control_type::cancellation> cancellation) {
            if (cancellation)
            {
                try
                {
                    if (cancellation->target == control_type::stage::acquire)
                    {
                        pool->cancel(cancellation->id);
                    }
                    else
                    {
                        lease.cancel(cancellation->id);
                    }
                }
                catch (...)
                {
                    // A failed cancellation post cannot turn an accepted write
                    // into a known failure. Its real completion/deadline remains.
                    if (auto requests = registry.lock())
                    {
                        std::lock_guard lock(requests->mutex);
                        requests->diagnostics.cancellation_submission = std::current_exception();
                    }
                }
            }
        }

        bool cancel() {
            const auto requested = control.cancel();
            forward_cancel(requested.native);
            return requested.active;
        }

        void erase_request() {
            if (auto requests = registry.lock())
            {
                std::lock_guard lock(requests->mutex);
                requests->active.erase(id);
            }
        }

        void return_lease(bool discard = false) {
            try
            {
                if (discard)
                {
                    lease.discard();
                }
                else
                {
                    lease.release();
                }
            }
            catch (...)
            {
                if (auto requests = registry.lock())
                {
                    std::lock_guard lock(requests->mutex);
                    requests->diagnostics.lease_return = std::current_exception();
                }
            }
        }

        std::optional<arknet::mysql::query_options> remaining_options() const {
            const auto remaining =
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
            if (remaining <= std::chrono::milliseconds::zero())
            {
                return std::nullopt;
            }
            auto options = limits;
            options.timeout = remaining;
            return options;
        }

        void finish() {
            if (!control.finish())
            {
                return;
            }
            return_lease();
            queue->complete(ticket, std::move(reply));
            erase_request();
        }

        void fail(std::exception_ptr failure) {
            if (!control.finish())
            {
                return;
            }
            return_lease(true);
            queue->complete(ticket, std::move(failure));
            erase_request();
        }

        void close_statement() {
            const auto options = remaining_options();
            if (!options || !lease)
            {
                arknet::mysql::connection_result cleanup;
                cleanup.code =
                    options ? arknet::mysql::connection_errc::not_connected : arknet::mysql::connection_errc::timeout;
                reply.statement_close.emplace(std::move(cleanup));
                return_lease(true);
                finish();
                return;
            }
            try
            {
                const auto submission = control.begin(control_type::stage::command);
                const auto native_id = lease.async_close(
                    statement,
                    [self = shared_from_this(), submission](arknet::mysql::connection_result result) mutable {
                        if (!self->control.complete(submission))
                        {
                            return;
                        }
                        self->reply.statement_close.emplace(std::move(result));
                        if (!bool(*self->reply.statement_close))
                        {
                            self->return_lease(true);
                        }
                        self->finish();
                    },
                    *options);
                forward_cancel(control.publish(submission, native_id));
            }
            catch (...)
            {
                reply.statement_close_exception = std::current_exception();
                return_lease(true);
                finish();
            }
        }

        void execute() {
            if (control.cancelled())
            {
                reply.result.status.code = arknet::mysql::connection_errc::cancelled;
                close_statement();
                return;
            }
            const auto options = remaining_options();
            if (!options)
            {
                reply.result.status.code = arknet::mysql::connection_errc::timeout;
                close_statement();
                return;
            }
            const auto submission = control.begin(control_type::stage::command);
            const auto native_id = lease.async_execute(
                statement, std::move(parameters),
                [self = shared_from_this(), submission](arknet::mysql::query_result result) mutable {
                    if (!self->control.complete(submission))
                    {
                        return;
                    }
                    self->reply.result = std::move(result);
                    self->close_statement();
                },
                *options);
            forward_cancel(control.publish(submission, native_id));
        }

        void acquired(arknet::mysql::pool::acquire_result result) {
            if (!result)
            {
                reply.result.status = std::move(result.status);
                // A successful acquisition status is insufficient if the
                // lease became unavailable before this callback used it.
                if (reply.result.status.code == arknet::mysql::connection_errc::none)
                {
                    reply.result.status.code = arknet::mysql::connection_errc::not_connected;
                }
                finish();
                return;
            }
            lease = std::move(result.connection);
            if (control.cancelled())
            {
                reply.result.status.code = arknet::mysql::connection_errc::cancelled;
                finish();
                return;
            }
            const auto options = remaining_options();
            if (!options)
            {
                reply.result.status.code = arknet::mysql::connection_errc::timeout;
                finish();
                return;
            }
            if (prepared)
            {
                const auto submission = control.begin(control_type::stage::command);
                const auto native_id = lease.async_prepare(
                    std::move(sql),
                    [self = shared_from_this(), submission](arknet::mysql::prepare_result result) mutable {
                        if (!self->control.complete(submission))
                        {
                            return;
                        }
                        if (!result)
                        {
                            self->reply.result.status = std::move(result.status);
                            self->finish();
                            return;
                        }
                        self->statement = std::move(result.statement);
                        try
                        { self->execute(); }
                        catch (...)
                        { self->fail(std::current_exception()); }
                    },
                    *options);
                forward_cancel(control.publish(submission, native_id));
            }
            else
            {
                const auto submission = control.begin(control_type::stage::command);
                const auto native_id = lease.async_query(
                    std::move(sql),
                    [self = shared_from_this(), submission](arknet::mysql::query_result result) mutable {
                        if (!self->control.complete(submission))
                        {
                            return;
                        }
                        self->reply.result = std::move(result);
                        self->finish();
                    },
                    *options);
                forward_cancel(control.publish(submission, native_id));
            }
        }
    };

public:
    using callback = std::function<void(mysql_service_result)>;
    using request_id = std::uint64_t;

    mysql_service(arknet::mysql::pool_options options, std::size_t max_operations = 64,
        std::size_t max_input_bytes = 1024 * 1024, arknet::mysql::query_options limits = {})
        : completions_(std::make_shared<queue_type>(max_operations, max_input_bytes))
        , limits_(limits)
        , pool_(std::make_shared<arknet::mysql::pool>(std::move(options))) {
    }

    ~mysql_service() {
        stop();
    }

    mysql_service(const mysql_service&) = delete;
    mysql_service& operator=(const mysql_service&) = delete;

    // false means not admitted and no callback. One deadline includes acquiring
    // the lease and every command; no stage gets a fresh timeout or write retry.
    bool submit_query(std::string sql, callback completion, std::chrono::milliseconds timeout) {
        return submit(std::move(sql), {}, std::move(completion), timeout, false) != 0;
    }

    bool submit_execute(std::string sql, std::vector<arknet::mysql::field_value> parameters, callback completion,
        std::chrono::milliseconds timeout) {
        return submit(std::move(sql), std::move(parameters), std::move(completion), timeout, true) != 0;
    }

    // Unlike legacy submit(), start() also completes rejected work, possibly inline.
    // Zero means no native operation was admitted and there is nothing to cancel.
    request_id start_query(std::string sql, callback completion, std::chrono::milliseconds timeout) {
        return start(std::move(sql), {}, std::move(completion), timeout, false);
    }

    request_id start_execute(std::string sql, std::vector<arknet::mysql::field_value> parameters, callback completion,
        std::chrono::milliseconds timeout) {
        return start(std::move(sql), std::move(parameters), std::move(completion), timeout, true);
    }

    bool cancel(request_id id) {
        check_owner();
        std::shared_ptr<operation> pending;
        {
            std::lock_guard lock(requests_->mutex);
            const auto found = requests_->active.find(id);
            if (found != requests_->active.end())
            {
                pending = found->second.lock();
            }
        }
        return pending && pending->cancel();
    }

    // Logical terminal observation only; queued results still retain quota.
    // This does not observe transport cleanup or a worker/DSO drain barrier.
    bool is_pending(request_id id) const {
        check_owner();
        std::lock_guard lock(requests_->mutex);
        const auto found = requests_->active.find(id);
        return found != requests_->active.end() && !found->second.expired();
    }

    database_service_diagnostics diagnostics() const {
        check_owner();
        std::lock_guard lock(requests_->mutex);
        return requests_->diagnostics;
    }

    void stop_accepting() {
        check_owner();
        accepting_ = false;
        completions_->stop_accepting();
    }

    void cancel_all() {
        check_owner();
        std::vector<std::shared_ptr<operation>> pending;
        {
            std::lock_guard lock(requests_->mutex);
            pending.reserve(requests_->active.size());
            for (const auto& [id, weak] : requests_->active)
            {
                if (auto request = weak.lock())
                {
                    pending.push_back(std::move(request));
                }
            }
        }
        for (const auto& request : pending)
        {
            request->cancel();
        }
    }

    std::size_t update(std::size_t maximum = 64) {
        check_owner();
        database_service_activity::guard active(activity_);
        // The business completion may destroy the service itself.
        auto queue = completions_;
        return queue->dispatch(maximum);
    }

    // The transaction coordinator borrows this pool. Only its owning service
    // retires it, after both coordinators have drained.
    std::shared_ptr<arknet::mysql::pool> borrow_pool() const {
        check_owner();
        return pool_;
    }

    bool quiescent() const {
        check_owner();
        std::lock_guard lock(requests_->mutex);
        return activity_->depth == 0 && requests_->active.empty() && completions_->quiescent();
    }

    // Seal admission only after accepted workflows finish their later requests.
    // This covers this service's logical activity, not native IO retirement.
    bool logically_drained() const {
        check_owner();
        return !accepting_ && quiescent();
    }

    // Call from the host retirement phase after coroutine/update stacks return,
    // before unloading plugin code. No external runtime or bridge is inspected.
    // False means not retired; native false/exception preserves this pool owner.
    bool retire_native() {
        return retire_native([](auto& pool) { return pool.close_and_wait(); });
    }

    bool native_retired() const {
        check_owner();
        return activity_->native_retired;
    }

    // Only suppresses business callbacks and requests native close. NOT drain,
    // a join barrier, or permission to unload a dynamic library.
    void stop() {
        check_owner();
        database_service_activity::guard active(activity_);
        // Destroying a business callback capture can destroy this service.
        // Pin the queue and access no members after dropping those captures.
        auto queue = completions_;
        accepting_ = false;
        pool_->close();
        queue->stop();
    }

private:
    friend struct database_service_test_access;

    template<class finalizer_type>
    bool retire_native(finalizer_type finalizer) {
        check_owner();
        if (!logically_drained())
        {
            return false;
        }
        auto activity = activity_;
        auto pool = pool_;
        if (activity->native_retired)
        {
            return true;
        }
        database_service_activity::guard active(activity);
        if (!finalizer(*pool))
        {
            return false;
        }
        activity->native_retired = true;
        return true;
    }

    void check_owner() const {
        if (std::this_thread::get_id() != owner_)
        {
            throw std::logic_error("Database service must be used on its module thread");
        }
    }

    request_id start(std::string sql, std::vector<arknet::mysql::field_value> parameters, callback incoming,
        std::chrono::milliseconds timeout, bool prepared) {
        check_owner();
        database_service_activity::guard active(activity_);
        auto completion = std::move(incoming);
        auto handler = std::make_shared<callback>(std::move(completion));
        mysql_service_result rejected;
        request_id id{};
        if (!arknet::detail::checked_deadline(std::chrono::steady_clock::now(), timeout))
        {
            std::get<mysql_service_reply>(rejected).result.status.code =
                arknet::mysql::connection_errc::invalid_options;
        }
        else
        {
            try
            {
                id = submit(
                    std::move(sql), std::move(parameters),
                    [handler](mysql_service_result result) {
                        if (*handler)
                        {
                            (*handler)(std::move(result));
                        }
                    },
                    timeout, prepared);
                if (id)
                {
                    return id;
                }
                std::get<mysql_service_reply>(rejected).result.status.code =
                    accepting_ ? arknet::mysql::connection_errc::queue_full
                               : arknet::mysql::connection_errc::not_connected;
            }
            catch (...)
            { rejected = std::current_exception(); }
        }
        if (*handler)
        {
            (*handler)(std::move(rejected));
        }
        return 0;
    }

    request_id submit(std::string sql, std::vector<arknet::mysql::field_value> parameters, callback incoming,
        std::chrono::milliseconds timeout, bool prepared) {
        check_owner();
        database_service_activity::guard active(activity_);
        auto completion = std::move(incoming);
        const auto deadline = arknet::detail::checked_deadline(std::chrono::steady_clock::now(), timeout);
        if (!deadline)
        {
            throw std::invalid_argument("Database operation timeout is not representable");
        }
        std::size_t bytes = sql.capacity();
        if (parameters.capacity() >
            (std::numeric_limits<std::size_t>::max() - bytes) / sizeof(arknet::mysql::field_value))
        {
            return false;
        }
        bytes += parameters.capacity() * sizeof(arknet::mysql::field_value);
        for (const auto& value : parameters)
        {
            if (const auto* text = std::get_if<std::string>(&value))
            {
                if (text->capacity() > std::numeric_limits<std::size_t>::max() - bytes)
                {
                    return false;
                }
                bytes += text->capacity();
            }
        }
        auto ticket = completions_->admit(bytes, std::move(completion));
        if (!ticket)
        {
            return 0;
        }
        auto queue = completions_;
        auto requests = requests_;
        std::shared_ptr<operation> pending;
        try
        {
            pending = std::make_shared<operation>();
            pending->queue = queue;
            pending->registry = requests;
            pending->pool = pool_;
            pending->ticket = ticket;
            pending->deadline = *deadline;
            pending->limits = limits_;
            pending->sql = std::move(sql);
            pending->parameters = std::move(parameters);
            pending->prepared = prepared;
            {
                std::lock_guard lock(requests->mutex);
                if (!requests->next_id)
                {
                    throw std::overflow_error("Database request IDs exhausted");
                }
                pending->id = requests->next_id++;
                requests->active.emplace(pending->id, pending);
            }
            const auto options = pending->remaining_options();
            if (!options)
            {
                pending->reply.result.status.code = arknet::mysql::connection_errc::timeout;
                pending->finish();
                return pending->id;
            }
            const auto submission = pending->control.begin(control_type::stage::acquire);
            const auto native_id = pending->pool->async_acquire(
                [pending, submission](arknet::mysql::pool::acquire_result acquired) mutable {
                    if (!pending->control.complete(submission))
                    {
                        return;
                    }
                    try
                    { pending->acquired(std::move(acquired)); }
                    catch (...)
                    { pending->fail(std::current_exception()); }
                },
                options->timeout);
            pending->forward_cancel(pending->control.publish(submission, native_id));
        }
        catch (...)
        {
            if (pending)
            {
                pending->control.finish();
                pending->erase_request();
            }
            queue->abandon(ticket);
            throw;
        }
        return pending->id;
    }

    std::shared_ptr<queue_type> completions_;
    std::shared_ptr<request_registry> requests_{std::make_shared<request_registry>()};
    std::shared_ptr<database_service_activity> activity_{std::make_shared<database_service_activity>()};
    const arknet::mysql::query_options limits_;
    const std::thread::id owner_{std::this_thread::get_id()};
    bool accepting_{true};
    // Allocate all service bookkeeping before starting the native IO worker.
    // No later member construction may unwind this pool under allocation failure.
    std::shared_ptr<arknet::mysql::pool> pool_;
};

} // namespace ark::detail
