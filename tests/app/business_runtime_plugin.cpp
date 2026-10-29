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
// No result is released until the real launcher enters managed-task drain.
// The native-readiness deadline case has no task dependency: scheduling the
// coroutine chain must not consume its separate 50 ms shutdown deadline.
class probe_module final : public ark::module
{
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
        if (mode_.starts_with("boundary_"))
        {
            // No task, native thread or later poll can delay these hooks.
            return true;
        }
        native_ = std::thread([this] {
            while (!native_release_.load(std::memory_order_acquire))
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            trace("native-exit");
            native_exited_.store(true, std::memory_order_release);
        });
        auto& tasks = manager_.create_task_group(*this, 2);
        if (mode_ == "timeout_native")
        {
            trace("native-pending");
            return true;
        }
        return start_external_root(sequence(this), [this](std::exception_ptr error) {
            if (error)
            {
                trace("task-error");
                return;
            }
            trace("complete-begin");
            if (manager_.stop(manager_.get_now_time()))
            {
                throw std::runtime_error("Teardown entered the final completion stack");
            }
            trace("callback-stop-pending");
            completion_returned_ = true;
            trace("complete-end");
        }) != 0;
    }

    bool stop_accepting() override {
        trace("stop-accepting");
        external_accepting_ = false;
        if (mode_.starts_with("boundary_"))
        {
            if (mode_ == "boundary_admission")
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            trace("admission-return");
            return true;
        }
        if (manager_.get_tasks(*this).start(rejected_root(), [](std::exception_ptr) {}) != 0)
        {
            throw std::runtime_error("Admission hook accepted a managed root");
        }
        trace("hook-root-rejected");
        if (start_external_root(rejected_root(), {}) != 0)
        {
            throw std::runtime_error("Admission hook left its external root gate open");
        }
        if (manager_.stop(manager_.get_now_time()))
        {
            throw std::runtime_error("Admission hook entered reentrant teardown");
        }
        trace("admission-stop-pending");
        if (mode_ == "stop_accepting_throw")
        {
            throw std::runtime_error("test stop-accepting exception");
        }
        if (mode_ == "stop_accepting_throw_unknown")
        {
            throw 7;
        }
        return mode_ != "stop_accepting_false";
    }

    bool update() override {
        if (mode_.starts_with("boundary_"))
        {
            if (cleanup_started_)
            {
                trace("update-after-cleanup");
            }
            if (!stop_requested_)
            {
                stop_requested_ = true;
                trace("request-stop");
#if !defined(_WIN32)
                std::raise(SIGTERM);
#endif
            }
            return true;
        }
        const auto state = manager_.get_shutdown_state();
        const auto draining = state == ark::plugin_manager::shutdown_state::draining ||
                              state == ark::plugin_manager::shutdown_state::timed_out;
        if (draining && !drain_observed_)
        {
            drain_observed_ = true;
            drain_started_ = std::chrono::steady_clock::now();
            auto& tasks = manager_.get_tasks(*this);
            if (tasks.start(rejected_root(), [](std::exception_ptr) {}) != 0)
            {
                throw std::runtime_error("Shutdown accepted a new root task");
            }
            trace("root-rejected");
            if (mode_ == "update_throw_unknown")
            {
                trace("update-throw");
                throw 7;
            }
            if (mode_ == "update_throw")
            {
                trace("update-throw");
                throw std::runtime_error("test drain update exception");
            }
        }
        if ((mode_ == "update_throw" || mode_ == "update_throw_unknown") && draining && !update_resumed_)
        {
            update_resumed_ = true;
            trace("update-resumed");
        }
        const auto delayed =
            draining && std::chrono::steady_clock::now() - drain_started_ < std::chrono::milliseconds(200);
        if ((completion_returned_ || (draining && mode_ == "timeout_native")) && !owner_stopped_)
        {
            owner_stopped_ = true;
            trace("owner-stop");
        }
        if (owner_stopped_ && ++native_updates_ > 2 && (mode_ != "timeout_native" || !delayed))
        {
            native_release_.store(true, std::memory_order_release);
        }
        if ((pending_ || mode_ == "timeout_native") && !stop_requested_ && !draining)
        {
            stop_requested_ = true;
            trace("request-stop");
#if !defined(_WIN32)
            std::raise(SIGTERM);
#endif
        }
        if (pending_)
        {
            if (draining && mode_ != "timeout_stalled" && (mode_ != "timeout_task" || !delayed))
            {
                trace("reply-" + std::to_string(started_));
                auto complete = std::move(pending_);
                complete({}, started_ == 1 ? "first" : started_ == 2 ? "second" : "third");
            }
        }
        bridge_.dispatch();
        return true;
    }

    bool ready_to_shut() const override {
        if (mode_.starts_with("boundary_"))
        {
            trace("ready");
            if (mode_ == "boundary_readiness")
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            trace("ready-return");
            return true;
        }
        if (!owner_stopped_ || !native_exited_.load(std::memory_order_acquire))
        {
            return false;
        }
        if (mode_ == "ready_throw" || mode_ == "ready_throw_unknown")
        {
            trace("ready-throw");
            if (mode_ == "ready_throw_unknown")
            {
                throw 7;
            }
            throw std::runtime_error("test readiness exception");
        }
        if (native_.joinable())
        {
            native_.join();
            trace("native-joined");
        }
        return true;
    }

    bool pre_shut() override {
        trace("pre-shut");
        if (mode_.starts_with("boundary_"))
        {
            cleanup_started_ = true;
            if (mode_ == "boundary_pre_shut")
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            trace("pre-shut-return");
            return true;
        }
        if (!sequence_finished_ || !completion_returned_ || pending_ || bridge_.pending() || native_.joinable())
        {
            throw std::runtime_error("Teardown preceded accepted business completion");
        }
        return mode_ != "pre_shut_false";
    }

    bool shut() override {
        trace("shut");
        if (mode_ == "shut_throw")
        {
            throw std::runtime_error("test shutdown exception");
        }
        return mode_ != "shut_false";
    }

