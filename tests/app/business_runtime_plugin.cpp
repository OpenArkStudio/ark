// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <exception>
#include <fstream>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include <ark/plugin_manager.hpp>
#include "detail/database_awaitable.hpp"

namespace {

std::mutex trace_mutex;

void trace(const std::string& event) {
    const std::lock_guard lock(trace_mutex);
    std::ofstream output("business-runtime.trace", std::ios::app);
    output << event << '\n';
}

// A controlled protocol completion, not a real database client or public plugin.
// Late protocol results arrive after cancellation and must not revive the task.
// The native-readiness deadline case has no task dependency: scheduling the
// coroutine chain must not consume its separate 50 ms shutdown deadline.
class probe_module final : public ark::module {
public:
    probe_module(ark::plugin_manager& manager, std::string mode)
        : manager_(manager)
        , mode_(std::move(mode))
        , bridge_(manager.get_task_runtime(), 1) {
    }

    ~probe_module() override {
        trace("destroy");
    }

    const std::string& get_name() const override {
        return name_;
    }

    bool init() override {
        if (mode_.starts_with("boundary_")) {
            // No task, native thread or later poll can delay these hooks.
            return true;
        }
        native_ = std::thread([this] {
            while (!native_release_.load(std::memory_order_acquire)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            trace("native-exit");
            native_exited_.store(true, std::memory_order_release);
        });
        auto& tasks = manager_.create_task_group(*this, 2);
        if (mode_ == "timeout_native") {
            trace("native-pending");
            return true;
        }
        return manager_.get_tasks(*this).start(sequence(this), [this](std::exception_ptr error) {
            trace("complete-begin");
            if (error) {
                try {
                    std::rethrow_exception(error);
                } catch (const arknet::system_error& failure) {
                    if (failure.code() != asio::error::operation_aborted) {
                        throw;
                    }
                    trace("task-cancelled");
                }
            }
            if (manager_.stop(manager_.get_now_time())) {
                throw std::runtime_error("Teardown entered the final completion stack");
            }
            trace("callback-stop-pending");
            completion_returned_ = true;
            trace("complete-end");
        }) != 0;
    }

    bool update() override {
        if (mode_.starts_with("boundary_")) {
            if (cleanup_started_) {
                trace("update-after-cleanup");
            }
            if (!stop_requested_) {
                stop_requested_ = true;
                trace("request-stop");
#if !defined(_WIN32)
                std::raise(SIGTERM);
#endif
            }
            return true;
        }
        const auto state = manager_.get_shutdown_state();
        const auto stopping = state == ark::plugin_manager::shutdown_state::stopping ||
                              state == ark::plugin_manager::shutdown_state::timed_out;
        if (stopping && !stop_observed_) {
            stop_observed_ = true;
            stop_started_ = std::chrono::steady_clock::now();
            auto& tasks = manager_.get_tasks(*this);
            if (tasks.start(rejected_root(), [](std::exception_ptr) {}) != 0) {
                throw std::runtime_error("Shutdown accepted a new root task");
            }
            trace("root-rejected");
            if (mode_ == "update_throw_unknown") {
                trace("update-throw");
                throw 7;
            }
            if (mode_ == "update_throw") {
                trace("update-throw");
                throw std::runtime_error("test drain update exception");
            }
        }
        if ((mode_ == "update_throw" || mode_ == "update_throw_unknown") && stopping && !update_resumed_) {
            update_resumed_ = true;
            trace("update-resumed");
        }
        const auto delayed =
            stopping && std::chrono::steady_clock::now() - stop_started_ < std::chrono::milliseconds(200);
        if ((completion_returned_ || (stopping && mode_ == "timeout_native")) && !owner_stopped_) {
            owner_stopped_ = true;
            trace("owner-stop");
        }
        if (owner_stopped_ && ++native_updates_ > 2 && (mode_ != "timeout_native" || !delayed)) {
            native_release_.store(true, std::memory_order_release);
        }
        if ((pending_ || mode_ == "timeout_native") && !stop_requested_ && !stopping) {
            stop_requested_ = true;
            trace("request-stop");
#if !defined(_WIN32)
            std::raise(SIGTERM);
#endif
        }
        if (pending_ && stopping && mode_ != "timeout_stalled" &&
            ((completion_returned_) || (mode_ == "timeout_task" && !delayed))) {
            trace("late-reply");
            auto complete = std::move(pending_);
            complete({}, "late");
        }
        bridge_.dispatch();
        return true;
    }

    bool ready_to_shut() const override {
        if (mode_.starts_with("boundary_")) {
            trace("ready");
            if (mode_ == "boundary_readiness") {
                expire_shutdown_deadline();
            }
            trace("ready-return");
            return true;
        }
        if (!owner_stopped_ || !native_exited_.load(std::memory_order_acquire)) {
            return false;
        }
        if (mode_ == "ready_throw" || mode_ == "ready_throw_unknown") {
            trace("ready-throw");
            if (mode_ == "ready_throw_unknown") {
                throw 7;
            }
            throw std::runtime_error("test readiness exception");
        }
        if (native_.joinable()) {
            native_.join();
            trace("native-joined");
        }
        return true;
    }

    bool pre_shut() override {
        trace("pre-shut");
        if (mode_.starts_with("boundary_")) {
            cleanup_started_ = true;
            if (mode_ == "boundary_pre_shut") {
                expire_shutdown_deadline();
            }
            trace("pre-shut-return");
            return true;
        }
        if ((!sequence_finished_ && mode_ != "timeout_native") ||
            (!completion_returned_ && mode_ != "timeout_native") || pending_ || bridge_.pending() ||
            native_.joinable()) {
            throw std::runtime_error("Teardown preceded task-frame or native-resource retirement");
        }
        return mode_ != "pre_shut_false";
    }

    bool shut() override {
        trace("shut");
        if (mode_ == "boundary_shut") {
            expire_shutdown_deadline();
            trace("shut-return");
        }
        if (mode_ == "shut_throw") {
            throw std::runtime_error("test shutdown exception");
        }
        return mode_ != "shut_false";
    }

private:
    using callback = std::function<void(std::exception_ptr, std::string)>;

    void expire_shutdown_deadline() const {
        // Tighten the active stop only after reaching the hook under test.
        // Earlier tracing or scheduling must not choose a different boundary.
        // Reentrant stop cannot clean up this hook's active stack; its caller
        // must notice expiry after the successful hook returns.
        if (manager_.stop_until(manager_.get_now_time(), std::chrono::steady_clock::time_point::min()) ||
            manager_.get_shutdown_state() != ark::plugin_manager::shutdown_state::stopping) {
            throw std::runtime_error("Shutdown deadline entered the active lifecycle hook");
        }
        trace("deadline-expired");
    }

    std::uint64_t start(callback complete) {
        if (pending_) {
            throw std::logic_error("Overlapping controlled operations");
        }
        pending_ = std::move(complete);
        trace("start-" + std::to_string(++started_));
        return started_;
    }

    static ark::task<> rejected_root() {
        trace("unexpected-root");
        co_return;
    }

    static ark::task<> sequence(probe_module* self) {
        struct frame_lifetime {
            probe_module& owner;

            ~frame_lifetime() {
                owner.sequence_finished_ = true;
                trace("frame-retired");
            }
        } lifetime{*self};

        if (self->mode_ == "timeout_task" || self->mode_ == "timeout_stalled") {
            co_await asio::this_coro::reset_cancellation_state(asio::disable_cancellation());
        }
        const auto response =
            co_await self->bridge_.wait([self](auto complete) { return self->start(std::move(complete)); },
                [self](std::uint64_t) {
                    trace("cancel");
                    self->pending_(std::make_exception_ptr(arknet::system_error(asio::error::operation_aborted)), {});
                });
        trace("received-late");
    }

    const std::string name_{"business-runtime-probe"};
    ark::plugin_manager& manager_;
    const std::string mode_;
    ark::detail::database_awaitable<std::string> bridge_;
    callback pending_;
    unsigned started_{};
    bool stop_requested_{};
    bool cleanup_started_{};
    bool sequence_finished_{};
    bool completion_returned_{};
    bool stop_observed_{};
    bool update_resumed_{};
    bool owner_stopped_{};
    unsigned native_updates_{};
    std::chrono::steady_clock::time_point stop_started_;
    std::atomic<bool> native_release_{};
    std::atomic<bool> native_exited_{};
    mutable std::thread native_;
};

probe_module* active_module{};

} // namespace

ARK_EXPORT_FUNC void DllEntryPlugin(ark::plugin_manager* manager, const std::string& name, ark::logger* logger) {
    ark::logger::init(logger);
    active_module = new probe_module(*manager, manager->get_plugin_conf(name));
    if (!manager->add_module(active_module->get_name(), active_module) || !manager->add_update_module(active_module)) {
        throw std::runtime_error("Cannot register the business-runtime test module");
    }
}

ARK_EXPORT_FUNC void DllExitPlugin(ark::plugin_manager* manager) {
    trace("uninstall");
    manager->remove_module(active_module->get_name());
    manager->remove_update_module(active_module->get_name());
    delete std::exchange(active_module, nullptr);
}
