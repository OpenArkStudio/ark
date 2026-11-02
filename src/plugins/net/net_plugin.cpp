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

#include "http_client_factory.hpp"
#include "net_plugin.hpp"

namespace ark {

ARK_EXPORT_FUNC void DllEntryPlugin(plugin_manager* manager, const std::string& name, logger* log) {
    logger::init(log);
    manager->register_plugin<net_plugin>(name);
}

ARK_EXPORT_FUNC void DllExitPlugin(plugin_manager* manager) {
    manager->unregister_plugin<net_plugin>();
}

void net_plugin::install() {
    if (!register_module(detail::make_http_client_module(get_plugin_manager()), module_update::enabled))
    {
        throw std::runtime_error("net HTTP client module registration rejected");
    }
    if (!register_module(detail::make_net_service_manager_module(), module_update::enabled))
    {
        throw std::runtime_error("net service manager module registration rejected");
    }
}

void net_plugin::uninstall() {
    if (!unregister_module<net_service_manager_module>())
    {
        throw std::runtime_error("net service manager module unregistration rejected");
    }
    if (!unregister_module<http_client_module>())
    {
        throw std::runtime_error("net HTTP client module unregistration rejected");
    }
}

} // namespace ark
