// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>

#include "../network/doctest.h"

#include "ark/kernel/class_meta_module_impl.hpp"
#include "ark/kernel/config_module_impl.hpp"
#include "ark/kernel/data_list_impl.hpp"
#include "ark/kernel/entity_json.hpp"
#include "ark/kernel/kernel_module_impl.hpp"
#include "ark/kernel/map_module_impl.hpp"

namespace {

std::string schema() {
    return R"({"format":"ark.entity-schema","version":1,"classes":[
{"name":"Player","fields":[],"tables":[],"containers":[]},
{"name":"GuestPlayer","fields":[
{"name":"id","index":99,"type":"uint32"},{"name":"name","index":100,"type":"string","mask":["sync_view","sync_self"]},
{"name":"x","index":101,"type":"double"},{"name":"y","index":102,"type":"double"},
{"name":"move_seq","index":103,"type":"uint32"},{"name":"hp","index":104,"type":"uint32","mask":["save","real_time"]},
{"name":"mp","index":105,"type":"uint32"},{"name":"hp_max","index":106,"type":"uint32"},
{"name":"mp_max","index":107,"type":"uint32"},{"name":"action_seq","index":108,"type":"uint32"},
{"name":"owner","index":109,"type":"guid"},{"name":"big_count","index":110,"type":"uint64"}],
"tables":[{"name":"inventory","index":200,"class":"Item","mask":["sync_view","save"]}],
"containers":[{"name":"bag","index":201,"class":"Item","mask":["save"]},
{"name":"stash","index":202,"class":"Item","mask":["save"]}]},
{"name":"Item","fields":[{"name":"id","index":1,"type":"uint32"},{"name":"count","index":2,"type":"uint32"}],"tables":[],"containers":[]}],
"configs":[{"class":"GuestPlayer","rows":[{"id":1,"name":"Configured guest","x":80.0,"y":100.0,"move_seq":0,
"hp":100,"mp":90,"hp_max":100,"mp_max":100,"action_seq":0,"owner":"42","big_count":18446744073709551615}]},
{"class":"Item","rows":[{"id":2,"count":7}]}]})";
}

class resource_files
{
public:
    resource_files()
        : previous_(std::filesystem::current_path())
        , directory_(
              std::filesystem::temp_directory_path() /
              ("ark-json-entities-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
        std::filesystem::create_directories(directory_ / "resource/meta");
        std::filesystem::current_path(directory_);
        write("entity_schema.json", schema());
        // Valid legacy input would succeed if malformed selected JSON incorrectly fell back.
        write("config_class.config", "<xml/>");
        write("entity_class.config", "<xml><config meta=\"meta/legacy.config\"/></xml>");
        write("legacy.config", "<xml><meta class=\"Player\" type=\"class\"/></xml>");
    }

    ~resource_files() {
        std::error_code ignored;
        std::filesystem::current_path(previous_, ignored);
        std::filesystem::remove_all(directory_, ignored);
    }

    void write(const std::string& name, const std::string& contents) {
        std::ofstream stream(directory_ / "resource/meta" / name);
        stream.exceptions(std::ios::badbit | std::ios::failbit);
        stream << contents;
    }

private:
    std::filesystem::path previous_, directory_;
};

class guid_sequence final : public ark::guid_module
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

class json_world
{
public:
    explicit json_world(bool replace_after_metadata = false, bool omit_player = false) {
        ark::logger::init(&logger_);
        logger_.set_log_func([](spdlog::level::level_enum, const std::string&) {});
        if (omit_player)
        {
            auto document = ark::entity_json::parse(schema());
            document.classes.erase(document.classes.begin());
            files_.write("entity_schema.json", ark::entity_json::serialize(document));
        }
        add<ark::class_meta_module>(metadata);
        add<ark::config_module>(config);
        add<ark::kernel_module>(kernel);
        add<ark::map_module>(maps_);
        add<ark::guid_module>(guid_);
        REQUIRE(maps_.init());
        REQUIRE(kernel.init());
        REQUIRE(config.init());
        REQUIRE(metadata.init());
        if (replace_after_metadata)
        {
            files_.write("entity_schema.json", "{");
        }
        REQUIRE(config.post_init());
        REQUIRE(kernel.post_init());
        REQUIRE(maps_.create_map(1));
    }

    ~json_world() {
        kernel.pre_shut();
        maps_.shut();
        config.shut();
        metadata.shut();
        ark::logger::set_instance(nullptr);
    }

    std::shared_ptr<ark::entity> create(
        ark::guid_t id, const ark::data_list& args = ark::data_list_impl{}, ark::id_type profile = 1) {
        return kernel.create_entity(id, 1, 0, "GuestPlayer", profile, args);
    }

    ark::class_meta_module_impl metadata;
    ark::config_module_impl config;
    ark::kernel_module_impl kernel;

private:
    template<class Interface, class Implementation>
    void add(Implementation& module) {
        module.set_plugin_manager(&manager_);
        REQUIRE(manager_.add_module(GET_CLASS_NAME(Interface), &module));
    }

    ark::logger logger_;
    resource_files files_;
    ark::plugin_manager manager_;
    ark::map_module_impl maps_;
    guid_sequence guid_;
};
} // namespace

