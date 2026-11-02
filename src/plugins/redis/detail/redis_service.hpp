// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

#include <chrono>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <arknet/base/detail/deadline.hpp>
#include <arknet/redis/pool.hpp>

#include "detail/database_completion_queue.hpp"
#include "detail/database_request_control.hpp"
#include "detail/database_service_activity.hpp"

namespace ark::detail {

// Private integration envelope; the public module API is not exposed here.
using redis_service_result = std::variant<arknet::redis::result, std::exception_ptr>;

template<class acquire_type = arknet::redis::pool::acquire_result>
inline arknet::redis::result redis_acquire_failure(acquire_type acquired) {
    const auto error =
        acquired.error == arknet::redis::client_errc::none ? arknet::redis::client_errc::not_connected : acquired.error;
    return {error, acquired.transport_error, std::move(acquired.replies), false};
}

template<class pool_type>
class basic_redis_service
{
    using queue_type = completion_queue<redis_service_result>;
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
        std::shared_ptr<pool_type> pool;
        std::shared_ptr<queue_type> queue;
        queue_type::ticket ticket;
        std::chrono::steady_clock::time_point deadline;
        std::vector<arknet::redis::command> commands;
        typename pool_type::lease lease;

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
                    // Keep the actual command alive after a failed cancel post;
                    // only its native reply/deadline can establish the outcome.
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

        void finish(redis_service_result result) {
            if (!control.finish())
            {
                return;
            }
            return_lease();
            queue->complete(ticket, std::move(result));
            erase_request();
        }

        void acquired(typename pool_type::acquire_result result) {
            if (!result)
            {
                finish(redis_acquire_failure(std::move(result)));
                return;
            }
            lease = std::move(result.connection);
            if (control.cancelled())
            {
                finish(arknet::redis::result{arknet::redis::client_errc::cancelled, {}, {}, false});
                return;
            }
            const auto remaining =
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
            if (remaining <= std::chrono::milliseconds::zero())
            {
                finish(arknet::redis::result{arknet::redis::client_errc::timeout, {}, {}, false});
                return;
            }
            const auto submission = control.begin(control_type::stage::command);
            const auto native_id = lease.async_pipeline(
                std::move(commands),
                [self = this->shared_from_this(), submission](arknet::redis::result response) mutable {
                    if (self->control.complete(submission))
                    {
                        self->finish(std::move(response));
                    }
                },
                remaining);
            forward_cancel(control.publish(submission, native_id));
        }
    };

public:
    using callback = std::function<void(redis_service_result)>;
    using request_id = std::uint64_t;

    basic_redis_service(
        arknet::redis::pool_options options, std::size_t max_operations = 64, std::size_t max_input_bytes = 1024 * 1024)
        : completions_(std::make_shared<queue_type>(max_operations, max_input_bytes))
        , pool_(std::make_shared<pool_type>(std::move(options))) {
    }

    ~basic_redis_service() {
        // The service is created, updated and destroyed by its module thread.
        stop();
    }

    basic_redis_service(const basic_redis_service&) = delete;
    basic_redis_service& operator=(const basic_redis_service&) = delete;

    // false means not accepted: no callback. Accepted callbacks run in update()
    // unless stop() suppresses them. timeout covers both acquire and execution.
    bool submit(arknet::redis::command command, callback incoming, std::chrono::milliseconds timeout) {
        check_owner();
        database_service_activity::guard active(activity_);
        auto completion = std::move(incoming);
        std::vector<arknet::redis::command> commands;
        commands.push_back(std::move(command));
        return submit_pipeline(std::move(commands), std::move(completion), timeout);
    }

    bool submit_pipeline(
        std::vector<arknet::redis::command> commands, callback completion, std::chrono::milliseconds timeout) {
        return initiate(std::move(commands), std::move(completion), timeout) != 0;
    }

    // Rejected new-style requests complete inline with ID0; legacy submit keeps
    // its no-callback-on-rejection rule.
    request_id start(arknet::redis::command command, callback incoming, std::chrono::milliseconds timeout) {
        check_owner();
        database_service_activity::guard active(activity_);
        auto completion = std::move(incoming);
        std::vector<arknet::redis::command> commands;
        commands.push_back(std::move(command));
        return start_pipeline(std::move(commands), std::move(completion), timeout);
    }

