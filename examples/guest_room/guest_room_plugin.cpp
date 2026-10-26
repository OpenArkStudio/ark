// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <stdexcept>

#include "guest_room_module.hpp"
#include "guest_room_plugin.hpp"

namespace ark::guest_room {

ARK_EXPORT_FUNC void DllEntryPlugin(plugin_manager* manager, const std::string& name, logger* log) {
    logger::init(log);
    manager->register_plugin<plugin>(name);
}

ARK_EXPORT_FUNC void DllExitPlugin(plugin_manager* manager) {
    manager->unregister_plugin<plugin>();
}

void plugin::install() {
    if (!register_module(std::make_unique<room_module>(), module_update::enabled))
    {
        throw std::runtime_error("guest_room module registration rejected");
    }
}

void plugin::uninstall() {
    if (!unregister_module<room_module>())
    {
        throw std::runtime_error("guest_room module unregistration rejected");
    }
}

} // namespace ark::guest_room
