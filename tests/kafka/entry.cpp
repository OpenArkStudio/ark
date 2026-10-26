// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <cstdlib>
#include <iostream>
#include <typeinfo>

#include <ark/dynamic_library.hpp>
#include <ark/kafka/kafka_module.hpp>
#include <ark/plugin_manager.hpp>

namespace {

void require(bool condition, const char* message) {
    if (!condition)
    {
        std::cerr << message << std::endl;
        std::_Exit(1);
    }
}

} // namespace

int main(int argc, char* argv[]) {
    require(argc == 3, "Expected Kafka entry-probe directory and library stem");
    ark::logger log;
    ark::logger::init(&log);
    log.set_log_func([](spdlog::level::level_enum, const std::string&) {});
    ark::dynamic_library library(argv[2]);
    require(library.load(argv[1]), "Cannot load Kafka entry-probe library");
    auto events = reinterpret_cast<unsigned (*)(unsigned)>(library.get_symbol("kafka_entry_events"));
    require(events && events(0) == 0 && events(1) == 0, "Entry-probe state is not fresh");
    require(library.get_symbol("DllEntryPlugin") && library.get_symbol("DllExitPlugin"),
        "Kafka explicit entry/exit exports are missing");
    ark::plugin_manager manager;
    require(manager.start(0, argv[1], "conf/", {{argv[2], "entry-probe.toml"}}), "Kafka explicit entry failed");
    auto* instance = manager.find_module<ark::kafka_module>();
    require(instance && instance->get_plugin_manager() == &manager &&
                instance->get_name() == typeid(ark::kafka_module).name(),
        "Kafka explicit entry lost interface registration metadata");
    require(events(0) == 1 && events(1) == 0, "Kafka entry did not transfer factory ownership");
    require(manager.update(1) && events(2) == 1, "Kafka entry lost enabled update selection");
    require(manager.stop(2) && events(1) == 1, "Kafka explicit exit did not destroy its module exactly once");
    require(manager.stop(3) && events(1) == 1, "Repeated stop invoked Kafka exit twice");
    require(library.unload(), "Cannot release Kafka observation handle");
    std::cout << "Kafka real assembly entry/exit, configuration, interface metadata and ownership passed\n";
}
