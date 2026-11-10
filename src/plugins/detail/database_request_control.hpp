// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

#include <cstdint>
#include <exception>
#include <mutex>
#include <optional>

namespace ark::detail {

// At most two retained diagnostics per private service. Neither overrides an
// execution result nor claims that native resource cleanup has completed.
struct database_service_diagnostics {
    std::exception_ptr cancellation_submission;
    std::exception_ptr lease_return;
};

// Native completion is allowed to run inline, before its request ID is returned.
// Only state is protected here: callers invoke native methods after unlocking.
class database_request_control {
public:
    enum class stage {
        idle,
        acquire,
        command,
        finished
    };

    struct submission {
        std::uint64_t epoch{};
        stage target{stage::idle};
    };

    struct cancellation {
        stage target;
        std::uint64_t id;
    };

    struct cancel_result {
        bool active{};
        std::optional<cancellation> native;
    };

    submission begin(stage target) {
        std::lock_guard lock(mutex_);
        if (target_ == stage::finished) {
            return {epoch_, target_};
        }
        target_ = target;
        id_ = 0;
        return {++epoch_, target};
    }

    std::optional<cancellation> publish(submission operation, std::uint64_t id) {
        std::lock_guard lock(mutex_);
        if (!matches(operation)) {
            return {};
        }
        id_ = id;
        return cancellation_locked();
    }

    bool complete(submission operation) {
        std::lock_guard lock(mutex_);
        if (!matches(operation)) {
            return false;
        }
        target_ = stage::idle;
        id_ = 0;
        ++epoch_;
        return true;
    }

    cancel_result cancel() {
        std::lock_guard lock(mutex_);
        if (target_ == stage::finished) {
            return {};
        }
        cancelled_ = true;
        return {true, cancellation_locked()};
    }

    bool cancelled() const {
        std::lock_guard lock(mutex_);
        return cancelled_;
    }

    bool finish() {
        std::lock_guard lock(mutex_);
        if (target_ == stage::finished) {
            return false;
        }
        target_ = stage::finished;
        id_ = 0;
        ++epoch_;
        return true;
    }

private:
    bool matches(submission operation) const {
        return target_ == operation.target && epoch_ == operation.epoch && target_ != stage::finished;
    }

    std::optional<cancellation> cancellation_locked() const {
        if (cancelled_ && id_ && (target_ == stage::acquire || target_ == stage::command)) {
            return cancellation{target_, id_};
        }
        return {};
    }

    mutable std::mutex mutex_;
    stage target_{stage::idle};
    std::uint64_t epoch_{};
    std::uint64_t id_{};
    bool cancelled_{};
};

} // namespace ark::detail
