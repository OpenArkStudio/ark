// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#include "../network/doctest.h"

#include "ark/kernel/class_meta_module_impl.hpp"
#include "ark/kernel/config_module_impl.hpp"
#include "ark/kernel/data_list_impl.hpp"
#include "ark/kernel/kernel_module_impl.hpp"
#include "ark/kernel/map_module_impl.hpp"

namespace {

class sequential_guid_module final : public ark::guid_module
{
public:
    ark::guid_t create_guid() override {
        return next_++;
    }

    std::string parse_uid(const ark::guid_t&) override {
        return {};
    }

private:
    ark::guid_t next_{1000};
};

class entity_config_files
{
public:
    entity_config_files()
        : previous_(std::filesystem::current_path())
        , directory_(
              std::filesystem::temp_directory_path() /
              ("ark-entity-config-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
        std::filesystem::create_directories(directory_ / "resource" / "meta");
        write("config_class.config", R"(<xml>
<config id="Player" meta="meta/player_static.config" res="meta/player.data"/>
<config id="item" meta="meta/item_static.config" res="meta/item.data"/>
</xml>)");
        write("entity_class.config", R"(<xml><config meta="meta/entities.config"/></xml>)");
        write("player_static.config", R"(<xml>
<meta field="id" type="uint64"/><meta field="level" type="int"/><meta field="name" type="string"/>
<meta field="config_id" type="uint32"/>
</xml>)");
        write("player.data", R"(<xml><data id="42" level="7" name="configured player" config_id="4045620583"/></xml>)");
        write("item_static.config", R"(<xml>
<meta field="id" type="uint64"/><meta field="level" type="int"/><meta field="comment" type="string"/>
</xml>)");
        write("item.data", R"(<xml><data id="99" level="13" comment="configured item"/></xml>)");
        write("entities.config", R"(<xml>
<meta class="Player" type="class"/>
<meta class="Player" field_name="id" type="uint64"/>
<meta class="Player" field_name="level" type="int"/>
<meta class="Player" field_name="name" type="string"/>
<meta class="Player" field_name="config_id" type="uint32"/>
<meta class="Player" field_name="exp" type="int64"/>
<meta class="Player" field_name="items" type="container" type_class="item"/>
<meta class="item" type="class"/>
<meta class="item" field_name="id" type="uint64"/>
<meta class="item" field_name="level" type="int"/>
<meta class="item" field_name="comment" type="string"/>
</xml>)");
        std::filesystem::current_path(directory_);
    }

    ~entity_config_files() {
        std::error_code ignored;
        std::filesystem::current_path(previous_, ignored);
        std::filesystem::remove_all(directory_, ignored);
    }

private:
    void write(const char* name, const std::string& contents) {
        std::ofstream output(directory_ / "resource" / "meta" / name);
        output.exceptions(std::ios::badbit | std::ios::failbit);
        output << contents;
    }

    std::filesystem::path previous_;
    std::filesystem::path directory_;
};

class configured_world
{
public:
    configured_world() {
        ark::logger::init(&logger_);
        logger_.set_log_func([](spdlog::level::level_enum, const std::string&) {});
        register_module<ark::class_meta_module>(metadata_);
        register_module<ark::config_module>(config_);
        register_module<ark::kernel_module>(kernel);
        register_module<ark::map_module>(map_);
        register_module<ark::guid_module>(guid_);
        REQUIRE(map_.init());
        REQUIRE(config_.init());
        REQUIRE(kernel.init());
        REQUIRE(metadata_.init());
        REQUIRE(config_.post_init());
        REQUIRE(kernel.post_init());
        REQUIRE(map_.create_map(1));
    }

    ~configured_world() {
        for (const auto id : contained_)
        {
            if (kernel.get_entity(id))
            {
                kernel.destroy_entity(id);
            }
        }
        kernel.pre_shut();
        map_.shut();
        config_.shut();
        metadata_.shut();
        ark::logger::set_instance(nullptr);
    }

    std::shared_ptr<ark::entity> create(
        ark::guid_t id, ark::id_type config_id, const std::string& class_name = "Player") {
        return kernel.create_entity(id, 1, 0, class_name, config_id, ark::data_list_impl{});
    }

    std::shared_ptr<ark::entity> create_with_args(ark::guid_t id, ark::id_type config_id, const ark::data_list& args) {
        return kernel.create_entity(id, 1, 0, "Player", config_id, args);
    }

