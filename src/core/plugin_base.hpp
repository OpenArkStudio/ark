// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

#include <concepts>
#include <memory>
#include <string>
#include <typeinfo>
#include <unordered_map>
#include <utility>

#include "ark/plugin.hpp"
#include "ark/plugin_manager.hpp"

namespace ark {

enum class module_update {
    disabled,
    enabled
};

template<typename derived_type>
class plugin_base : public plugin {
public:
    plugin_base() = default;
    plugin_base(const plugin_base&) = delete;
    plugin_base& operator=(const plugin_base&) = delete;
    plugin_base(plugin_base&&) = delete;
    plugin_base& operator=(plugin_base&&) = delete;

    int get_plugin_version() override {
        return 0;
    }

    const std::string get_plugin_name() override {
        return typeid(derived_type).name();
    }

    plugin_manager* get_plugin_manager() const override {
        return plugin_manager_;
    }

    void set_plugin_manager(plugin_manager* value) override {
        ARK_ASSERT_RET_NONE(value != nullptr);
        plugin_manager_ = value;
    }

    static const std::string& get_plugin_conf() {
        return plugin_conf_;
    }

    void set_plugin_conf(const std::string& value) override {
        plugin_conf_ = value;
    }

protected:
    // Register an owner callback so dependency-ordered shutdown can destroy the
    // concrete module before its providers, while plugin code remains loaded.
    template<std::derived_from<module> interface_type>
    bool register_module(std::unique_ptr<interface_type> candidate, module_update update) {
        get_plugin_manager()->check_registration();
        if (!candidate) {
            return false;
        }
        const std::string name = typeid(interface_type).name();
        candidate->set_plugin_manager(get_plugin_manager());
        candidate->set_name(name);
        const auto [entry, inserted] = modules_.emplace(name, candidate.get());
        if (!inserted) {
            return false;
        }
        try {
            if (!get_plugin_manager()->register_owned_module(name, *candidate, update == module_update::enabled,
                    {this,
                        [](void* owner, const std::string& key) {
                            return static_cast<plugin_base*>(owner)->destroy_named_module(key);
                        },
                        lifetime_})) {
                modules_.erase(entry);
                return false;
            }
        } catch (...) {
            modules_.erase(entry);
            throw;
        }
        candidate.release();
        return true;
    }

    template<std::derived_from<module> interface_type>
    bool unregister_module() {
        return unregister_named_module(typeid(interface_type).name());
    }

private:
    bool unregister_named_module(const std::string& name) {
        get_plugin_manager()->check_registry_mutation();
        const auto entry = modules_.find(name);
        if (entry == modules_.end() || !destroy_named_module(name)) {
            return false;
        }
        modules_.erase(entry);
        return true;
    }

    bool destroy_named_module(const std::string& name) {
        get_plugin_manager()->check_registry_mutation();
        const auto entry = modules_.find(name);
        if (entry == modules_.end()) {
            return false;
        }
        // Keep a null entry until uninstall acknowledges this registration.
        // Existing explicit unregister calls still succeed exactly once.
        if (!entry->second) {
            return true;
        }
        std::unique_ptr<task_group> retired_tasks;
        if (!get_plugin_manager()->unregister_owned_module(entry->first, *entry->second, retired_tasks)) {
            return false;
        }
        auto* instance = std::exchange(entry->second, nullptr);
        // A concrete destructor must not remove another provider or reenter
        // teardown while the manager is walking its validated snapshot.
        typename plugin_manager::phase_guard destroying(
            *get_plugin_manager(), plugin_manager::call_phase::tearing_down);
        delete instance;
        return true;
    }

    std::unordered_map<std::string, module*> modules_;
    // Standalone plugin destruction keeps the legacy explicit ownership path;
    // it must not leave a callable manager pointer into the destroyed plugin.
    std::shared_ptr<void> lifetime_ = std::make_shared<int>(0);
    plugin_manager* plugin_manager_{nullptr};
    inline static std::string plugin_conf_;
};

} // namespace ark
