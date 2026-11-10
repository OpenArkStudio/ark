// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <cstdlib>
#include <iostream>
#include <string>
#include <typeinfo>

#include <ark/dynamic_library.hpp>
#include <ark/plugin_manager.hpp>
#include "login/interface/login_net_module.hpp"

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << std::endl;
        std::_Exit(1);
    }
}

} // namespace

int main(int argc, char* argv[]) {
    require(argc == 3, "Expected entry-probe library directory and stem");
    ark::logger log;
    ark::logger::init(&log);
    log.set_log_func([](spdlog::level::level_enum, const std::string&) {});
    ark::dynamic_library library(argv[2]);
    require(library.load(argv[1]), "Cannot load entry-probe library");
    auto events = reinterpret_cast<unsigned (*)(unsigned)>(library.get_symbol("server_entry_events"));
    require(events && events(0) == 0 && events(1) == 0, "Entry-probe state is not fresh");
    ark::plugin_manager manager;
    require(manager.start(0, argv[1], "conf/", {{argv[2], "entry-probe.conf"}}), "Explicit Login entry failed");
    auto* instance = manager.find_module<ark::login_net_module>();
    require(instance && instance->get_plugin_manager() == &manager &&
                instance->get_name() == typeid(ark::login_net_module).name(),
        "Explicit entry did not publish the Login interface");
    require(events(0) == 1 && events(1) == 0, "Explicit entry did not transfer factory ownership");
    require(manager.update(1) && events(2) == 0, "Explicit entry changed disabled update admission");
    require(manager.stop(2) && events(1) == 1, "Explicit Login exit did not destroy its owned module");
    require(manager.stop(3) && events(1) == 1, "Repeated stop invoked the exit path twice");
    require(library.unload(), "Cannot release entry-probe observation handle");
    std::cout << "server entry: real Login assembly entry/exit with a controlled factory passed\n";
}