    std::shared_ptr<ark::entity> create_child(ark::guid_t parent, ark::id_type config_id) {
        auto result = kernel.create_container_entity(parent, ark::entity_meta_player::items_index(), "item", config_id);
        if (result)
        {
            contained_.push_back(result->get_id());
        }
        return result;
    }

private:
    template<class Interface, class Implementation>
    void register_module(Implementation& implementation) {
        implementation.set_plugin_manager(&manager_);
        REQUIRE(manager_.add_module(GET_CLASS_NAME(Interface), &implementation));
    }

    ark::logger logger_;
    entity_config_files files_;
    ark::plugin_manager manager_;
    ark::class_meta_module_impl metadata_;
    ark::config_module_impl config_;
    ark::map_module_impl map_;
    sequential_guid_module guid_;
    std::vector<ark::guid_t> contained_;

public:
    ark::kernel_module_impl kernel;
};

ark::protobuf::protobuf_entity_data entity_data(configured_world& world, const std::shared_ptr<ark::entity>& entity) {
    ark::protobuf::protobuf_entity_data data;
    REQUIRE(world.kernel.entity_node_to_protobuf_data(entity, &data));
    return data;
}

struct creation_observer
{
    int on_class_event(const ark::guid_t& id, const std::string&, ark::entity_event event, const ark::data_list&) {
        if (event >= ark::entity_event::entity_evt_pre_load_data &&
            event <= ark::entity_event::entity_evt_data_finished)
        {
            events.push_back(event);
            if (event == ark::entity_event::entity_evt_pre_load_data)
            {
                snapshot_saved = kernel->entity_node_to_protobuf_data(kernel->get_entity(id), &pre_load_data);
            }
        }
        return 0;
    }

    int on_node_change(const ark::guid_t&, const std::string&, std::uint32_t, const ark::data&, const ark::data&) {
        ++node_changes;
        return 0;
    }

