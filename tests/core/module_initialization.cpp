// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <memory>
#include <string>

#include "../network/doctest.h"

#include "ark/kernel/class_meta_module_impl.hpp"
#include "ark/kernel/config_module_impl.hpp"
#include "ark/kernel/data_impl.hpp"
#include "ark/kernel/kernel_module_impl.hpp"
#include "ark/kernel/map_module_impl.hpp"
#include "ark/kernel/node_impl.hpp"
#include "kernel_metadata_files.hpp"

namespace {

class unused_guid_module final : public ark::guid_module
{
public:
    ark::guid_t create_guid() override {
        return {};
    }

    std::string parse_uid(const ark::guid_t&) override {
        return {};
    }
};

using ark::test::metadata_files;

void verify_initialization_order(bool leading_separator, bool config_init_first, bool config_post_init_first) {
    ark::logger test_logger;
    ark::logger::init(&test_logger);
    test_logger.set_log_func([](spdlog::level::level_enum, const std::string&) {});

    metadata_files files(leading_separator);
    ark::plugin_manager manager;
    ark::class_meta_module_impl metadata;
    ark::config_module_impl config;
    ark::kernel_module_impl kernel;
    ark::map_module_impl map;
    unused_guid_module guid;

    const auto register_module = [&]<typename interface_type>(interface_type* implementation) {
        implementation->set_plugin_manager(&manager);
        REQUIRE(manager.add_module(GET_CLASS_NAME(interface_type), implementation));
    };
    register_module.operator()<ark::class_meta_module>(&metadata);
    register_module.operator()<ark::config_module>(&config);
    register_module.operator()<ark::kernel_module>(&kernel);
    register_module.operator()<ark::map_module>(&map);
    register_module.operator()<ark::guid_module>(&guid);

    REQUIRE(map.init());
    if (config_init_first)
    {
        REQUIRE(config.init());
        REQUIRE(kernel.init());
    }
    else
    {
        REQUIRE(kernel.init());
        REQUIRE(config.init());
    }

    // Consumers have resolved their interfaces, but no schema has been loaded.
    CHECK(config.find_static_entity(42) == nullptr);
    REQUIRE(metadata.init());
    REQUIRE(metadata.find_meta("Player") != nullptr);
    REQUIRE(metadata.find_static_meta("attribute") != nullptr);

    if (config_post_init_first)
    {
        REQUIRE(config.post_init());
        REQUIRE(kernel.post_init());
    }
    else
    {
        REQUIRE(kernel.post_init());
        REQUIRE(config.post_init());
    }

    const auto entity = config.find_static_entity(42);
    REQUIRE(entity != nullptr);
    CHECK(entity->get_config_id() == 42);
    CHECK(entity->get_int32("level") == 7);
    CHECK(kernel.get_static_entity(42) == entity);
    CHECK(kernel.update());
    CHECK(kernel.pre_shut());
    CHECK(config.shut());
    CHECK(metadata.shut());
}

void verify_initialization_orders(bool leading_separator) {
    SUBCASE("kernel init and kernel post_init first") {
        verify_initialization_order(leading_separator, false, false);
    }
    SUBCASE("kernel init and config post_init first") {
        verify_initialization_order(leading_separator, false, true);
    }
    SUBCASE("config init and kernel post_init first") {
        verify_initialization_order(leading_separator, true, false);
    }
    SUBCASE("config init and config post_init first") {
        verify_initialization_order(leading_separator, true, true);
    }
}

} // namespace

TEST_CASE("kernel and config initialize before schema regardless of consumer order") {
    SUBCASE("metadata paths relative to the resource directory") {
        verify_initialization_orders(false);
    }
    SUBCASE("metadata paths with a legacy leading separator") {
        verify_initialization_orders(true);
    }
}

TEST_CASE("sync callback registration replaces the prior owner for every singleton mask handler") {
    struct observations
    {
        unsigned nodes{}, tables{}, containers{}, delayed{};
    };

    const auto register_owner = [](std::shared_ptr<observations> owner) {
        using callbacks = ark::class_call_back_manager;
        const auto mask = ark::data_mask::pf_sync_view;
        callbacks::add_node_sync_call_back(
            mask, [owner](const ark::guid_t&, std::uint32_t, ark::data_mask, const ark::data&) {
                ++owner->nodes;
                return 0;
            });
        callbacks::add_table_sync_call_back(
            mask, [owner](const ark::guid_t&, const ark::table_event_data&, ark::data_mask, const ark::data&) {
                ++owner->tables;
                return 0;
            });
        callbacks::add_container_sync_call_back(mask, [owner](const ark::guid_t&, std::uint32_t, ark::data_mask,
                                                          ark::container_op_type, std::uint32_t, std::uint32_t) {
            ++owner->containers;
            return 0;
        });
        callbacks::add_delay_sync_call_back(
            mask, [owner](const ark::guid_t&, ark::data_mask, const ark::delay_sync_data&) {
                ++owner->delayed;
                return 0;
            });
    };
    auto first = std::make_shared<observations>();
    const std::weak_ptr<observations> retired = first;
    register_owner(first);
    first.reset();
    const auto current = std::make_shared<observations>();
    register_owner(current);
    CHECK(retired.expired());

    ark::class_call_back_manager callbacks;
    auto metadata = std::make_shared<ark::node_meta>("level", 1);
    metadata->set_type(ark::data_type::dt_int32);
    ark::mask_type immediate;
    immediate.set(static_cast<std::size_t>(ark::data_mask::pf_sync_view));
    immediate.set(static_cast<std::size_t>(ark::data_mask::pf_real_time));
    metadata->set_mask(immediate);
    ark::node_int32 node(metadata);
    ark::data_impl before(ark::data_type::dt_int32, 0);
    ark::data_impl after(ark::data_type::dt_int32, 1);
    CHECK(callbacks.on_node_call_back(1, &node, before, after));
    ark::table_event_data event;
    event.table_index = 2;
    CHECK(callbacks.on_table_call_back(1, immediate, &node, event, before, after));
    CHECK(callbacks.on_container_call_back(1, 3, immediate, ark::container_op_type::op_place, 1, 0));
    immediate.reset(static_cast<std::size_t>(ark::data_mask::pf_real_time));
    metadata->set_mask(immediate);
    CHECK(callbacks.on_node_call_back(1, &node, before, after));
    CHECK(ark::class_call_back_manager::on_delay_sync());
    CHECK(current->nodes == 1);
    CHECK(current->tables == 1);
    CHECK(current->containers == 1);
    CHECK(current->delayed == 1);
}
