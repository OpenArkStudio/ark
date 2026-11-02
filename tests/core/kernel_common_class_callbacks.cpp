// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <fstream>
#include <string>
#include <vector>

#include "../network/doctest.h"

#include "ark/kernel/class_meta_module_impl.hpp"
#include "ark/kernel/config_module_impl.hpp"
#include "ark/kernel/data_list_impl.hpp"
#include "ark/kernel/kernel_module_impl.hpp"
#include "ark/kernel/map_module_impl.hpp"
#include "kernel_metadata_files.hpp"

namespace {

class common_event_guid final : public ark::guid_module
{
public:
    ark::guid_t create_guid() override {
        return 1;
    }

    std::string parse_uid(const ark::guid_t&) override {
        return {};
    }
};

class common_event_receiver
{
public:
    explicit common_event_receiver(std::vector<std::string>& deliveries)
        : deliveries_(deliveries) {
    }

    common_event_receiver(const common_event_receiver&) = delete;
    common_event_receiver& operator=(const common_event_receiver&) = delete;

    int on_class(
        const ark::guid_t& self, const std::string& name, ark::entity_event event, const ark::data_list& arguments) {
        CHECK(self == 77);
        CHECK(event == ark::entity_event::entity_evt_all_finished);
        CHECK(&arguments == expected_arguments);
        deliveries_.push_back(name);
        return 0;
    }

    const ark::data_list* expected_arguments{};

private:
    std::vector<std::string>& deliveries_;
};

} // namespace

TEST_CASE("Kernel common class callbacks remain callable for every real metadata class") {
    ark::logger logger;
    ark::logger::init(&logger);
    logger.set_log_func([](spdlog::level::level_enum, const std::string&) {});

    struct reset_logger
    {
        ~reset_logger() {
            ark::logger::set_instance(nullptr);
        }
    } reset;

    ark::test::metadata_files files(false);
    {
        std::ofstream schema("resource/meta/player.config");
        schema.exceptions(std::ios::badbit | std::ios::failbit);
        schema << R"(<xml><meta class="Player" type="class"/><meta class="Item" type="class"/></xml>)";
    }
    ark::plugin_manager manager;
    ark::class_meta_module_impl metadata;
    ark::config_module_impl config;
    ark::kernel_module_impl implementation;
    ark::map_module_impl maps;
    common_event_guid guid;
    const auto add = [&]<typename interface_type>(interface_type& instance) {
        instance.set_plugin_manager(&manager);
        REQUIRE(manager.add_module(GET_CLASS_NAME(interface_type), &instance));
    };
    add.operator()<ark::class_meta_module>(metadata);
    add.operator()<ark::config_module>(config);
    add.operator()<ark::kernel_module>(implementation);
    add.operator()<ark::map_module>(maps);
    add.operator()<ark::guid_module>(guid);
    REQUIRE(metadata.init());
    REQUIRE(implementation.init());
    REQUIRE(metadata.get_meta_list().size() == 2);
    ark::kernel_module& kernel = implementation;
    ark::data_list_impl arguments;
    std::vector<std::string> deliveries;
    common_event_receiver receiver(deliveries);
    receiver.expected_arguments = &arguments;
    REQUIRE(kernel.add_common_class_event(&receiver, &common_event_receiver::on_class, -4));
    for (const auto* name : {"Player", "Item", "Player", "Item"})
    {
        REQUIRE(kernel.do_event(77, name, ark::entity_event::entity_evt_all_finished, arguments));
    }
    CHECK(deliveries == std::vector<std::string>{"Player", "Item", "Player", "Item"});
    CHECK(metadata.shut());
}
