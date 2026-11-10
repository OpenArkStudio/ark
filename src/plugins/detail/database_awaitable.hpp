// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "detail/module_task_group.hpp"

namespace ark::detail {

struct database_awaitable_test_access;

inline bool is_database_executor(const asio::any_io_executor& candidate, arknet::manual_runtime& runtime) {
    // Type-erasure function tables can have different addresses across DSOs.
    // Compare the exact concrete executor, preserving rejection of strands and
    // other wrappers even when they use the same execution context.
    const auto* executor = candidate.target<arknet::manual_runtime::executor_type>();
    return executor && *executor == runtime.get_executor();
}

// Private bridge for owner-thread service callbacks. Native results keep their
// original type; local C++ failures travel through exception_ptr. Every accepted
// operation reserves one record until its result is actually delivered.
//
// Owner update order: service.update(), bridge.dispatch(), task_group.poll().
// dispatch() schedules one bounded drain on this exact manual executor. It does
// not resume a task directly from a service callback. If scheduling throws, all
// ready records remain reserved and the owner may retry dispatch().
//
// The bridge and the business objects referenced by initiation/cancel closures
// must outlive accepted operations. Destroy only on the owner after pending()==0.
// stop_accepting() prevents new starts, but never suppresses accepted terminals.
template<class result_type>
class database_awaitable {
    static_assert(std::is_nothrow_move_constructible_v<result_type>);
    static_assert(std::is_nothrow_default_constructible_v<result_type>);

    struct operation {
        std::size_t index{};
        std::uint64_t native_id{};
        bool published{};
        bool cancel_requested{};
        bool cancel_forwarded{};
        bool ready{};
        bool retired{};
        std::exception_ptr exception;
        std::optional<result_type> response;
        std::shared_ptr<operation> next;

        virtual ~operation() = default;
        virtual void deliver(std::exception_ptr error, result_type result) = 0;
        virtual void cancel(std::uint64_t id) = 0;
    };

    template<class handler_type, class cancel_type>
    struct typed_operation final : operation {
        std::optional<handler_type> handler;
        cancel_type cancel_native;
        asio::cancellation_slot slot;

        typed_operation(handler_type completion, cancel_type cancel)
            : handler(std::move(completion))
            , cancel_native(std::move(cancel))
            , slot(asio::get_associated_cancellation_slot(*handler)) {
        }

        void deliver(std::exception_ptr error, result_type result) override {
            // Unlink cancellation before resumption can start another wait and
            // reuse the coroutine's associated cancellation slot.
            slot.clear();
            auto completion = std::move(*handler);
            handler.reset();
            completion(std::move(error), std::move(result));
        }

        void cancel(std::uint64_t id) override {
            cancel_native(id);
        }
    };

    struct state : std::enable_shared_from_this<state> {
        std::shared_ptr<arknet::manual_runtime> runtime;
        const std::thread::id owner = std::this_thread::get_id();
        mutable std::mutex mutex;
        std::vector<std::shared_ptr<operation>> slots;
        std::shared_ptr<operation> head;
        std::shared_ptr<operation> tail;
        std::size_t count{};
        bool accepting{true};
        bool scheduled{};
        std::exception_ptr cancel_exception;

        state(std::shared_ptr<arknet::manual_runtime> execution, std::size_t maximum)
            : runtime(std::move(execution))
            , slots(maximum) {
            if (!runtime || !maximum) {
                throw std::invalid_argument("Database awaitable requires a runtime and positive operation limit");
            }
        }

        void check_owner() const {
            if (std::this_thread::get_id() != owner) {
                throw std::logic_error("Database awaitable requires its constructing thread");
            }
        }

        void admit(const std::shared_ptr<operation>& pending) {
            check_owner();
            std::lock_guard lock(mutex);
            if (!accepting) {
                throw std::logic_error("Database awaitable is not accepting operations");
            }
            for (std::size_t index = 0; index < slots.size(); ++index) {
                if (!slots[index]) {
                    pending->index = index;
                    slots[index] = pending;
                    ++count;
                    return;
                }
            }
            throw std::length_error("Database awaitable operation limit reached");
        }