    ark::kernel_module* kernel{};
    std::vector<ark::entity_event> events;
    ark::protobuf::protobuf_entity_data pre_load_data;
    bool snapshot_saved{};
    std::size_t node_changes{};
};

} // namespace

TEST_CASE("created entities copy configuration values into independent nodes") {
    configured_world world;
    const auto source = world.kernel.get_static_entity(42);
    REQUIRE(source != nullptr);
    CHECK(source->get_class_name() == "Player");
    CHECK(source->get_int32("level") == 7);
    CHECK(source->get_string("name") == "configured player");

    const auto first = world.create(10, 42);
    const auto second = world.create(11, 42);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    const auto initial_data = entity_data(world, first);
    const auto level = initial_data.datas_int32().find(ark::entity_meta_player::level_index());
    REQUIRE_MESSAGE(level != initial_data.datas_int32().end(), "create_entity must copy the configured level node");
    CHECK(level->second == 7);
    CHECK(first->get_config_id() == 42);
    CHECK(first->get_int32("level") == 7);
    CHECK(first->get_string("name") == "configured player");
    CHECK(second->get_int32("level") == 7);
    CHECK(second->get_string("name") == "configured player");
    REQUIRE(first->set_int32("level", 50));
    REQUIRE(first->set_string("name", "changed player"));
    CHECK(second->get_int32("level") == 7);
    CHECK(second->get_string("name") == "configured player");
    CHECK(source->get_int32("level") == 7);
    CHECK(source->get_string("name") == "configured player");
}

TEST_CASE("configuration uint32 values retain their full range in independent entity nodes") {
    configured_world world;
    constexpr std::uint32_t configured_value = 4045620583u;
    constexpr std::uint32_t changed_value = 2271560481u;
    const auto source = world.kernel.get_static_entity(42);
    REQUIRE(source != nullptr);
    REQUIRE(source->get_u_int32("config_id") == configured_value);

    const auto first = world.create(10, 42);
    const auto second = world.create(11, 42);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    const auto initial_data = entity_data(world, first);
    const auto value = initial_data.datas_uint32().find(ark::entity_meta_player::config_id_index());
    REQUIRE(value != initial_data.datas_uint32().end());
    REQUIRE_MESSAGE(value->second == configured_value, "configured uint32 clone must preserve values above INT32_MAX");
    CHECK(first->get_u_int32("config_id") == configured_value);
    CHECK(second->get_u_int32("config_id") == configured_value);
    CHECK(first->get_config_id() == 42);
    REQUIRE(first->set_u_int32("config_id", changed_value));
    CHECK(first->get_u_int32("config_id") == changed_value);
    CHECK(second->get_u_int32("config_id") == configured_value);
    CHECK(source->get_u_int32("config_id") == configured_value);
}

TEST_CASE("arguments override configuration defaults before the original creation events") {
    creation_observer observer;
    configured_world world;
    observer.kernel = &world.kernel;
    auto& kernel = static_cast<ark::kernel_module&>(world.kernel);
    REQUIRE(kernel.add_class_call_back("Player", &observer, &creation_observer::on_class_event));
    REQUIRE(kernel.add_node_call_back("Player", "level", &observer, &creation_observer::on_node_change));
    REQUIRE(kernel.add_node_call_back("Player", "name", &observer, &creation_observer::on_node_change));
    REQUIRE(kernel.add_node_call_back("Player", "config_id", &observer, &creation_observer::on_node_change));
    REQUIRE(kernel.add_node_call_back("Player", "exp", &observer, &creation_observer::on_node_change));
    ark::data_list_impl args;
    REQUIRE(args.add_int(23));
    REQUIRE(args.add_u_int(ark::entity_meta_player::level_index()));
    REQUIRE(args.add_int64(9876543210LL));
    REQUIRE(args.add_u_int(ark::entity_meta_player::exp_index()));

    const auto created = world.create_with_args(10, 42, args);
    REQUIRE(created != nullptr);
    const auto initial_data = entity_data(world, created);
    const auto name = initial_data.datas_string().find(ark::entity_meta_player::name_index());
    REQUIRE_MESSAGE(
        name != initial_data.datas_string().end(), "arguments must not suppress other configuration defaults");
    CHECK(name->second == "configured player");
    CHECK(created->get_int32("level") == 23);
    CHECK(created->get_int64("exp") == 9876543210LL);
    CHECK(created->get_u_int32("config_id") == 4045620583u);
    REQUIRE(observer.snapshot_saved);
    CHECK(observer.pre_load_data.datas_int32().at(ark::entity_meta_player::level_index()) == 23);
    CHECK(observer.pre_load_data.datas_int64().at(ark::entity_meta_player::exp_index()) == 9876543210LL);
    CHECK(observer.pre_load_data.datas_string().at(ark::entity_meta_player::name_index()) == "configured player");
    CHECK(observer.pre_load_data.datas_uint32().at(ark::entity_meta_player::config_id_index()) == 4045620583u);
    const std::vector<ark::entity_event> expected_events = {ark::entity_event::entity_evt_pre_load_data,
        ark::entity_event::entity_evt_load_data, ark::entity_event::entity_evt_pre_effect_data,
        ark::entity_event::entity_evt_effect_data, ark::entity_event::entity_evt_post_effect_data,
        ark::entity_event::entity_evt_data_finished};
    CHECK(observer.events == expected_events);
    CHECK(observer.node_changes == 0);
    REQUIRE(created->set_int32("level", 24));
    CHECK(observer.node_changes == 1);
    CHECK(world.kernel.get_static_entity(42)->get_int32("level") == 7);
    CHECK(world.kernel.get_static_entity(42)->get_string("name") == "configured player");
}

TEST_CASE("explicit zero and empty initialization arguments retain their values") {
    configured_world world;
    ark::data_list_impl args;
    REQUIRE(args.add_int(0));
    REQUIRE(args.add_u_int(ark::entity_meta_player::level_index()));
    REQUIRE(args.add_string(""));
    REQUIRE(args.add_u_int(ark::entity_meta_player::name_index()));

    const auto created = world.create_with_args(10, 42, args);
    REQUIRE(created != nullptr);
    const auto initial_data = entity_data(world, created);
    REQUIRE(initial_data.datas_uint32().find(ark::entity_meta_player::config_id_index()) !=
            initial_data.datas_uint32().end());
    CHECK(initial_data.datas_uint32().at(ark::entity_meta_player::config_id_index()) == 4045620583u);
    CHECK(initial_data.datas_int32().at(ark::entity_meta_player::level_index()) == 0);
    CHECK(initial_data.datas_string().at(ark::entity_meta_player::name_index()).empty());
    CHECK(world.kernel.get_static_entity(42)->get_int32("level") == 7);
    CHECK(world.kernel.get_static_entity(42)->get_string("name") == "configured player");
}

TEST_CASE("valid repeated arguments use the last value and configuration zero stays argument only") {
    configured_world world;
    ark::data_list_impl repeated;
    REQUIRE(repeated.add_int(20));
    REQUIRE(repeated.add_u_int(ark::entity_meta_player::level_index()));
    REQUIRE(repeated.add_int(30));
    REQUIRE(repeated.add_u_int(ark::entity_meta_player::level_index()));

    const auto configured = world.create_with_args(10, 42, repeated);
    REQUIRE(configured != nullptr);
    CHECK(configured->get_int32("level") == 30);
    const auto configured_data = entity_data(world, configured);
    REQUIRE(configured_data.datas_string().find(ark::entity_meta_player::name_index()) !=
            configured_data.datas_string().end());
    CHECK(configured_data.datas_string().at(ark::entity_meta_player::name_index()) == "configured player");
    CHECK(world.kernel.get_static_entity(42)->get_int32("level") == 7);

    ark::data_list_impl args_only;
    REQUIRE(args_only.add_int(33));
    REQUIRE(args_only.add_u_int(ark::entity_meta_player::level_index()));
    REQUIRE(args_only.add_int64(4567890123LL));
    REQUIRE(args_only.add_u_int(ark::entity_meta_player::exp_index()));
    const auto unconfigured = world.create_with_args(11, 0, args_only);
    REQUIRE(unconfigured != nullptr);
    CHECK(unconfigured->get_config_id() == 0);
    const auto unconfigured_data = entity_data(world, unconfigured);
    REQUIRE(unconfigured_data.datas_int32().size() == 1);
    CHECK(unconfigured_data.datas_int32().at(ark::entity_meta_player::level_index()) == 33);
    REQUIRE(unconfigured_data.datas_int64().size() == 1);
    CHECK(unconfigured_data.datas_int64().at(ark::entity_meta_player::exp_index()) == 4567890123LL);
    CHECK(unconfigured_data.datas_string().empty());
    CHECK(unconfigured_data.datas_uint32().empty());
    CHECK(unconfigured_data.datas_uint64().empty());
}

TEST_CASE("container entities copy configuration values into independent nodes") {
    configured_world world;
    const auto parent = world.create(10, 0);
    REQUIRE(parent != nullptr);
    const auto container = parent->find_container("items");
    REQUIRE(container != nullptr);
    const auto source = world.kernel.get_static_entity(99);
    REQUIRE(source != nullptr);
    CHECK(source->get_class_name() == "item");
    CHECK(source->get_int32("level") == 13);
    CHECK(source->get_string("comment") == "configured item");

    const auto first = world.create_child(parent->get_id(), 99);
    const auto second = world.create_child(parent->get_id(), 99);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    const auto initial_data = entity_data(world, first);
    const auto level = initial_data.datas_int32().find(ark::config_meta_item::level_index());
    REQUIRE_MESSAGE(
        level != initial_data.datas_int32().end(), "create_container_entity must copy the configured level node");
    CHECK(level->second == 13);
    CHECK(first->get_config_id() == 99);
    CHECK(first->get_int32("level") == 13);
    CHECK(first->get_string("comment") == "configured item");
    CHECK(first->get_parent_container() == container);
    CHECK(container->exist(first->get_id()));
    CHECK(container->exist(second->get_id()));
    REQUIRE(first->set_int32("level", 88));
    REQUIRE(first->set_string("comment", "changed item"));
    CHECK(second->get_int32("level") == 13);
    CHECK(second->get_string("comment") == "configured item");
    CHECK(source->get_int32("level") == 13);
    CHECK(source->get_string("comment") == "configured item");
}

TEST_CASE("entity configuration zero and rejected configurations preserve creation boundaries") {
    configured_world world;
    const auto empty = world.create(10, 0);
    REQUIRE(empty != nullptr);
    CHECK(empty->get_config_id() == 0);
    CHECK(entity_data(world, empty).ByteSizeLong() == 0);
    CHECK(world.create(11, 9999) == nullptr);
    CHECK(world.kernel.get_entity(11) == nullptr);
    CHECK(world.create(12, 99) == nullptr);
    CHECK(world.kernel.get_entity(12) == nullptr);
    CHECK(world.kernel.get_static_entity(42)->get_int32("level") == 7);
}

TEST_CASE("container configuration zero and rejected configurations preserve creation boundaries") {
    configured_world world;
    const auto parent = world.create(10, 0);
    REQUIRE(parent != nullptr);
    const auto container = parent->find_container("items");
    REQUIRE(container != nullptr);
    CHECK(world.create_child(parent->get_id(), 9999) == nullptr);
    CHECK(world.create_child(parent->get_id(), 42) == nullptr);
    CHECK(container->first() == 0);
    const auto empty = world.create_child(parent->get_id(), 0);
    REQUIRE(empty != nullptr);
    CHECK(empty->get_config_id() == 0);
    CHECK(entity_data(world, empty).ByteSizeLong() == 0);
    CHECK(empty->get_parent_container() == container);
    CHECK(container->exist(empty->get_id()));
    CHECK(world.kernel.get_static_entity(99)->get_int32("level") == 13);
}

TEST_CASE("public entity custom uint32 updates preserve the full unsigned range") {
    configured_world world;
    const std::shared_ptr<ark::entity> object = world.create(10, 0);
    REQUIRE(object != nullptr);
    REQUIRE(object->add_custom_u_int32("sequence", 0));
    CHECK(object->get_custom_u_int32("sequence") == 0);
    for (const auto value :
        {std::numeric_limits<std::uint32_t>::max(), std::uint32_t{0x80000000}, std::uint32_t{1}, std::uint32_t{0}})
    {
        REQUIRE(object->set_custom_u_int32("sequence", value));
        CHECK(object->get_custom_u_int32("sequence") == value);
        CHECK(object->find_custom_data("sequence"));
    }
}

TEST_CASE("public entity custom uint32 rejects missing names without creating data") {
    configured_world world;
    const std::shared_ptr<ark::entity> object = world.create(10, 0);
    REQUIRE(object != nullptr);
    CHECK_FALSE(object->find_custom_data("missing"));
    CHECK_FALSE(object->set_custom_u_int32("missing", std::numeric_limits<std::uint32_t>::max()));
    CHECK_FALSE(object->find_custom_data("missing"));
    REQUIRE(object->add_custom_u_int32("missing", 7));
    CHECK(object->get_custom_u_int32("missing") == 7);
    REQUIRE(object->remove_custom_data("missing"));
    CHECK_FALSE(object->set_custom_u_int32("missing", 0));
    CHECK_FALSE(object->find_custom_data("missing"));
}

TEST_CASE("public entity custom uint32 rejects other scalar types without changing their values") {
    configured_world world;
    const std::shared_ptr<ark::entity> object = world.create(10, 0);
    REQUIRE(object != nullptr);
    REQUIRE(object->add_custom_bool("boolean", true));
    REQUIRE(object->add_custom_int32("signed32", -17));
    REQUIRE(object->add_custom_int64("signed64", 4567890123LL));
    REQUIRE(object->add_custom_float("float", 1.25F));
    REQUIRE(object->add_custom_double("double", 2.5));
    REQUIRE(object->add_custom_string("string", "preserved"));
    for (const auto* name : {"boolean", "signed32", "signed64", "float", "double", "string"})
    {
        CHECK_FALSE(object->set_custom_u_int32(name, 0));
        CHECK_FALSE(object->set_custom_u_int32(name, std::numeric_limits<std::uint32_t>::max()));
    }
    CHECK(object->get_custom_bool("boolean"));
    CHECK(object->get_custom_int32("signed32") == -17);
    CHECK(object->get_custom_int64("signed64") == 4567890123LL);
    CHECK(object->get_custom_float("float") == 1.25F);
    CHECK(object->get_custom_double("double") == 2.5);
    CHECK(std::string(object->get_custom_string("string")) == "preserved");

    REQUIRE(object->set_custom_bool("boolean", false));
    REQUIRE(object->set_custom_int32("signed32", 23));
    REQUIRE(object->set_custom_int64("signed64", -4567890123LL));
    REQUIRE(object->set_custom_float("float", 3.5F));
    REQUIRE(object->set_custom_double("double", 4.25));
    REQUIRE(object->set_custom_string("string", "updated"));
    CHECK_FALSE(object->get_custom_bool("boolean"));
    CHECK(object->get_custom_int32("signed32") == 23);
    CHECK(object->get_custom_int64("signed64") == -4567890123LL);
    CHECK(object->get_custom_float("float") == 3.5F);
    CHECK(object->get_custom_double("double") == 4.25);
    CHECK(std::string(object->get_custom_string("string")) == "updated");
}
