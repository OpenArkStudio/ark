#include <stdexcept>

#include "ark/consul/consul_plugin.hpp"
#include "consul_factory.hpp"

namespace ark {

ARK_EXPORT_FUNC void DllEntryPlugin(plugin_manager* manager, const std::string& name, logger* log) {
    logger::init(log);
    manager->register_plugin<consul_plugin>(name);
}

ARK_EXPORT_FUNC void DllExitPlugin(plugin_manager* manager) {
    manager->unregister_plugin<consul_plugin>();
}

void consul_plugin::install() {
    if (!register_module(detail::make_consul_module(), module_update::disabled))
    {
        throw std::runtime_error("Consul module registration rejected");
    }
}

void consul_plugin::uninstall() {
    if (!unregister_module<consul_module>())
    {
        throw std::runtime_error("Consul module unregistration rejected");
    }
}

} // namespace ark
