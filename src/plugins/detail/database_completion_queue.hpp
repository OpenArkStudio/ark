// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace ark::detail {

// Private completion storage reserved before an operation is submitted.
// Public methods except complete() belong to the constructing/module thread.
template<class result_type>
class completion_queue {
    static_assert(std::is_nothrow_move_constructible_v<result_type>);

public:
    using callback = std::function<void(result_type)>;

    struct record {
        std::size_t index{};
        std::size_t input_bytes{};
        callback completion;
        std::optional<result_type> response;
        std::shared_ptr<record> next;
        bool ready{};
    };

    using ticket = std::shared_ptr<record>;

    completion_queue(std::size_t max_operations, std::size_t max_input_bytes)
        : owner_(std::this_thread::get_id())
        , slots_(max_operations)
        , max_input_bytes_(max_input_bytes) {
        if (!max_operations || !max_input_bytes) {
            throw std::invalid_argument("Database completion limits must be positive");
        }
    }

    ticket admit(std::size_t input_bytes, callback handler) {
        check_owner();
        auto operation = std::make_shared<record>();
        operation->input_bytes = input_bytes;
        operation->completion = std::move(handler);
        std::lock_guard lock(mutex_);
        if (!accepting_ || input_bytes > max_input_bytes_ - retained_input_bytes_) {
            return {};
        }
        for (std::size_t index = 0; index < slots_.size(); ++index) {
            if (!slots_[index]) {
                operation->index = index;
                slots_[index] = operation;
                retained_input_bytes_ += input_bytes;
                return operation;
            }
        }
        return {};
    }

    // The response is moved into pre-reserved storage; no queue-node allocation.
    void complete(const ticket& operation, result_type response) {
        std::lock_guard lock(mutex_);
        if (!dispatch_enabled_ || !contains(operation) || operation->ready) {
            return;
        }
        operation->response.emplace(std::move(response));
        operation->ready = true;
        if (tail_) {
            tail_->next = operation;
        } else {
            head_ = operation;
        }
        tail_ = operation;
    }

    // Called only if initiating the native operation threw before acceptance.
    void abandon(const ticket& operation) {
        check_owner();
        {
            std::lock_guard lock(mutex_);
            if (contains(operation)) {
                retained_input_bytes_ -= operation->input_bytes;
                slots_[operation->index].reset();
            }
        }
        operation->completion = {};
    }

    std::size_t dispatch(std::size_t maximum = 64) {
        check_owner();

        struct dispatch_guard {
            std::size_t& depth;

            explicit dispatch_guard(std::size_t& active) noexcept
                : depth(active) {
                ++depth;
            }

            ~dispatch_guard() {
                --depth;
            }
        } guard(dispatch_depth_);

        std::size_t dispatched{};
        while (dispatched < maximum) {
            ticket operation;
            {
                std::lock_guard lock(mutex_);
                if (!dispatch_enabled_ || !head_) {
                    break;
                }
                operation = std::move(head_);
                head_ = std::move(operation->next);
                if (!head_) {
                    tail_.reset();
                }
            }
            auto handler = std::move(operation->completion);
            try {
                if (handler) {
                    handler(std::move(*operation->response));
                }
            } catch (...) {
                handler = {};
                operation->response.reset();
                finish(operation);
                throw;
            }
            handler = {};
            operation->response.reset();
            finish(operation);
            ++dispatched;
        }
        return dispatched;
    }

    // Logical owner-thread observation, not native IO retirement. A reentrant
    // stop can remove every reservation while dispatch still owns user captures.
    bool quiescent() const {
        check_owner();
        std::lock_guard lock(mutex_);
        for (const auto& operation : slots_) {
            if (operation) {
                return false;
            }
        }
        return dispatch_depth_ == 0;
    }

    // Stops admission while preserving delivery of every accepted completion.
    void stop_accepting() {
        check_owner();
        std::lock_guard lock(mutex_);
        accepting_ = false;
    }

    // Suppresses not-yet-started business callbacks. This is NOT IO drain/join.
    void stop() {
        check_owner();
        std::vector<ticket> retired;
        {
            std::lock_guard lock(mutex_);
            accepting_ = false;
            dispatch_enabled_ = false;
            retired.swap(slots_);
            head_.reset();
            tail_.reset();
            retained_input_bytes_ = 0;
        }
        for (auto& operation : retired) {
            if (operation) {
                operation->completion = {};
                operation->next.reset();
            }
        }
    }

private:
    void check_owner() const {
        if (std::this_thread::get_id() != owner_) {
            throw std::logic_error("Database service must be used on its module thread");
        }
    }

    bool contains(const ticket& operation) const {
        return operation && operation->index < slots_.size() && slots_[operation->index] == operation;
    }

    void finish(const ticket& operation) {
        std::lock_guard lock(mutex_);
        if (contains(operation)) {
            retained_input_bytes_ -= operation->input_bytes;
            slots_[operation->index].reset();
        }
    }

    const std::thread::id owner_;
    mutable std::mutex mutex_;
    std::vector<ticket> slots_;
    ticket head_;
    ticket tail_;
    const std::size_t max_input_bytes_;
    std::size_t retained_input_bytes_{};
    std::size_t dispatch_depth_{};
    bool accepting_{true};
    bool dispatch_enabled_{true};
};

} // namespace ark::detail
