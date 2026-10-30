// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <concepts>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <typeinfo>

#include <ark/application_manifest.hpp>
#include <ark/kernel/data_list_impl.hpp>
#include <ark/kernel/kernel_module.hpp>
#include <ark/kernel/map_module.hpp>
#include <ark/meta_define.hpp>
#include <ark/plugin_manager.hpp>
#include <ark/utility/compress_module.hpp>
#include <ark/utility/guid_module.hpp>
#include <ark/utility/schedule_module.hpp>
#include <ark/utility/timer_module.hpp>
#include "../../examples/entity_operations/entity_operations_module.hpp"
#include "../../examples/module_lifecycle/module_lifecycle_module.hpp"
#include "../../examples/timers/timers_module.hpp"

// These preserve the old registration macro's update detection for all three examples.
static_assert(!std::same_as<decltype(&ark::timers_module::update), decltype(&ark::module::update)>);
static_assert(std::same_as<decltype(&ark::module_lifecycle_module::update), decltype(&ark::module::update)>);
static_assert(
    std::derived_from<ark::timers_module, ark::module_base<ark::module, ark::timer_module, ark::guid_module>>);
static_assert(std::derived_from<ark::module_lifecycle_module, ark::module_base<ark::module>>);

static_assert(std::derived_from<ark::entity_operations_module, ark::module_base<ark::module>>);
static_assert(std::same_as<decltype(&ark::entity_operations_module::update), decltype(&ark::module::update)>);

#include "owner_drain.hpp"

namespace {

struct timer_receiver
{
    void on_timer(uint64_t id, const ark::guid_t& entity) {
        ++calls;
        timer_id = id;
        entity_id = entity;
    }

    int calls{};
    uint64_t timer_id{};
    ark::guid_t entity_id{};
};

struct entity_receiver
{
    int on_event(const ark::guid_t&, const std::string&, ark::entity_event event, const ark::data_list&) {
        if (event == ark::entity_event::entity_evt_destroy)
        {
            ++destroyed;
        }
        return 0;
    }

    unsigned destroyed{};
};

void require(bool condition, const std::string& message) {
    if (!condition)
    {
        std::cerr << message << std::endl;
        // Preserve retained startup resources on a failing test process too.
        std::_Exit(1);
    }
}

} // namespace

