#include <stdexcept>

#include "ark/kafka/kafka_plugin.hpp"
#include "kafka_factory.hpp"

namespace ark {

ARK_EXPORT_FUNC void DllEntryPlugin(plugin_manager* manager, const std::string& name, logger* log) {
    logger::init(log);
    manager->register_plugin<kafka_plugin>(name);
}

ARK_EXPORT_FUNC void DllExitPlugin(plugin_manager* manager) {
    manager->unregister_plugin<kafka_plugin>();
}

void kafka_plugin::install() {
    if (!register_module(detail::make_kafka_module(), module_update::enabled))
    {
        throw std::runtime_error("Kafka module registration rejected");
    }
}

void kafka_plugin::uninstall() {
    if (!unregister_module<kafka_module>())
    {
        throw std::runtime_error("Kafka module unregistration rejected");
    }
}

} // namespace ark
