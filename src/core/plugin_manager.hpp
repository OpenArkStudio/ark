/*
 * This source file is part of ARK
 * For the latest info, see https://github.com/ArkNX
 *
 * Copyright (c) 2013-2020 ArkNX authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"),
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <exception>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ark/dynamic_library.hpp"
#include "ark/logger.hpp"
#include "ark/module.hpp"
#include "ark/noncopyable.hpp"
#include "ark/plugin.hpp"
#include "ark/startup_cleanup.hpp"
#include "ark/task.hpp"

namespace ark {

namespace detail {
struct plugin_manager_test_access;
}

template<typename derived_type>
class plugin_base;

class plugin_manager final : private noncopyable
{
    using startup_modules = std::vector<std::pair<std::string, module*>>;

    enum class call_phase
    {
        idle,
        starting,
        installing,
        updating,
        stopping_acceptance,
        tearing_down,
        uninstalling,
        startup_cleanup
    };

    class phase_guard
    {
    public:
        phase_guard(plugin_manager& manager, call_phase phase)
            : manager_(manager)
            , previous_(manager.phase_) {
            manager_.phase_ = phase;
        }

        ~phase_guard() {
            manager_.phase_ = previous_;
        }

    private:
        plugin_manager& manager_;
        call_phase previous_;
    };

public:
    enum class shutdown_state
    {
        running,
        draining,
        failed,
        stopped,
        stopping_acceptance,
        timed_out,
        cleanup_timed_out
    };

    plugin_manager() = default;

    ~plugin_manager() {
        // Destruction cannot drive a drain or release code executing on this
        // stack. Keep the same visible contract as the task groups themselves.
        if (owner_ != std::this_thread::get_id() || execution_active() || pending_tasks_unchecked())
        {
            std::terminate();
        }
    }

    static auto instance() -> plugin_manager* {
        static plugin_manager* instance_ptr = nullptr;
        if (instance_ptr == nullptr)
        {
            instance_ptr = ARK_NEW plugin_manager();
        }

        return instance_ptr;
    }

    // Prepare before resource acquisition. The capsule must be independently
    // owned; neither the action nor its deleter may borrow an incomplete module.
    // One slot, keys <=95 bytes; false means acquire nothing and fail startup.
    bool prepare_startup_cleanup(std::string_view owner, std::string_view code_owner,
        const std::shared_ptr<void>& context, startup_cleanup_action action) noexcept {
        if (owner_ != std::this_thread::get_id() || startup_finished_ ||
            (phase_ != call_phase::idle && phase_ != call_phase::installing && phase_ != call_phase::starting) ||
            shutdown_state_ != shutdown_state::running || task_runtime_->get_executor().running_in_this_thread())
        {
            return false;
        }
        return startup_cleanup_.prepare(owner, code_owner, context, action);
    }

    const startup_call_record& get_startup_call() const noexcept {
        return startup_call_;
    }

    // Call only after failed start() returned/unwound. No poll/cancel, normal
    // shutdown, deletion or DSO unload. Caller must still terminate with exit 1.
    startup_cleanup_result cleanup_failed_startup() noexcept {
        if (owner_ != std::this_thread::get_id() || execution_active() || !startup_finished_ || startup_succeeded_)
        {
            return startup_cleanup_result::unsafe;
        }
        startup_cleanup_started_ = true;
        shutdown_state_ = shutdown_state::failed;
        phase_guard active(*this, call_phase::startup_cleanup);
        for (const auto& [owner, tasks] : task_groups_)
        {
            tasks->stop_accepting();
        }
        return startup_cleanup_.run();
    }

    auto start(int64_t const timestamp, std::string const& lib_dir, std::string const& conf_dir,
        std::unordered_map<std::string, std::string> const& plugins) -> bool {
        check_owner();
        if (execution_active() || startup_finished_ || shutdown_state_ != shutdown_state::running)
        {
            throw std::logic_error("Plugin manager cannot start during a lifecycle call or after shutdown begins");
        }
        phase_guard active(*this, call_phase::starting);

        struct attempt_guard
        {
            plugin_manager& manager;

            ~attempt_guard() {
                manager.startup_finished_ = true;
                if (!manager.startup_succeeded_ && manager.startup_call_.result == startup_call_result::entered)
                {
                    manager.startup_call_.result = startup_call_result::exception;
                }
            }
        } attempt{*this};

        startup_call_.enter("<manager>", "prepare");
        startup_failure_.clear();
        timestamp_ = timestamp;

        plugin_library_dir_ = lib_dir;
        plugin_conf_dir_ = conf_dir;
        plugins_ = plugins;

        if (!init())
        {
            return false;
        }
        if (!post_init(lifecycle_order_))
        {
            return false;
        }
        if (!check_config(lifecycle_order_))
        {
            return false;
        }
        if (!pre_update(lifecycle_order_))
        {
            return false;
        }

        startup_succeeded_ = true;
        startup_cleanup_.disarm();
        return true;
    }

    auto stop(const int64_t timestamp) -> bool {
        return stop_with(timestamp, [](dynamic_library& library) { return library.unload(); });
    }

    // The earliest supplied deadline remains in force for all later stop calls.
    // Expiry is sticky; user hooks are checked only at synchronous boundaries.
    // Keep updates before cleanup, but disable them after cleanup has begun.
    auto stop_until(const int64_t timestamp, std::chrono::steady_clock::time_point deadline) -> bool {
        return stop_with(timestamp, [](dynamic_library& library) { return library.unload(); }, deadline);
    }

    auto update(const int64_t timestamp, std::size_t maximum_handlers = 64) -> bool {
        check_owner();
        if (execution_active())
        {
            throw std::logic_error("Plugin manager update does not permit reentrant lifecycle calls");
        }
        if (shutdown_state_ == shutdown_state::failed || shutdown_state_ == shutdown_state::stopped ||
            shutdown_state_ == shutdown_state::cleanup_timed_out)
        {
            return false;
        }
        phase_guard active(*this, call_phase::updating);
        timestamp_ = timestamp;

        // Just loop the modules which have update function.
        for (const auto& iter : module_updates_)
        {
            module* module_ptr = iter.second;
            ARK_ASSERT_CONTINUE(module_ptr != nullptr);

            module_ptr->update();
        }

        // Modules publish service results and schedule bridge deliveries first.
        // One bounded owner poll serves every managed group, including drain.
        task_runtime_->poll(maximum_handlers);
        return true;
    }

    std::shared_ptr<arknet::manual_runtime> get_task_runtime() const {
        check_owner();
        return task_runtime_;
    }

    task_group& create_task_group(module& owner, std::size_t maximum = 64) {
        check_owner();
        if (shutdown_state_ != shutdown_state::running || !registered_module(owner))
        {
            throw std::logic_error("Task group requires a registered module before shutdown begins");
        }
        if (task_groups_.contains(&owner))
        {
            throw std::logic_error("Module already owns a task group");
        }
        auto tasks = std::make_unique<task_group>(task_runtime_, maximum);
        auto& result = *tasks;
        task_groups_.emplace(&owner, std::move(tasks));
        return result;
    }

    task_group& get_tasks(module& owner) const {
        check_owner();
        const auto found = task_groups_.find(&owner);
        if (!registered_module(owner) || found == task_groups_.end())
        {
            throw std::logic_error("Module has no managed task group");
        }
        return *found->second;
    }

    std::size_t pending_tasks() const {
        check_owner();
        return pending_tasks_unchecked();
    }

    const std::string& get_startup_failure() const {
        check_owner();
        return startup_failure_;
    }

    shutdown_state get_shutdown_state() const {
        check_owner();
        return shutdown_state_;
    }

    std::exception_ptr get_shutdown_exception() const {
        check_owner();
        return shutdown_exception_;
    }

    const std::string& get_shutdown_failure() const {
        check_owner();
        return shutdown_failure_;
    }

    // Nullable dependency lookup for owner-thread lifecycle code. Unlike
    // find_module(), absence and a mismatched runtime type are ordinary results.
    template<typename module_type>
        requires std::is_base_of_v<module, module_type>
    auto try_find_module() -> module_type* {
        check_owner();
        return dynamic_cast<module_type*>(find_module(GET_CLASS_NAME(module_type)));
    }

    template<typename module_type>
        requires std::is_base_of_v<module, module_type>
    auto find_module() -> module_type* {
        module* logic_module = find_module(GET_CLASS_NAME(module_type));
        ARK_ASSERT_RET_VAL(logic_module != nullptr, nullptr);

        auto derived_module = dynamic_cast<module_type*>(logic_module);
        ARK_ASSERT_RET_VAL(derived_module != nullptr, nullptr);

        return derived_module;
    }

    template<typename plugin_type>
    void register_plugin(std::string const& plugin_name) {
        check_registration();
        std::string runtime_plugin_name = GET_CLASS_NAME(plugin_type);
        startup_call_.enter(plugin_name, "constructor");
        plugin* new_plugin{};
        try
        {
            new_plugin = ARK_NEW plugin_type();
            startup_call_.result = startup_call_result::passed;
        }
        catch (...)
        {
            startup_call_.result = startup_call_result::exception;
            throw;
        }
        register_plugin(new_plugin, plugin_name, runtime_plugin_name);
    }

    template<typename plugin_type>
    void unregister_plugin() {
        //! Delete plugin memory in Unregister
        unregister_plugin(GET_CLASS_NAME(plugin_type));
    }

    auto add_module(const std::string& module_name, module* module_ptr) -> bool {
        check_registration();
        if (!module_ptr || registered_module(*module_ptr))
        {
            return false;
        }
        ARK_ASSERT_RET_VAL(!module_name.empty(), false);
        ARK_ASSERT_RET_VAL(find_module(module_name) == nullptr, false);
        const auto inserted = module_instances_.insert(std::make_pair(module_name, module_ptr)).second;
        if (inserted)
        {
            lifecycle_order_valid_ = false;
        }
        return inserted;
    }

    void remove_module(const std::string& module_name) {
        check_registry_mutation();
        auto iter = module_instances_.find(module_name);
        if (iter == module_instances_.end())
        {
            return;
        }

        // A task owned by another module can still borrow this provider. Until
        // dependency lifetimes are explicit, every managed group must drain.
        if (pending_tasks_unchecked())
        {
            throw std::logic_error("Cannot remove modules while managed tasks are pending");
        }
        const auto tasks = task_groups_.find(iter->second);
        if (tasks != task_groups_.end())
        {
            task_groups_.erase(tasks);
        }
        module_instances_.erase(iter);
        lifecycle_order_valid_ = false;
    }

    auto add_update_module(module* module_ptr) -> bool {
        check_registration();
        ARK_ASSERT_RET_VAL(module_ptr != nullptr, false);
        return module_updates_.insert(std::make_pair(module_ptr->get_name(), module_ptr)).second;
    }

    void remove_update_module(const std::string& module_name) {
        check_registry_mutation();
        module_updates_.erase(module_name);
    }

    auto get_now_time() const -> int64_t {
        return timestamp_;
    }

    auto get_bus_id() const -> bus_id_t {
        return bus_id_;
    }

    void set_bus_id(const bus_id_t value) {
        bus_id_ = value;
    }

    auto get_app_name() const -> const std::string& {
        return app_name_;
    }

    void set_app_name(const std::string& value) {
        app_name_ = value;
    }

    void set_app_conf(const std::string& value) {
        ARK_ASSERT_RET_NONE(!value.empty());
        ARK_ASSERT_RET_NONE(value.find(".app.conf") != std::string::npos || value.ends_with(".toml"));

        app_conf_path_ = value;
    }

    auto get_app_conf() const -> std::string const& {
        return app_conf_path_;
    }

    auto get_plugin_conf(std::string const& plugin_name) -> std::string const& {
        const static std::string null_str;
        ARK_ASSERT_RET_VAL(!plugin_name.empty(), null_str);
        auto iter = plugins_.find(plugin_name);
        return iter != plugins_.end() ? iter->second : null_str;
    }

    template<typename plugin_type>
    auto get_plugin_conf() -> const std::string& {
        auto plugin = find_plugin<plugin_type>();
        ARK_ASSERT_RET_VAL(plugin != nullptr, null_str);

        return plugin->get_plugin_conf();
    }

protected:
    void register_plugin(plugin* plugin, std::string const& plugin_name, std::string const& runtime_plugin_name) {
        const auto destroy = [](ark::plugin* value) { ARK_DELETE(value); };
        std::unique_ptr<ark::plugin, decltype(destroy)> candidate(plugin, destroy);
        check_registration();
        if (!candidate)
        {
            startup_failure_ = "allocate_plugin: " + plugin_name;
            return;
        }
        // The library stem selects configuration; the runtime name identifies
        // the C++ plugin type registered by its entry point.

        ARK_ASSERT_RET_NONE(find_plugin(plugin_name) == nullptr);

        auto iter = plugins_.find(plugin_name);
        ARK_ASSERT_NO_EFFECT(iter != plugins_.end());

        plugin->set_plugin_manager(this);
        // set plugin self configuration
        plugin->set_plugin_conf(plugin_conf_dir_ + iter->second);
        // Install modules
        // Installation may publish resources that require cooperative shutdown.
        // Retain the existing process-exit policy once user installation begins.
        candidate.release();
        startup_call_.enter(plugin_name, "install");
        try
        {
            plugin->install();
            startup_call_.result = startup_call_result::passed;
        }
        catch (...)
        {
            startup_call_.result = startup_call_result::exception;
            throw;
        }
        // manage this plugin
        plugin_instances_.insert(std::make_pair(runtime_plugin_name, plugin));
    }

    void unregister_plugin(const std::string& plugin_name) {
        check_registry_mutation();
        if (pending_tasks_unchecked())
        {
            throw std::logic_error("Cannot uninstall plugins with pending managed tasks");
        }
        ARK_ASSERT_RET_NONE(!plugin_name.empty());

        auto plugin = find_plugin(plugin_name);
        ARK_ASSERT_RET_NONE(plugin != nullptr);

        plugin->uninstall();
        plugin_instances_.erase(plugin_name);
        ARK_DELETE(plugin);
    }

    auto find_plugin(const std::string& plugin_name) -> plugin* {
        auto iter = plugin_instances_.find(plugin_name);
        if (iter != plugin_instances_.end())
        {
            return iter->second;
        }
        else
        {
            return nullptr;
        }
    }

    template<typename plugin_type>
        requires std::is_base_of_v<plugin, plugin_type>
    auto find_plugin() -> plugin* {
        auto plugin_name = GET_CLASS_NAME(plugin_type);
        auto iter = plugin_instances_.find(plugin_name);
        if (iter != plugin_instances_.end())
        {
            return iter->second;
        }
        else
        {
            return nullptr;
        }
    }

    auto find_module(const std::string& module_name) -> module* {
        auto iter = module_instances_.find(module_name);
        return ((iter != module_instances_.end()) ? iter->second : nullptr);
    }

    bool order_modules(startup_modules& order, std::string& failure, const char* phase) {
        // Lexicographic registration keys break ties independently of unordered
        // registry insertion and declaration order. Duplicate edges count once.
        std::map<std::string, std::size_t> pending;
        std::map<std::string, std::vector<std::string>> consumers;
        for (const auto& [name, instance] : module_instances_)
        {
            pending.emplace(name, 0);
        }
        for (auto& [name, count] : pending)
        {
            const bool startup = std::string_view(phase) == "dependencies";
            if (startup)
            {
                startup_call_.enter(name, phase);
            }
            failure = std::string(phase) + ": " + name;
            if (startup)
            {
                startup_call_.diagnostic_ready = true;
            }
            auto dependencies = module_instances_.at(name)->get_dependencies();
            if (startup)
            {
                startup_call_.result = startup_call_result::passed;
            }
            std::sort(dependencies.begin(), dependencies.end());
            dependencies.erase(std::unique(dependencies.begin(), dependencies.end()), dependencies.end());
            for (const auto& provider : dependencies)
            {
                if (!pending.contains(provider))
                {
                    failure += " requires missing module '" + provider + "'";
                    return false;
                }
                ++count;
                consumers[provider].push_back(name);
            }
        }
        std::set<std::string> ready;
        for (const auto& [name, count] : pending)
        {
            if (count == 0)
            {
                ready.insert(name);
            }
        }
        while (!ready.empty())
        {
            auto name = *ready.begin();
            ready.erase(ready.begin());
            order.emplace_back(name, module_instances_.at(name));
            for (const auto& consumer : consumers[name])
            {
                if (--pending.at(consumer) == 0)
                {
                    ready.insert(consumer);
                }
            }
        }
        if (order.size() != pending.size())
        {
            failure = std::string(phase) + (std::string(phase) == "dependencies"
                                                   ? ": cycle prevents startup; blocked modules: "
                                                   : ": cycle prevents admission stop; blocked modules: ");
            bool first = true;
            for (const auto& [name, count] : pending)
            {
                if (count != 0)
                {
                    failure += (first ? "" : ", ") + name;
                    first = false;
                }
            }
            return false;
        }
        failure.clear();
        return true;
    }

    bool call_startup(module& instance, const std::string& name, std::string_view phase, bool (module::*hook)()) {
        startup_call_.enter(name, phase);
        try
        {
            startup_failure_ = std::string(phase) + ": " + name;
            startup_call_.diagnostic_ready = true;
            const bool result = (instance.*hook)();
            startup_call_.result = result ? startup_call_result::passed : startup_call_result::returned_false;
            return result;
        }
        catch (...)
        {
            startup_call_.result = startup_call_result::exception;
            throw;
        }
    }

    auto init() -> bool {
        // load plugin dynamic libraries
        for (auto const& iter : plugins_)
        {
            phase_guard installing(*this, call_phase::installing);
            startup_call_.enter(iter.first, "load_plugin");
            startup_failure_ = "load_plugin: " + iter.first;
            startup_call_.diagnostic_ready = true;
            if (!load_plugin_library(iter.first))
            {
                startup_call_.result = startup_call_result::returned_false;
                return false;
            }
            startup_call_.result = startup_call_result::passed;
        }

        startup_call_.enter("<registry>", "dependencies");
        // Count construction propagates MSVC Debug iterator-proxy allocation failures.
        startup_modules order(0);
        if (!order_modules(order, startup_failure_, "dependencies"))
        {
            startup_call_.result = startup_call_result::returned_false;
            return false;
        }
        startup_call_.result = startup_call_result::passed;
        lifecycle_order_ = std::move(order);
        lifecycle_order_valid_ = true;

        // Initialize providers before their declared consumers in every phase.
        for (const auto& [name, module_ptr] : lifecycle_order_)
        {
            ARK_ASSERT_CONTINUE(module_ptr != nullptr);

            if (!call_startup(*module_ptr, name, "init", &module::init))
            {
                return false;
            }
        }

        startup_failure_.clear();
        return true;
    }

    auto post_init(const startup_modules& order) -> bool {
        for (const auto& [name, module_ptr] : order)
        {
            ARK_ASSERT_CONTINUE(module_ptr != nullptr);

            if (!call_startup(*module_ptr, name, "post_init", &module::post_init))
            {
                return false;
            }
        }

        startup_failure_.clear();
        return true;
    }

    auto check_config(const startup_modules& order) -> bool {
        for (const auto& [name, module_ptr] : order)
        {
            ARK_ASSERT_CONTINUE(module_ptr != nullptr);

            if (!call_startup(*module_ptr, name, "check_config", &module::check_config))
            {
                return false;
            }
        }

        startup_failure_.clear();
        return true;
    }

    auto pre_update(const startup_modules& order) -> bool {
        for (const auto& [name, module_ptr] : order)
        {
            ARK_ASSERT_CONTINUE(module_ptr != nullptr);

            if (!call_startup(*module_ptr, name, "pre_update", &module::pre_update))
            {
                return false;
            }
        }

        startup_failure_.clear();
        return true;
    }

    auto ready_to_shut() -> bool {
        bool ready = true;
        for (auto const& iter : module_instances_)
        {
            const module* module_ptr = iter.second;
            ARK_ASSERT_CONTINUE(module_ptr != nullptr);

            shutdown_failure_ = "ready_to_shut: " + iter.first;
            if (shutdown_deadline_expired())
            {
                return false;
            }
            const auto module_ready = module_ptr->ready_to_shut();
            if (shutdown_deadline_expired())
            {
                return false;
            }
            if (!module_ready)
            {
                ready = false;
            }
        }

        shutdown_failure_.clear();
        return ready;
    }

    auto pre_shut() -> bool {
        for (auto const& iter : module_instances_)
        {
            module* module_ptr = iter.second;
            ARK_ASSERT_CONTINUE(module_ptr != nullptr);

            shutdown_failure_ = "pre_shut: " + iter.first;
            if (!module_ptr->pre_shut() || shutdown_deadline_expired(true))
            {
                return false;
            }
        }

        shutdown_failure_.clear();
        return true;
    }

    auto load_plugin_library(const std::string& plugin_name) -> bool {
        auto iter = plugin_libs_.find(plugin_name);
        ARK_ASSERT_RET_VAL(iter == plugin_libs_.end(), false);

        const auto destroy = [](dynamic_library* value) { ARK_DELETE(value); };
        std::unique_ptr<dynamic_library, decltype(destroy)> library(ARK_NEW dynamic_library(plugin_name), destroy);
        ARK_ASSERT_RET_VAL(library != nullptr, false);

        bool load_ret = library->load(plugin_library_dir_);
        if (load_ret)
        {
            auto* loaded_library = library.get();
            plugin_libs_.insert(std::make_pair(plugin_name, loaded_library));
            library.release();
            auto func = reinterpret_cast<dll_entry_plugin_func>(loaded_library->get_symbol("DllEntryPlugin"));
            if (func == nullptr)
            {
                ARK_LOG_ERROR("Shared library[{}] has no DllEntryPlugin entrypoint", loaded_library->get_name());
                // Keep an already loaded handle until process exit. Static
                // initializers may have started work before symbol validation.
                return false;
            }
            func(this, plugin_name, logger::instance());

            // The void entrypoint cannot return a nullable allocation failure.
            return !startup_failure_.starts_with("allocate_plugin: ");
        }
        else
        {
#ifdef ARK_PLATFORM_WIN
            auto error = GetLastError();
#else
            auto error = dlerror();
#endif
            ARK_LOG_ERROR("Load shared library[{}] failed, ErrorNo[{}]", library->get_name(), error);
            // Native loading failed, so this object owns no loaded handle.
            return false;
        }
    }

    auto unload_plugin_library(const std::string& plugin_name) -> bool {
        auto iter = plugin_libs_.find(plugin_name);
        ARK_ASSERT_RET_VAL(iter != plugin_libs_.end(), false);

        dynamic_library* library = iter->second;
        ARK_ASSERT_RET_VAL(library != nullptr, false);

        auto func = reinterpret_cast<dll_exit_plugin_func>(library->get_symbol("DllExitPlugin"));
        ARK_ASSERT_RET_VAL(func != nullptr, false);
        func(this);

        return true;
    }

private:
    friend struct detail::plugin_manager_test_access;

    template<typename unload_type>
    auto stop_with(const int64_t timestamp, unload_type unload,
        std::optional<std::chrono::steady_clock::time_point> deadline = std::nullopt) -> bool {
        check_owner();
        timestamp_ = timestamp;
        if (shutdown_state_ == shutdown_state::stopped)
        {
            return true;
        }
        if (shutdown_state_ == shutdown_state::failed || shutdown_state_ == shutdown_state::timed_out ||
            shutdown_state_ == shutdown_state::cleanup_timed_out)
        {
            return false;
        }
        if (deadline && (!shutdown_deadline_ || *deadline < *shutdown_deadline_))
        {
            shutdown_deadline_ = deadline;
        }
        if (shutdown_state_ == shutdown_state::running)
        {
            shutdown_state_ = shutdown_state::stopping_acceptance;
            for (const auto& [owner, tasks] : task_groups_)
            {
                tasks->stop_accepting();
            }
        }
        // The last task is removed before its completion is called. Even zero
        // pending tasks cannot permit teardown while that callback is active.
        if (execution_active())
        {
            return false;
        }
        try
        {
            if (shutdown_state_ == shutdown_state::stopping_acceptance)
            {
                phase_guard stopping(*this, call_phase::stopping_acceptance);
                shutdown_failure_ = "stop_accepting";
                if (shutdown_deadline_expired())
                {
                    return false;
                }
                // Idle registry changes invalidate old pointers. Rebuild only
                // here, before the one-shot hook sequence, never during drain.
                if (!lifecycle_order_valid_)
                {
                    startup_modules order(0);
                    if (!order_modules(order, shutdown_failure_, "stop_accepting"))
                    {
                        shutdown_state_ = shutdown_state::failed;
                        return false;
                    }
                    lifecycle_order_ = std::move(order);
                    lifecycle_order_valid_ = true;
                    shutdown_failure_ = "stop_accepting";
                    if (shutdown_deadline_expired())
                    {
                        return false;
                    }
                }
                for (auto iter = lifecycle_order_.rbegin(); iter != lifecycle_order_.rend(); ++iter)
                {
                    shutdown_failure_ = "stop_accepting: " + iter->first;
                    if (!iter->second->stop_accepting())
                    {
                        shutdown_state_ = shutdown_state::failed;
                        return false;
                    }
                    if (shutdown_deadline_expired())
                    {
                        return false;
                    }
                }
                shutdown_failure_.clear();
                shutdown_state_ = shutdown_state::draining;
            }
            if (pending_tasks_unchecked())
            {
                if (shutdown_deadline_expired())
                {
                    shutdown_failure_ = "managed_tasks";
                }
                return false;
            }
            phase_guard active(*this, call_phase::tearing_down);
            if (!ready_to_shut())
            {
                return false;
            }
            // Readiness may be empty, or its final return may reach the limit.
            shutdown_failure_ = "ready_to_shut";
            if (shutdown_deadline_expired())
            {
                return false;
            }
            if (!pre_shut() || !shut(unload))
            {
                if (shutdown_state_ != shutdown_state::cleanup_timed_out)
                {
                    shutdown_state_ = shutdown_state::failed;
                }
                return false;
            }
        }
        catch (...)
        {
            shutdown_exception_ = std::current_exception();
            shutdown_state_ = shutdown_state::failed;
            return false;
        }
        shutdown_state_ = shutdown_state::stopped;
        return true;
    }

    bool shutdown_deadline_expired(bool cleanup_started = false) {
        if (!shutdown_deadline_ || std::chrono::steady_clock::now() < *shutdown_deadline_)
        {
            return false;
        }
        shutdown_state_ = cleanup_started ? shutdown_state::cleanup_timed_out : shutdown_state::timed_out;
        return true;
    }

    template<typename unload_type>
    auto shut(unload_type unload) -> bool {
        for (auto const& iter : module_instances_)
        {
            module* module_ptr = iter.second;
            ARK_ASSERT_CONTINUE(module_ptr != nullptr);

            shutdown_failure_ = "shut: " + iter.first;
            if (!module_ptr->shut() || shutdown_deadline_expired(true))
            {
                return false;
            }
        }

        phase_guard uninstalling(*this, call_phase::uninstalling);
        for (auto& it : plugins_)
        {
            shutdown_failure_ = "uninstall: " + it.first;
            if (!unload_plugin_library(it.first) || shutdown_deadline_expired(true))
            {
                return false;
            }
        }

        for (auto& iter : plugin_libs_)
        {
            shutdown_failure_ = "unload: " + iter.first;
            if (!unload(*iter.second))
            {
                return false;
            }
            ARK_DELETE(iter.second);
            if (shutdown_deadline_expired(true))
            {
                return false;
            }
        }

        shutdown_failure_.clear();
        return true;
    }

    template<typename derived_type>
    friend class plugin_base;

    // Publish only registry bookkeeping; no user callbacks or lifecycle rollback.
    bool register_owned_module(const std::string& name, module& instance, bool update) {
        check_registration();
        if (module_instances_.contains(name) || module_updates_.contains(name) || registered_module(instance))
        {
            return false;
        }
        const auto entry = module_instances_.emplace(name, &instance).first;
        try
        {
            if (update)
            {
                module_updates_.emplace(name, &instance);
            }
        }
        catch (...)
        {
            module_instances_.erase(entry);
            throw;
        }
        lifecycle_order_valid_ = false;
        return true;
    }

    bool unregister_owned_module(const std::string& name, module& expected) {
        check_registry_mutation();
        const auto entry = module_instances_.find(name);
        const auto update = module_updates_.find(name);
        if (entry == module_instances_.end() || entry->second != &expected ||
            (update != module_updates_.end() && update->second != &expected))
        {
            return false;
        }
        remove_module(name); // Preserve the global pending-task and owner/phase guards.
        if (update != module_updates_.end())
        {
            module_updates_.erase(update);
        }
        return true;
    }

    void check_owner() const {
        if (owner_ != std::this_thread::get_id())
        {
            throw std::logic_error("Plugin manager requires its constructing thread");
        }
    }

    bool execution_active() const {
        return phase_ != call_phase::idle || task_runtime_->get_executor().running_in_this_thread();
    }

    void check_registry_mutation() const {
        check_owner();
        if (startup_cleanup_started_ ||
            (phase_ != call_phase::idle && phase_ != call_phase::installing && phase_ != call_phase::uninstalling) ||
            task_runtime_->get_executor().running_in_this_thread())
        {
            throw std::logic_error("Cannot mutate the module registry during update or shutdown hooks");
        }
    }

    void check_registration() const {
        check_registry_mutation();
        if (shutdown_state_ != shutdown_state::running)
        {
            throw std::logic_error("Cannot register modules or plugins after shutdown begins");
        }
    }

    bool registered_module(const module& owner) const {
        for (const auto& [name, instance] : module_instances_)
        {
            if (instance == &owner)
            {
                return true;
            }
        }
        return false;
    }

    std::size_t pending_tasks_unchecked() const {
        std::size_t count{};
        for (const auto& [owner, tasks] : task_groups_)
        {
            count += tasks->pending();
        }
        return count;
    }

    const std::thread::id owner_ = std::this_thread::get_id();
    // Standard Asio executors do not own their context. Declare runtime before
    // groups so it outlives every group during valid owner-side destruction.
    std::shared_ptr<arknet::manual_runtime> task_runtime_ = std::make_shared<arknet::manual_runtime>();
    std::unordered_map<module*, std::unique_ptr<task_group>> task_groups_;
    call_phase phase_{call_phase::idle};
    startup_modules lifecycle_order_;
    bool lifecycle_order_valid_{};
    shutdown_state shutdown_state_{shutdown_state::running};
    std::optional<std::chrono::steady_clock::time_point> shutdown_deadline_;
    std::exception_ptr shutdown_exception_;
    std::string shutdown_failure_;
    std::string startup_failure_;
    detail::startup_cleanup_slot startup_cleanup_;
    startup_call_record startup_call_;
    bool startup_finished_{};
    bool startup_succeeded_{};
    bool startup_cleanup_started_{};

    // Bus id
    int bus_id_{0};
    // Current time(ms)
    int64_t timestamp_{0};
    // plugin so/dll file dir
    std::string plugin_library_dir_{};
    // xx.plugin.conf file path
    std::string plugin_conf_dir_{};
    // xx.app.conf file path
    std::string app_conf_path_{};
    // app name
    std::string app_name_{"ark"};

    using dll_entry_plugin_func = void (*)(plugin_manager*, std::string const&, logger*);
    using dll_exit_plugin_func = void (*)(plugin_manager*);

    // plugin_code_name -> plugin_config
    std::unordered_map<std::string, std::string> plugins_;
    // plugin_code_name -> AFDynLib*
    std::unordered_map<std::string, dynamic_library*> plugin_libs_;
    // plugin_code_name -> AFIPlugin*
    std::unordered_map<std::string, plugin*> plugin_instances_;
    // module_runtime_name -> AFIModule*
    std::unordered_map<std::string, module*> module_instances_;
    // Only include the module with self Update function. module_runtime_name -> AFIModule*
    std::unordered_map<std::string, module*> module_updates_;
};

} // namespace ark
