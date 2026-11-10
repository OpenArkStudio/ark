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

#include <stdexcept>

#include "ark/kernel/kernel_plugin.hpp"
#include "kernel_factory.hpp"

namespace ark {

ARK_EXPORT_FUNC void DllEntryPlugin(plugin_manager* manager, const std::string& name, logger* log) {
    logger::init(log);
    manager->register_plugin<kernel_plugin>(name);
}

ARK_EXPORT_FUNC void DllExitPlugin(plugin_manager* manager) {
    manager->unregister_plugin<kernel_plugin>();
}

void kernel_plugin::install() {
    if (!register_module(detail::make_class_meta_module(), module_update::disabled)) {
        throw std::runtime_error("kernel class_meta_module registration rejected");
    }
    if (!register_module(detail::make_config_module(), module_update::disabled)) {
        throw std::runtime_error("kernel config_module registration rejected");
    }
    if (!register_module(detail::make_kernel_module(), module_update::enabled)) {
        throw std::runtime_error("kernel kernel_module registration rejected");
    }
    if (!register_module(detail::make_map_module(), module_update::disabled)) {
        throw std::runtime_error("kernel map_module registration rejected");
    }
}

void kernel_plugin::uninstall() {
    if (!unregister_module<map_module>()) {
        throw std::runtime_error("kernel map_module unregistration rejected");
    }
    if (!unregister_module<kernel_module>()) {
        throw std::runtime_error("kernel kernel_module unregistration rejected");
    }
    if (!unregister_module<config_module>()) {
        throw std::runtime_error("kernel config_module unregistration rejected");
    }
    if (!unregister_module<class_meta_module>()) {
        throw std::runtime_error("kernel class_meta_module unregistration rejected");
    }
}

} // namespace ark
