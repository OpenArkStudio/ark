// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <cstdlib>
#include <iostream>
#include <string>
#include <typeinfo>

#include <ark/kernel/class_meta_module.hpp>
#include <ark/kernel/config_module.hpp>
#include <ark/kernel/data_list_impl.hpp>
#include <ark/kernel/kernel_module.hpp>
#include <ark/kernel/map_module.hpp>
#include <ark/plugin_manager.hpp>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition)
    {
        std::cerr << message << '\n';
        std::_Exit(1);
    }
}

struct entity_events
{
    int on_event(const ark::guid_t&, const std::string&, ark::entity_event event, const ark::data_list&) {
        if (event == ark::entity_event::entity_evt_data_finished)
        {
            ++created;
        }
        if (event == ark::entity_event::entity_evt_destroy)
        {
            ++destroyed;
        }
        return 0;
    }

    unsigned created{};
    unsigned destroyed{};
};

template<typename interface_type>
interface_type* find_owned(ark::plugin_manager& manager) {
    auto* result = manager.find_module<interface_type>();
    require(result && result->get_plugin_manager() == &manager && result->get_name() == typeid(interface_type).name(),
        "Kernel interface registration key or manager ownership changed");
    return result;
}

} // namespace

int main(int argc, char* argv[]) {
    require(argc == 2 || argc == 3, "Expected actual plugin directory and optional JSON mode");
    const bool json = argc == 3 && std::string(argv[2]) == "json";
    ark::logger log;
    ark::logger::init(&log);
    log.set_log_func([](spdlog::level::level_enum, const std::string&) {});
    entity_events events;
    ark::plugin_manager manager;
    const bool started =
        manager.start(0, std::string(argv[1]) + "/", "conf/", {{"ark_kernel_plugin", ""}, {"ark_utility_plugin", ""}});
    require(started, "Kernel DSO startup failed: " + manager.get_startup_failure());
    auto* metadata = find_owned<ark::class_meta_module>(manager);
    auto* config = find_owned<ark::config_module>(manager);
    auto* kernel = find_owned<ark::kernel_module>(manager);
    auto* maps = find_owned<ark::map_module>(manager);
    require(metadata->find_meta("Player") != nullptr &&
                metadata->find_static_meta(json ? "Player" : "attribute") != nullptr,
        "Kernel metadata initialization changed");
    {
        auto record = config->find_static_entity(42);
        require(record && (json ? record->get_u_int32("hp") == 100 : record->get_int32("level") == 7) &&
                    kernel->get_static_entity(42) == record,
            "Kernel configuration record loading changed");
    }
    require(maps->create_map(7) && maps->exist_map(7), "Kernel map module did not create a map");
    require(kernel->add_class_call_back("Player", &events, &entity_events::on_event),
        "Kernel entity observer registration failed");
    {
        ark::data_list_impl args;
        if (json)
        {
            require(args.add_u_int(77) && args.add_u_int(104), "JSON argument construction failed");
        }
        auto entity = kernel->create_entity(1001, 7, 0, "Player", json ? 42 : 0, args);
        require(entity && entity->get_id() == 1001 && kernel->get_entity(1001) == entity && events.created == 1,
            "Kernel DSO entity creation or lifecycle callback changed");
        if (json)
        {
            require(entity->get_u_int32("hp") == 77 && entity->get_string("name") == "Configured",
                "JSON profile and per-field argument precedence changed");
        }
        ark::data_list_impl ids;
        require(maps->get_inst_entity_list(7, 0, ids) && ids.get_count() == 1 && ids.int64(0) == 1001,
            "Kernel entity did not retain its map registration");
    }
    require(manager.update(1) && manager.update(2), "Kernel manager updates failed");
    const bool stopped = manager.stop(3);
    require(stopped, "Kernel plugin exit failed: " + manager.get_shutdown_failure());
    require(events.destroyed == 1, "Kernel shutdown did not dispatch entity destruction exactly once");
    require(manager.stop(4), "Repeated Kernel stop failed");
    std::cout << "Kernel actual DSO interfaces, metadata/configuration, entity lifecycle and shutdown passed\n";
}