int main(int argc, char* argv[]) {
    require(argc == 4, "Expected source root, plugin directory and sample name");
    const std::string sample = argv[3];
    require(sample == "timers" || sample == "module_lifecycle" || sample == "entity_operations", "Unknown sample");
    const auto manifest =
        ark::load_application_manifest((std::filesystem::path(argv[1]) / "config" / (sample + ".app.toml")).string());
    std::ostringstream events;
    auto* original = std::cout.rdbuf(events.rdbuf());
    ark::logger log;
    ark::logger::init(&log);
    log.set_log_func([&events](spdlog::level::level_enum, const std::string& text) { events << text << '\n'; });
    entity_receiver entity_events;
    ark::plugin_manager manager;
    try
    {
        const bool started = manager.start(0, std::string(argv[2]) + "/", "conf/", manifest.plugins);
        require(started, "Sample startup failed: " + manager.get_startup_failure());
        if (sample == "entity_operations")
        {
            auto* kernel = manager.find_module<ark::kernel_module>();
            auto* maps = manager.find_module<ark::map_module>();
            require(
                kernel && maps && maps->exist_map(1), "Entity example did not resolve Kernel/Map or create its map");
            ark::data_list_impl ids;
            require(maps->get_inst_entity_list(1, 0, ids) && ids.get_count() == 4,
                "Entity example must create four players");
            unsigned edited = 0;
            unsigned table_demo = 0;
            for (std::size_t i = 0; i < ids.get_count(); ++i)
            {
                auto player = kernel->get_entity(ids.int64(i));
                require(
                    player && player->get_string(ark::entity_meta_player::name()) == "Xiaoming", "Player name changed");
                for (const auto& field : {ark::entity_meta_player::career(), ark::entity_meta_player::camp(),
                         ark::entity_meta_player::last_map_id(), ark::entity_meta_player::level()})
                {
                    require(player->get_int32(field) == 1, "Player initialization changed: " + field);
                }
                const auto gender = player->get_int32(ark::entity_meta_player::gender_index());
                require(gender == 1 || gender == 11, "Unexpected player gender");
                edited += gender == 11;
                require(player->get_custom_int32("test") == 111, "Custom data initialization changed");
                require(player->add_custom_u_int32("unsigned_sequence", 0), "Custom uint32 creation failed");
                for (const auto value : {std::numeric_limits<std::uint32_t>::max(), std::uint32_t{0}})
                {
                    require(player->set_custom_u_int32("unsigned_sequence", value) &&
                                player->get_custom_u_int32("unsigned_sequence") == value,
                        "Public entity custom uint32 update failed through the real Kernel DSO");
                }
                require(!player->set_custom_u_int32("test", 7) && player->get_custom_int32("test") == 111,
                    "Custom uint32 update changed a signed field");
                auto* items = player->find_table(ark::entity_meta_player::items());
                require(items && items->first() &&
                            items->first()->get_int64(ark::entity_meta_item_bag::guid_index()) == 1111,
                    "Initial item row GUID changed");
                if (items->get_row_count() == 2)
                {
                    auto* row = items->first();
                    row = items->next();
                    require(row && row->get_int32(ark::entity_meta_item_bag::count_index()) == 22,
                        "Table demonstration count changed");
                    ++table_demo;
                }
                require(
                    player->find_container(ark::entity_meta_player::bag_index()) != nullptr, "Bag metadata missing");
            }
            require(
                edited == 1 && table_demo == 1, "Lightweight data/table demonstrations did not preserve final values");
            const auto before_callback = events.str().size();
            auto child = kernel->create_container_entity(
                ids.int64(0), ark::entity_meta_player::bag_index(), ark::entity_meta_item_bag::self_name(), 0);
            require(child != nullptr &&
                        events.str().find("OnContainerCallBackEvent start", before_callback) != std::string::npos,
                "Original bag callback did not receive a real container event");
            const auto child_id = child->get_id();
            child.reset();
            require(kernel->destroy_entity(child_id), "Container fixture cleanup failed");
            require(kernel->add_class_call_back(
                        ark::entity_meta_player::self_name(), &entity_events, &entity_receiver::on_event),
                "Shutdown entity observer registration failed");
            require(
                events.str().find("Entity data demonstration: name=Xiaoming gender=11 custom=111") != std::string::npos,
                "Missing lightweight demonstration result");
            require(events.str().find("query with [int key]") == std::string::npos &&
                        events.str().find("set with [int key]") == std::string::npos,
                "Startup must not run the timed workload");
        }
        auto* guids = manager.find_module<ark::guid_module>();
        auto* timers = manager.find_module<ark::timer_module>();
        auto* schedules = manager.find_module<ark::schedule_module>();
        auto* compression = manager.find_module<ark::compress_module>();
        for (auto* service : {static_cast<ark::module*>(guids), static_cast<ark::module*>(timers),
                 static_cast<ark::module*>(schedules), static_cast<ark::module*>(compression)})
        {
            require(service && service->get_plugin_manager() == &manager, "Utility interface lost manager ownership");
        }
        require(guids->get_name() == typeid(ark::guid_module).name() &&
                    timers->get_name() == typeid(ark::timer_module).name() &&
                    schedules->get_name() == typeid(ark::schedule_module).name() &&
                    compression->get_name() == typeid(ark::compress_module).name(),
            "Utility registration must retain interface RTTI keys");
        const auto first_id = guids->create_guid();
        const auto second_id = guids->create_guid();
        require(first_id != ark::null_guid && second_id != first_id && !guids->parse_uid(first_id).empty(),
            "Utility GUID generation or parsing failed");
        const std::string input("utility binary\0round trip", 25);
        for (auto algorithm : {ark::compress_type::compress_type_zlib, ark::compress_type::compress_type_zstd})
        {
            std::string packed;
            std::string unpacked;
            require(compression->compress(algorithm, input.data(), input.size(), packed), "Utility compression failed");
            require(compression->decompress(algorithm, packed.data(), packed.size(), unpacked) && unpacked == input,
                "Utility decompression did not preserve binary bytes");
        }
        timer_receiver timer;
        const auto timer_id =
            timers->add_single_timer(first_id, std::chrono::milliseconds(20), 1, &timer, &timer_receiver::on_timer);
        require(timer_id != 0 && timer.calls == 0, "Utility timer fired without manager updates");
        for (int i = 1; i <= 5; ++i)
        {
            require(manager.update(i * 10), "Sample owner update failed");
        }
        require(timer.calls == 1 && timer.timer_id == timer_id && timer.entity_id == first_id,
            "Utility timer must fire once through owner-thread updates with original callback arguments");
        int64_t tick = 59;
        ark::test::drain_owner(
            manager, [&] { return ++tick; }, std::chrono::steady_clock::now() + std::chrono::seconds(5), sample);
        require(manager.stop(++tick), "Repeated stop must not unregister Utility modules twice");
        if (sample == "entity_operations")
        {
            require(entity_events.destroyed == 4, "Shutdown must destroy each demonstration player exactly once");
        }
        require(manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::stopped,
            "Sample resources were retained instead of stopped");
    }
    catch (const std::exception& error)
    {
        require(false,
            "Sample lifecycle exception: " + std::string(error.what()) + "; startup: " + manager.get_startup_failure());
    }
    std::cout.rdbuf(original);
    const auto output = events.str();
    std::cout << output;
    std::size_t previous{};
    for (const std::string event : {sample == "timers" ? "init" : "Init", "PostInit", "PreShut", "Shut"})
    {
        auto position = output.find(sample + "_module", previous);
        while (position != std::string::npos)
        {
            const auto end = output.find('\n', position);
            if (output.substr(position, end - position).find(", " + event) != std::string::npos)
            {
                break;
            }
            position = output.find(sample + "_module", end);
        }
        require(position != std::string::npos, "Missing/out-of-order sample event: " + event);
        previous = output.find('\n', position);
    }
    std::cout << sample
              << ": actual DSOs, four Utility interfaces, GUID, zlib/zstd binary round trips, owner-driven timer, "
                 "startup and shutdown passed"
              << std::endl;
}