        void check_admission() const {
            check_owner();
            std::lock_guard lock(mutex);
            if (!accepting) {
                throw std::logic_error("Database awaitable is not accepting operations");
            }
            if (count == slots.size()) {
                throw std::length_error("Database awaitable operation limit reached");
            }
        }

        // May run on a native callback thread. Storage and its queue link were
        // allocated before start; completion itself allocates no delivery node.
        void complete(const std::shared_ptr<operation>& pending, std::exception_ptr error, result_type result) {
            std::lock_guard lock(mutex);
            if (pending->retired || pending->ready) {
                return;
            }
            pending->exception = std::move(error);
            pending->response.emplace(std::move(result));
            pending->ready = true;
            if (tail) {
                tail->next = pending;
            } else {
                head = pending;
            }
            tail = pending;
        }

        void request_cancel(const std::shared_ptr<operation>& pending) {
            check_owner();
            std::uint64_t id{};
            {
                std::lock_guard lock(mutex);
                if (pending->retired || pending->ready) {
                    return;
                }
                pending->cancel_requested = true;
                if (pending->published && pending->native_id && !pending->cancel_forwarded) {
                    id = pending->native_id;
                    pending->cancel_forwarded = true;
                }
            }
            if (id) {
                // Native cancellation is a request, not a synthetic terminal.
                // Keep the record until the native service returns its result.
                forward_cancel(pending, id);
            }
        }

        void forward_cancel(const std::shared_ptr<operation>& pending, std::uint64_t id) noexcept {
            try {
                pending->cancel(id);
            } catch (...) {
                // The request is already accepted. A failed cancellation
                // submission is a separate diagnostic, never its terminal.
                // This state is read/written only by the owner thread.
                if (!cancel_exception) {
                    cancel_exception = std::current_exception();
                }
            }
        }

        void publish(const std::shared_ptr<operation>& pending, std::uint64_t id) {
            check_owner();
            bool cancel{};
            {
                std::lock_guard lock(mutex);
                pending->native_id = id;
                pending->published = true;
                cancel = pending->cancel_requested && !pending->ready && !pending->retired && id;
                if (cancel) {
                    pending->cancel_forwarded = true;
                }
            }
            if (cancel) {
                forward_cancel(pending, id);
            }
        }

        void drain(std::size_t maximum) {
            check_owner();

            struct scheduled_guard {
                state& shared;

                ~scheduled_guard() {
                    std::lock_guard lock(shared.mutex);
                    shared.scheduled = false;
                }
            } guard{*this};

            for (std::size_t completed = 0; completed < maximum; ++completed) {
                std::shared_ptr<operation> pending;
                {
                    std::lock_guard lock(mutex);
                    if (!head) {
                        break;
                    }
                    pending = std::move(head);
                    head = std::move(pending->next);
                    if (!head) {
                        tail.reset();
                    }
                    pending->retired = true;
                    slots[pending->index].reset();
                    --count;
                }
                // Owning result and handler stay in this local record while
                // the quota is free for the resumed task's next operation.
                pending->deliver(std::move(pending->exception), std::move(*pending->response));
                pending->response.reset();
            }
        }

        template<class post_type>
        bool schedule(std::size_t maximum, post_type&& post) {
            check_owner();
            {
                std::lock_guard lock(mutex);
                if (scheduled || !head || !maximum) {
                    return false;
                }
                scheduled = true;
            }
            try {
                post(runtime->get_executor(), [shared = this->shared_from_this(), maximum] { shared->drain(maximum); });
            } catch (...) {
                std::lock_guard lock(mutex);
                scheduled = false;
                throw;
            }
            return true;
        }
    };

public:
    explicit database_awaitable(std::shared_ptr<arknet::manual_runtime> runtime, std::size_t maximum = 64)
        : state_(std::make_shared<state>(std::move(runtime), maximum)) {
    }

    database_awaitable(const database_awaitable&) = delete;
    database_awaitable& operator=(const database_awaitable&) = delete;

