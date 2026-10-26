#include <stdexcept>

#include "ark/etcd_client/etcd_client_plugin.hpp"
#include "etcd_client_factory.hpp"

namespace ark {

ARK_EXPORT_FUNC void DllEntryPlugin(plugin_manager* manager, const std::string& name, logger* log) {
    logger::init(log);
    manager->register_plugin<etcd_client_plugin>(name);
}

ARK_EXPORT_FUNC void DllExitPlugin(plugin_manager* manager) {
    manager->unregister_plugin<etcd_client_plugin>();
}

void etcd_client_plugin::install() {
    if (!register_module(detail::make_etcd_client_module(), module_update::disabled))
    {
        throw std::runtime_error("Etcd Client module registration rejected");
    }
}

void etcd_client_plugin::uninstall() {
    if (!unregister_module<etcd_client_module>())
    {
        throw std::runtime_error("Etcd Client module unregistration rejected");
    }
}

} // namespace ark