TEST_CASE(
    "Kernel JSON installs explicit attributes and configured defaults with args overriding only supplied fields") {
    json_world world;
    const auto meta = world.metadata.find_meta("GuestPlayer");
    REQUIRE(meta != nullptr);
    CHECK(meta->get_index("id") == 99);
    CHECK(meta->get_index("name") == 100);
    CHECK(meta->get_index("action_seq") == 108);
    CHECK(meta->find_data_meta(104)->have_mask(ark::data_mask::pf_save));
    CHECK(meta->find_data_meta(104)->have_mask(ark::data_mask::pf_real_time));
    CHECK(ark::meta_name_index::get_index("GuestPlayer", "hp") == 0); // No generated header needed.
    const auto profile = world.config.find_static_entity(1);
    REQUIRE(profile != nullptr);
    CHECK(profile->get_u_int32(104) == 100);
    CHECK(profile->get_string(100) == "Configured guest");
    CHECK(profile->get_u_int64("big_count") == std::numeric_limits<std::uint64_t>::max());
    ark::data_list_impl args;
    REQUIRE(args.add_string("Argument guest"));
    REQUIRE(args.add_u_int(100));
    REQUIRE(args.add_double(12.5));
    REQUIRE(args.add_u_int(101));
    const auto first = world.create(10, args);
    REQUIRE(first != nullptr);
    CHECK(first->get_string("name") == "Argument guest");
    CHECK(first->get_double("x") == 12.5);
    CHECK(first->get_double("y") == 100.0);
    CHECK(first->get_u_int32("hp") == 100);
    CHECK(first->get_u_int32("mp") == 90);
    CHECK(first->get_u_int32("move_seq") == 0);
    CHECK(first->get_guid("owner") == 42);
    CHECK(first->get_u_int64("big_count") == std::numeric_limits<std::uint64_t>::max());
    REQUIRE(first->set_u_int32("hp", 77));
    const auto second = world.create(11);
    REQUIRE(second != nullptr);
    CHECK(second->get_u_int32("hp") == 100);
    CHECK(profile->get_u_int32("hp") == 100);
    const auto unconfigured = world.create(12, ark::data_list_impl{}, 0);
    REQUIRE(unconfigured != nullptr);
    static_cast<void>(unconfigured->set_u_int32("hp", 0)); // Existing lazy-node initialization.
    CHECK(unconfigured->get_u_int32("hp") == 0);
}

TEST_CASE("Kernel JSON preserves table column schemas masks and configured container entity ownership") {
    json_world world;
    const auto parent = world.create(10);
    REQUIRE(parent != nullptr);
    auto* inventory = parent->find_table("inventory");
    REQUIRE(inventory != nullptr);
    auto* row = inventory->add_row(0);
    REQUIRE(row != nullptr);
    REQUIRE(row->set_u_int32("count", 9));
    CHECK(row->get_u_int32(2) == 9);
    const auto meta = world.metadata.find_meta("GuestPlayer");
    REQUIRE(meta->find_table_meta(200) != nullptr);
    CHECK(meta->find_table_meta(200)->have_mask(ark::data_mask::pf_save));
    CHECK(meta->find_container_meta(201)->get_mask().test(static_cast<std::size_t>(ark::data_mask::pf_save)));
    const auto child = world.kernel.create_container_entity(parent->get_id(), 201, "Item", 2);
    REQUIRE(child != nullptr);
    CHECK(child->get_u_int32("count") == 7);
    REQUIRE(child->get_parent_container() != nullptr);
    const auto child_id = child->get_id();
    CHECK(parent->find_container("bag")->find(child_id) > 0);
    REQUIRE(world.kernel.destroy_entity(child_id));
    CHECK(world.kernel.get_entity(child_id) == nullptr);
    const auto second_child = world.kernel.create_container_entity(parent->get_id(), 202, "Item", 2);
    REQUIRE(second_child != nullptr);
    REQUIRE(world.kernel.destroy_entity(second_child->get_id()));
    CHECK(world.kernel.get_entity(second_child->get_id()) == nullptr);
    REQUIRE(world.kernel.destroy_entity(parent->get_id()));
}

TEST_CASE("Kernel JSON static records use the validated startup snapshot without rereading a replaced file") {
    json_world world(true, true);
    CHECK(world.metadata.find_meta("Player") == nullptr);
    const auto entity = world.create(10);
    REQUIRE(entity != nullptr);
    CHECK(entity->get_u_int32("hp") == 100);
    CHECK(entity->get_string("name") == "Configured guest");
}

TEST_CASE("Malformed selected Kernel JSON fails startup instead of silently loading valid XML") {
    resource_files files;
    ark::logger logger;
    ark::logger::init(&logger);
    logger.set_log_func([](spdlog::level::level_enum, const std::string&) {});
    for (const auto& contents :
        {std::string("{"), std::string(R"({"format":"wrong","version":1,"classes":[],"configs":[]})")})
    {
        files.write("entity_schema.json", contents);
        ark::class_meta_module_impl metadata;
        try
        {
            static_cast<void>(metadata.init());
            FAIL("selected invalid JSON did not fail startup");
        }
        catch (const ark::entity_json::error& error)
        {
            CHECK(error.source().find("resource") != std::string::npos);
            CHECK(std::string(error.what()).find("entity_schema.json") != std::string::npos);
        }
    }
    ark::logger::set_instance(nullptr);
}
