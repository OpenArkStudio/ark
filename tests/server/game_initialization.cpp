// Copyright (c) 2026 OpenArkStudio. SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <typeinfo>
#include <vector>

#include "../network/doctest.h"

#include <ark/kernel/class_meta_module_impl.hpp>
#include <ark/kernel/config_module_impl.hpp>
#include <ark/kernel/data_list_impl.hpp>
#include <ark/kernel/kernel_module_impl.hpp>
#include <ark/kernel/map_module_impl.hpp>
#include <ark/module_base.hpp>
#include <ark/net/net_service_manager_module.hpp>
#include "bus/bus_plugin.hpp"
#include "game_net_module.hpp"
#include "game_plugin.hpp"
#include "utility/utility_plugin.hpp"

namespace {

// Control only listener creation. Kernel and every metadata provider below are
// production implementations; the separate DSO suite owns real loopback IO.
class listener_probe final : public ark::module_base<ark::net_service_manager_module>
{
public:
    unsigned create_calls{};

    bool create_server(const size_t, const ark::msg_header&, const ark::msg_header&) override {
        ++create_calls;
        return false;
    }

    std::shared_ptr<ark::net_server_service> get_inter_net_server() override {
        return {};
    }

    std::shared_ptr<ark::net_server_service> get_intra_net_server() override {
        return {};
    }

    bool add_net_connection_bus(ark::bus_id_t, std::shared_ptr<ark::net>) override {
        return false;
    }

    bool remove_net_connection_bus(ark::bus_id_t) override {
        return false;
    }

    std::shared_ptr<ark::net> get_net_connection_bus(ark::bus_id_t, ark::bus_id_t) override {
        return {};
    }

    std::shared_ptr<ark::net_client_service> get_client_service(const ark::app_type) override {
        return {};
    }

    ark::conv_id_t get_session_id(ark::bus_id_t) override {
        return {};
    }

    bool remove_session_id(ark::conv_id_t) override {
        return false;
    }

    bool create_client_service(const ark::bus_addr&, ark::proto_type, const std::string&, uint16_t,
        std::shared_ptr<const ark::msg_header>) override {
        return false;
    }
};

class game_world
{
public:
    explicit game_world(bool with_player = true)
        : previous_directory_(std::filesystem::current_path())
        , directory_(std::filesystem::temp_directory_path() /
                     ("ark-game-order-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
        , previous_logger_(ark::logger::instance()) {
        ark::logger::init(&logger_);
        logger_.set_log_func(
            [this](spdlog::level::level_enum, const std::string& message) { messages.push_back(message); });
        std::filesystem::create_directories(directory_ / "resource/meta");
        std::filesystem::current_path(directory_);
        std::ofstream schema("resource/meta/entity_schema.json");
        schema.exceptions(std::ios::badbit | std::ios::failbit);
        schema << "{\"format\":\"ark.entity-schema\",\"version\":1,\"classes\":[{\"name\":\""
               << (with_player ? "Player" : "Other")
               << "\",\"fields\":[],\"tables\":[],\"containers\":[]}],\"configs\":[]}";
        schema.close();
        manager_.set_bus_id(ark::bus_addr(1, 1, 31, 1).bus_id);
        bus_ = ark::detail::make_bus_module();
        guid_ = ark::detail::make_guid_module();
        add<ark::bus_module>(*bus_);
        add<ark::guid_module>(*guid_);
        add<ark::class_meta_module>(metadata);
        add<ark::config_module>(config_);
        add<ark::kernel_module>(kernel_);
        add<ark::map_module>(maps_);
        add<ark::net_service_manager_module>(network);
        owner_.set_plugin_manager(&manager_);
        owner_.install();
        game = manager_.find_module<ark::game_net_module>();
        REQUIRE(game != nullptr);
    }

    ~game_world() {
        if (providers_initialized_)
        {
            kernel_.pre_shut();
            maps_.shut();
            config_.shut();
            metadata.shut();
        }
        owner_.uninstall();
        ark::logger::set_instance(previous_logger_);
        std::error_code ignored;
        std::filesystem::current_path(previous_directory_, ignored);
        std::filesystem::remove_all(directory_, ignored);
    }

    void initialize_providers(bool kernel_post_init_first = true) {
        REQUIRE(guid_->init());
        REQUIRE(maps_.init());
        REQUIRE(config_.init());
        REQUIRE(kernel_.init());
        REQUIRE(metadata.init());
        providers_initialized_ = true;
        REQUIRE(config_.post_init());
        if (kernel_post_init_first)
        {
            post_initialize_kernel();
        }
    }

    void post_initialize_kernel() {
        REQUIRE(kernel_.post_init());
    }

    const std::filesystem::path& runtime_directory() const {
        return directory_;
    }

    unsigned player_online_calls() const {
        return static_cast<unsigned>(std::count_if(messages.begin(), messages.end(),
            [](const std::string& value) { return value.find("Player online, player_id = 42") != std::string::npos; }));
    }

    ark::game_net_module* game{};
    ark::class_meta_module_impl metadata;
    listener_probe network;
    std::vector<std::string> messages;

private:
    template<typename interface_type, typename implementation_type>
    void add(implementation_type& implementation) {
        implementation.set_plugin_manager(&manager_);
        REQUIRE(manager_.add_module(typeid(interface_type).name(), &implementation));
    }

    std::filesystem::path previous_directory_, directory_;
    ark::logger* previous_logger_;
    ark::logger logger_;
    ark::plugin_manager manager_;
    std::unique_ptr<ark::bus_module> bus_;
    std::unique_ptr<ark::guid_module> guid_;
    ark::config_module_impl config_;
    ark::kernel_module_impl kernel_;
    ark::map_module_impl maps_;
    ark::game_plugin owner_;
    bool providers_initialized_{};
};

} // namespace

TEST_CASE("Game init precedes Kernel initialization without registering callbacks") {
    for (const bool kernel_post_init_first : {false, true})
    {
        CAPTURE(kernel_post_init_first);
        game_world world;
        INFO("Fixture directory: " << world.runtime_directory().string());
        // Every real dependency is registered, but Kernel's metadata pointer is
        // still null. This order must be valid for the manager's init stage.
        REQUIRE(world.game->init());
        CHECK(world.network.create_calls == 0);
        world.initialize_providers(kernel_post_init_first);
        REQUIRE(world.metadata.do_class_event(
            42, "Player", ark::entity_event::entity_evt_load_data, ark::data_list_impl{}));
        CHECK(world.player_online_calls() == 0);
        CHECK_FALSE(world.game->post_init()); // Deliberate listener failure after registration.
        CHECK(world.network.create_calls == 1);
        if (!kernel_post_init_first)
        {
            world.post_initialize_kernel();
        }
        REQUIRE(world.metadata.do_class_event(
            42, "Player", ark::entity_event::entity_evt_load_data, ark::data_list_impl{}));
        CHECK(world.player_online_calls() == 1);
    }
}

TEST_CASE("Game missing Player metadata fails before registering callbacks or creating a listener") {
    game_world world(false);
    REQUIRE(world.game->init());
    world.initialize_providers();
    CHECK(world.metadata.find_meta("Player") == nullptr);
    CHECK_FALSE(world.game->post_init());
    CHECK(world.network.create_calls == 0);
    CHECK(world.player_online_calls() == 0);
}
