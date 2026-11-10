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

#include "ark/bus/msg_module.hpp"
#include "bus_plugin.hpp"

namespace ark {

ARK_EXPORT_FUNC void DllEntryPlugin(plugin_manager* manager, const std::string& name, logger* log) {
    logger::init(log);
    manager->register_plugin<bus_plugin>(name);
}

ARK_EXPORT_FUNC void DllExitPlugin(plugin_manager* manager) {
    manager->unregister_plugin<bus_plugin>();
}

void bus_plugin::install() {
    if (!register_module(detail::make_bus_module(), module_update::disabled)) {
        throw std::runtime_error("bus module registration rejected");
    }
    if (!register_module(detail::make_msg_module(), module_update::disabled)) {
        throw std::runtime_error("bus message module registration rejected");
    }
}

void bus_plugin::uninstall() {
    if (!unregister_module<msg_module>()) {
        throw std::runtime_error("bus message module unregistration rejected");
    }
    if (!unregister_module<bus_module>()) {
        throw std::runtime_error("bus module unregistration rejected");
    }
}

} // namespace ark
