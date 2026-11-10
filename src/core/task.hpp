// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>

#include <arknet/base/manual_runtime.hpp>

namespace ark {

template<class result_type = void>
using task = asio::awaitable<result_type>;

// The constructing thread owns admission, cancellation and polling.
// close() cancels accepted tasks but never drives their execution. Keep pumping
// native service completions and this group until closed() before destroying
// the group or the business state referenced by its tasks. Destruction with
// pending tasks (or on a different thread) terminates; it does not detach tasks,
// block, poll recursively, or destroy suspended coroutine frames.
class task_group {
    struct task_record {
        asio::cancellation_signal cancellation;
        std::function<void(std::exception_ptr)> completion;
        bool cancel_requested{};
    };

    struct state {
        std::shared_ptr<arknet::manual_runtime> runtime;
        const std::thread::id owner = std::this_thread::get_id();
        std::unordered_map<std::uint64_t, std::shared_ptr<task_record>> tasks;
        std::uint64_t next_id{1};
        std::size_t maximum;
        bool accepting{true};

        state(std::shared_ptr<arknet::manual_runtime> execution, std::size_t limit)
            : runtime(std::move(execution))
            , maximum(limit) {
            if (!runtime || !maximum) {
                throw std::invalid_argument("Module task group requires a runtime and positive task limit");
            }
        }

        void check_owner() const {
            if (owner != std::this_thread::get_id()) {
                throw std::logic_error("Module task group requires its constructing thread");
            }
        }
    };

public:
    using task_id = std::uint64_t;
    using completion_handler = std::function<void(std::exception_ptr)>;

    explicit task_group(std::shared_ptr<arknet::manual_runtime> runtime, std::size_t maximum = 64)
        : state_(std::make_shared<state>(std::move(runtime), maximum)) {
    }

    task_group(const task_group&) = delete;
    task_group& operator=(const task_group&) = delete;
    task_group(task_group&&) = delete;
    task_group& operator=(task_group&&) = delete;

    ~task_group() {
        if (state_->owner != std::this_thread::get_id() || !state_->tasks.empty()) {
            std::terminate();
        }
    }

    // Zero means admission was closed/full. The lazy, unstarted task is then
    // destroyed normally by Asio; no native operation has been initiated.
    task_id start(task<> task, completion_handler completion = {}) {
        auto shared = state_;
        shared->check_owner();
        if (!shared->accepting || shared->tasks.size() == shared->maximum) {
            return 0;
        }
        if (shared->next_id == std::numeric_limits<task_id>::max()) {
            throw std::overflow_error("Module task identifiers exhausted");
        }
        auto record = std::make_shared<task_record>();
        record->completion = std::move(completion);
        const auto id = shared->next_id++;
        shared->tasks.emplace(id, record);
        try {
            asio::co_spawn(shared->runtime->get_executor(), std::move(task),
                asio::bind_cancellation_slot(
                    record->cancellation.slot(), [shared, record, id](std::exception_ptr error) mutable {
                        shared->check_owner();
                        // Retire membership before business code: the last
                        // completion may destroy the public group facade.
                        shared->tasks.erase(id);
                        auto handler = std::move(record->completion);
                        if (handler) {
                            handler(std::move(error));
                        }
                    }));
        } catch (...) {
            shared->tasks.erase(id);
            throw;
        }
        return id;
    }

    bool cancel(task_id id) {
        auto shared = state_;
        shared->check_owner();
        const auto found = shared->tasks.find(id);
        if (found == shared->tasks.end()) {
            return false;
        }
        auto record = found->second;
        if (!record->cancel_requested) {
            record->cancel_requested = true;
            record->cancellation.emit(asio::cancellation_type::terminal);
        }
        return true;
    }

    void cancel_all() {
        auto shared = state_;
        shared->check_owner();
        // Emission may synchronously execute an operation's cancellation hook.
        // Do not retain an iterator across that external code.
        while (true) {
            std::shared_ptr<task_record> next;
            for (const auto& [id, record] : shared->tasks) {
                if (!record->cancel_requested) {
                    next = record;
                    break;
                }
            }
            if (!next) {
                return;
            }
            next->cancel_requested = true;
            next->cancellation.emit(asio::cancellation_type::terminal);
        }
    }

    // Stop top-level admission without cancelling or polling accepted tasks.
    void stop_accepting() {
        auto shared = state_;
        shared->check_owner();
        shared->accepting = false;
    }

    void close() {
        auto shared = state_;
        shared->check_owner();
        shared->accepting = false;
        cancel_all();
    }

    std::size_t poll(std::size_t maximum = 64) {
        auto shared = state_;
        shared->check_owner();
        return shared->runtime->poll(maximum);
    }

    std::size_t pending() const {
        state_->check_owner();
        return state_->tasks.size();
    }

    bool closed() const {
        state_->check_owner();
        return !state_->accepting && state_->tasks.empty();
    }

private:
    std::shared_ptr<state> state_;
};

} // namespace ark