    request_id start_pipeline(
        std::vector<arknet::redis::command> commands, callback incoming, std::chrono::milliseconds timeout) {
        check_owner();
        database_service_activity::guard active(activity_);
        auto completion = std::move(incoming);
        auto handler = std::make_shared<callback>(std::move(completion));
        redis_service_result rejected;
        if (!arknet::detail::checked_deadline(std::chrono::steady_clock::now(), timeout))
        {
            std::get<arknet::redis::result>(rejected).error = arknet::redis::client_errc::invalid_command;
        }
        else
        {
            try
            {
                const auto id = initiate(
                    std::move(commands),
                    [handler](redis_service_result result) {
                        if (*handler)
                        {
                            (*handler)(std::move(result));
                        }
                    },
                    timeout);
                if (id)
                {
                    return id;
                }
                std::get<arknet::redis::result>(rejected).error =
                    accepting_ ? arknet::redis::client_errc::queue_full : arknet::redis::client_errc::closed;
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

    // Native logical completion may be queued without owner dispatch. Its
    // storage remains charged; false does not mean transport/worker drain.
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
        // A business callback may destroy its service while dispatch is active.
        auto completions = completions_;
        return completions->dispatch(maximum);
    }

    // Observation only: accepted workflows may still submit follow-up requests.
    bool quiescent() const {
        check_owner();
        std::lock_guard lock(requests_->mutex);
        return activity_->depth == 0 && requests_->active.empty() && completions_->quiescent();
    }

    // Seal admission after accepted workflows finish their later requests.
    // This covers this service's logical activity, not native IO retirement.
    bool logically_drained() const {
        check_owner();
        return !accepting_ && quiescent();
    }

    // Caller must return from its coroutine/lifecycle stacks to an outer owner
    // frame first. No external runtime or bridge is owned or inspected here.
    // False means not retired; native false/exception preserves this pool owner.
    bool retire_native() {
        return retire_native([](auto& pool) { return pool.close_and_wait(); });
    }

    bool native_retired() const {
        check_owner();
        return activity_->native_retired;
    }

    // Legacy capture suppression; this does not drain native work or continuations.
    void stop() {
        check_owner();
        database_service_activity::guard active(activity_);
        auto completions = completions_;
        accepting_ = false;
        pool_->close();
        // Releasing business captures may destroy this service. Keep the queue
        // alive and do not access service members after callbacks are removed.
        completions->stop();
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

    request_id initiate(
        std::vector<arknet::redis::command> commands, callback incoming, std::chrono::milliseconds timeout) {
        check_owner();
        database_service_activity::guard active(activity_);
        auto completion = std::move(incoming);
        const auto deadline = arknet::detail::checked_deadline(std::chrono::steady_clock::now(), timeout);
        if (!deadline)
        {
            throw std::invalid_argument("Database operation timeout is not representable");
        }
        std::size_t bytes{};
        for (const auto& command : commands)
        {
            if (command.capacity() > (std::numeric_limits<std::size_t>::max() - bytes) / sizeof(std::string))
            {
                return false;
            }
            bytes += command.capacity() * sizeof(std::string);
            for (const auto& argument : command)
            {
                if (argument.capacity() > std::numeric_limits<std::size_t>::max() - bytes)
                {
                    return false;
                }
                bytes += argument.capacity();
            }
        }
        if (commands.capacity() > (std::numeric_limits<std::size_t>::max() - bytes) / sizeof(arknet::redis::command))
        {
            return false;
        }
        bytes += commands.capacity() * sizeof(arknet::redis::command);
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
            pending->ticket = ticket;
            pending->registry = requests;
            pending->pool = pool_;
            pending->deadline = *deadline;
            pending->commands = std::move(commands);
            {
                std::lock_guard lock(requests->mutex);
                if (!requests->next_id)
                {
                    throw std::overflow_error("Database request IDs exhausted");
                }
                pending->id = requests->next_id++;
                requests->active.emplace(pending->id, pending);
            }
            const auto acquire_remaining =
                std::chrono::duration_cast<std::chrono::milliseconds>(*deadline - std::chrono::steady_clock::now());
            if (acquire_remaining <= std::chrono::milliseconds::zero())
            {
                pending->finish(arknet::redis::result{arknet::redis::client_errc::timeout, {}, {}, false});
                return pending->id;
            }
            const auto submission = pending->control.begin(control_type::stage::acquire);
            const auto native_id = pending->pool->async_acquire(
                [pending, submission](typename pool_type::acquire_result acquired) mutable {
                    if (!pending->control.complete(submission))
                    {
                        return;
                    }
                    try
                    { pending->acquired(std::move(acquired)); }
                    catch (...)
                    {
                        pending->return_lease(true);
                        pending->finish(std::current_exception());
                    }
                },
                acquire_remaining);
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
    const std::thread::id owner_{std::this_thread::get_id()};
    bool accepting_{true};
    // Allocate all service bookkeeping before starting the native IO worker.
    // No later member construction may unwind this pool under allocation failure.
    std::shared_ptr<pool_type> pool_;
};

using redis_service = basic_redis_service<arknet::redis::pool>;
#if defined(ARKNET_ENABLE_SSL)
using tls_redis_service = basic_redis_service<arknet::redis::tls_pool>;
#endif

} // namespace ark::detail
