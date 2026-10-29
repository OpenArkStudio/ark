// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

#include <concepts>
#include <string>
#include <typeinfo>
#include <vector>

#include "ark/module.hpp"
#include "ark/plugin_manager.hpp"

namespace ark {

template<std::derived_from<module> interface_type, std::derived_from<module>... provider_types>
class module_base : public interface_type
{
public:
    std::vector<std::string> get_dependencies() const override {
        return {typeid(provider_types).name()...};
    }

    plugin_manager* get_plugin_manager() const override {
        return plugin_manager_;
    }

    void set_plugin_manager(plugin_manager* value) override {
        ARK_ASSERT_RET_NONE(value != nullptr);
        plugin_manager_ = value;
    }

    const std::string& get_name() const override {
        return name_;
    }

    void set_name(const std::string& value) override {
        name_ = value;
    }

protected:
    template<std::derived_from<module> module_type>
    module_type* find_module() {
        return get_plugin_manager()->template find_module<module_type>();
    }

private:
    plugin_manager* plugin_manager_{nullptr};
    std::string name_;
};

} // namespace ark