    ~database_awaitable() {
        if (state_->owner != std::this_thread::get_id() || pending()) {
            std::terminate();
        }
    }

    // start(callback(exception_ptr, result_type)) returns the logical service
    // request ID; ID zero still requires a terminal callback. It may complete
    // inline, before its ID is returned. cancel(id) forwards a best-effort
    // request and must not invent a completed result or replay a write.
    // start may throw only before native acceptance; after acceptance it must
    // return its ID and deliver a terminal callback, including local failures.
    template<class start_type, class cancel_type>
    module_task<result_type> wait(start_type start, cancel_type cancel) {
        return await_result(state_, std::move(start), std::move(cancel));
    }

    bool dispatch(std::size_t maximum = 64) {
        auto shared = state_;
        return shared->schedule(
            maximum, [](auto executor, auto delivery) { asio::post(executor, std::move(delivery)); });
    }

    void stop_accepting() {
        state_->check_owner();
        std::lock_guard lock(state_->mutex);
        state_->accepting = false;
    }

    std::size_t pending() const {
        state_->check_owner();
        std::lock_guard lock(state_->mutex);
        return state_->count;
    }

    // Quota is released before resumption, so count alone misses the final
    // delivery stack. This is not a native callback or worker retirement barrier.
    bool quiescent() const {
        state_->check_owner();
        std::lock_guard lock(state_->mutex);
        return state_->count == 0 && !state_->scheduled;
    }

    // A cancellation submission exception does not change the native outcome.
    // The first such local failure remains available without an unbounded log.
    std::exception_ptr cancellation_exception() const {
        state_->check_owner();
        return state_->cancel_exception;
    }

private:
    friend struct database_awaitable_test_access;

    template<class start_type, class cancel_type>
    static module_task<result_type> await_result(std::shared_ptr<state> shared, start_type start, cancel_type cancel) {
        const auto executor = co_await asio::this_coro::executor;
        if (!is_database_executor(executor, *shared->runtime)) {
            throw std::logic_error("Database awaitable must run on its manual runtime executor");
        }
        // Reject before use_awaitable transfers ownership of the Asio coroutine
        // thread into an operation handler. No native call has happened yet.
        shared->check_admission();
        auto initiate = [shared, start = std::move(start), cancel = std::move(cancel)](auto handler) mutable {
            using handler_type = decltype(handler);
            static_assert(std::is_nothrow_move_constructible_v<handler_type>);
            static_assert(std::is_nothrow_move_constructible_v<cancel_type>);
            std::shared_ptr<typed_operation<handler_type, cancel_type>> pending;
            std::uint64_t id{};
            try {
                // Allocation precedes the noexcept moves of handler/cancel.
                // If it fails, handler still owns the Asio coroutine thread.
                pending =
                    std::make_shared<typed_operation<handler_type, cancel_type>>(std::move(handler), std::move(cancel));
            } catch (...) {
                // No native operation or delivery record was accepted. We are
                // already on the required manual executor; propagate through
                // Asio rather than dropping its detached coroutine thread.
                handler(std::current_exception(), result_type{});
                return;
            }
            try {
                shared->admit(pending);
            } catch (...) {
                pending->deliver(std::current_exception(), result_type{});
                return;
            }
            try {
                if (pending->slot.is_connected()) {
                    pending->slot.assign(
                        [shared, weak = std::weak_ptr<operation>(pending)](asio::cancellation_type type) {
                            if (type != asio::cancellation_type::none) {
                                if (auto operation = weak.lock()) {
                                    shared->request_cancel(operation);
                                }
                            }
                        });
                }
                id = start([shared, pending](std::exception_ptr error, result_type result) {
                    shared->complete(pending, std::move(error), std::move(result));
                });
            } catch (...) {
                shared->complete(pending, std::current_exception(), result_type{});
                return;
            }
            shared->publish(pending, id);
        };
        auto token = asio::use_awaitable;
        co_return co_await asio::async_initiate<decltype(token), void(std::exception_ptr, result_type)>(
            std::move(initiate), token);
    }

    std::shared_ptr<state> state_;
};

} // namespace ark::detail
