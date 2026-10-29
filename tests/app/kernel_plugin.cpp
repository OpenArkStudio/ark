// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
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
    std::optional<ark::entity_sync::scalar_snapshot> retained;
    std::optional<ark::entity_sync::nested_snapshot> retained_nested;
    {
        ark::data_list_impl args;
        if (json)
        {
            require(args.add_u_int(77) && args.add_u_int(104), "JSON argument construction failed");
        }
        auto entity = kernel->create_entity(1001, 7, 0, "Player", json ? 42 : 0, args);
        require(entity && entity->get_id() == 1001 && kernel->get_entity(1001) == entity && events.created == 1,
            "Kernel DSO entity creation or lifecycle callback changed");
        const auto empty = kernel->capture_scalar_snapshot(*entity, {});
        require(empty.entity_id == 1001 && empty.class_name == "Player" && empty.fields.empty(),
            "Kernel DSO empty scalar selection changed");
        if (json)
        {
            require(entity->get_u_int32("hp") == 77 && entity->get_string("name") == "Configured",
                "JSON profile and per-field argument precedence changed");
            const std::array<std::uint32_t, 10> selection{100, 104, 105, 106, 107, 108, 109, 110, 111, 112};
            retained = kernel->capture_scalar_snapshot(*entity, selection);
            auto* table = entity->find_table(200);
            require(table != nullptr, "Kernel DSO nested table missing");
            auto* row = table->add_row(3);
            require(row && row->set_u_int32(1, 13) && row->set_string(2, std::string("a\0b", 3)),
                "Kernel DSO nested row creation failed");
            auto child = kernel->create_container_entity(entity->get_id(), 201, "Item", 0);
            require(child && child->set_string(2, "Owned"), "Kernel DSO nested child creation failed");
            ark::entity_sync::nested_projection projection{"player/v1", entity->get_id(),
                {"Player", {100, 104}, {{200, {1, 2}}}, {{201, {{child->get_id(), {"Item", {1, 2}, {}, {}}}}}}}};
            retained_nested = kernel->capture_nested_snapshot(*entity, projection);
            require(retained_nested->root.incarnation != 0 &&
                        retained_nested->root.tables.at(200).rows.at(3).incarnation != 0 &&
                        retained_nested->root.containers.at(201).size() == 1,
                "Kernel DSO nested lifetime or relationship capture failed");
            const auto child_id = child->get_id();
            const auto slot = entity->find_container(201)->find(child_id);
            require(kernel->destroy_entity(child_id), "Kernel DSO nested child destruction failed");
            child.reset();
            auto replacement = kernel->create_entity(child_id, 7, 0, "Item", 0, ark::data_list_impl{});
            require(replacement && entity->find_container(201)->place(slot, replacement),
                "Kernel DSO reused GUID insertion failed");
            const auto reused = kernel->capture_nested_snapshot(*entity, projection);
            require(reused.root.containers.at(201).at(slot).incarnation !=
                        retained_nested->root.containers.at(201).at(slot).incarnation,
                "Kernel DSO reused GUID retained its old incarnation");
            require(kernel->destroy_entity(child_id), "Kernel DSO replacement destruction failed");
            replacement.reset();
            table->clear();
            const auto& values = retained->fields;
            require(values.size() == selection.size() && std::get<std::string>(values.at(100)) == "Configured" &&
                        std::get<std::uint32_t>(values.at(104)) == 77 && std::get<bool>(values.at(105)) &&
                        std::get<std::int32_t>(values.at(106)) == std::numeric_limits<std::int32_t>::min() &&
                        std::get<std::uint64_t>(values.at(107)) == std::numeric_limits<std::uint64_t>::max() &&
                        std::get<std::int64_t>(values.at(108)) == std::numeric_limits<std::int64_t>::min() &&
                        std::get<float>(values.at(109)) == 1.25F && std::get<double>(values.at(110)) == 2.5 &&
                        std::get<ark::entity_sync::guid_value>(values.at(111)).value == 42 &&
                        std::get<std::uint32_t>(values.at(112)) == 0,
                "Kernel DSO scalar capture types, widths or lazy defaults changed");
            require(entity->set_u_int64("counter", 7) && entity->set_guid("owner", -7) &&
                        entity->set_string("name", "Changed"),
                "Kernel DSO scalar mutation failed");
            const auto changed = kernel->capture_scalar_snapshot(*entity, selection);
            require(std::get<std::uint64_t>(changed.fields.at(107)) == 7 &&
                        std::get<ark::entity_sync::guid_value>(changed.fields.at(111)).value == -7 &&
                        std::get<std::string>(changed.fields.at(100)) == "Changed" &&
                        std::get<std::uint64_t>(values.at(107)) == std::numeric_limits<std::uint64_t>::max(),
                "Kernel DSO snapshot retained borrowed scalar state");
            bool limited = false;
            try
            { static_cast<void>(kernel->capture_scalar_snapshot(*entity, selection, {1, 0})); }
            catch (const std::length_error&)
            { limited = true; }
            require(limited, "Kernel DSO capture did not enforce its limits");
            auto lazy = kernel->create_entity(1002, 7, 0, "LazyOwner", 0, ark::data_list_impl{});
            require(lazy != nullptr, "Kernel DSO lazy entity creation failed");
            const std::array<std::uint32_t, 1> lazy_selection{112};
            const auto lazy_snapshot = kernel->capture_scalar_snapshot(*lazy, lazy_selection);
            ark::protobuf::protobuf_entity_data materialized;
            require(std::get<std::uint32_t>(lazy_snapshot.fields.at(112)) == 0 &&
                        kernel->entity_node_to_protobuf_data(lazy, &materialized) &&
                        materialized.datas_uint32().count(112) == 0,
                "Kernel DSO lazy capture materialized a node or lost its typed zero");
            require(kernel->destroy_entity(1002), "Kernel DSO lazy entity destruction failed");
            lazy.reset();
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
    if (retained)
    {
        require(std::get<std::string>(retained->fields.at(100)) == "Configured" &&
                    std::get<ark::entity_sync::guid_value>(retained->fields.at(111)).value == 42,
            "Kernel DSO unload invalidated an owning scalar snapshot");
    }
    if (retained_nested)
    {
        require(std::get<std::string>(retained_nested->root.scalars.fields.at(100)) == "Configured" &&
                    std::get<std::string>(retained_nested->root.tables.at(200).rows.at(3).fields.at(2)) ==
                        std::string("a\0b", 3) &&
                    std::get<std::string>(
                        retained_nested->root.containers.at(201).begin()->second.scalars.fields.at(2)) == "Owned",
            "Kernel DSO unload invalidated an owning nested snapshot");
    }
    std::cout
        << "Kernel actual DSO interfaces, scalar and nested snapshots, metadata/configuration, entity lifecycle and "
           "shutdown passed\n";
}