private:
    using callback = std::function<void(std::exception_ptr, std::string)>;

    ark::task_group::task_id start_external_root(ark::task<> work, ark::task_group::completion_handler complete) {
        if (!external_accepting_)
        {
            trace("external-root-rejected");
            return 0;
        }
        return manager_.get_tasks(*this).start(std::move(work), std::move(complete));
    }

    std::uint64_t start(callback complete) {
        if (pending_)
        {
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
        const auto steps = self->mode_ == "drain_admission" ? 3u : 2u;
        for (unsigned step = 1; step <= steps; ++step)
        {
            const auto response =
                co_await self->bridge_.wait([self](auto complete) { return self->start(std::move(complete)); },
                    [](std::uint64_t) { trace("cancel"); });
            if (response != (step == 1 ? "first" : step == 2 ? "second" : "third"))
            {
                throw std::runtime_error("Controlled operation lost its owning response");
            }
            trace("received-" + std::to_string(step));
        }
        self->sequence_finished_ = true;
    }

    const std::string name_{"business-runtime-probe"};
    ark::plugin_manager& manager_;
    const std::string mode_;
    ark::detail::database_awaitable<std::string> bridge_;
    callback pending_;
    unsigned started_{};
    bool stop_requested_{};
    bool external_accepting_{true};
    bool cleanup_started_{};
    bool sequence_finished_{};
    bool completion_returned_{};
    bool drain_observed_{};
    bool update_resumed_{};
    bool owner_stopped_{};
    unsigned native_updates_{};
    std::chrono::steady_clock::time_point drain_started_;
    std::atomic<bool> native_release_{};
    std::atomic<bool> native_exited_{};
    mutable std::thread native_;
};

probe_module* active_module{};

} // namespace

ARK_EXPORT_FUNC void DllEntryPlugin(ark::plugin_manager* manager, const std::string& name, ark::logger* logger) {
    ark::logger::init(logger);
    active_module = new probe_module(*manager, manager->get_plugin_conf(name));
    if (!manager->add_module(active_module->get_name(), active_module) || !manager->add_update_module(active_module))
    {
        throw std::runtime_error("Cannot register the business-runtime test module");
    }
}

ARK_EXPORT_FUNC void DllExitPlugin(ark::plugin_manager* manager) {
    trace("uninstall");
    manager->remove_module(active_module->get_name());
    manager->remove_update_module(active_module->get_name());
    delete std::exchange(active_module, nullptr);
}
