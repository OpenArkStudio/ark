// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "../network/doctest.h"

#include "ark/kernel/class_meta_module_impl.hpp"
#include "ark/kernel/config_module_impl.hpp"
#include "ark/kernel/data_list_impl.hpp"
#include "ark/kernel/kernel_module_impl.hpp"
#include "ark/kernel/map_module_impl.hpp"

namespace ark {

// Test-only access to the existing delivery callback. The actual serializer and
// send_sync_msg path remain intact; this does not implement a transport.
class kernel_delayed_sync_test_access
{
public:
    static auto& delivery_callbacks(kernel_module_impl& kernel) {
        return kernel.sync_functors_;
    }
};

} // namespace ark

namespace {

class delayed_sync_files
{
public:
    delayed_sync_files()
        : previous_(std::filesystem::current_path())
        , directory_(
              std::filesystem::temp_directory_path() /
              ("ark-delayed-sync-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
        std::filesystem::create_directories(directory_ / "resource/meta");
        std::ofstream output(directory_ / "resource/meta/entity_schema.json");
        output.exceptions(std::ios::badbit | std::ios::failbit);
        output << R"({"format":"ark.entity-schema","version":1,"classes":[
{"name":"Player","fields":[],"tables":[],"containers":[]},
{"name":"Owner","fields":[{"name":"level","index":100,"type":"uint32","mask":["sync_view","sync_self"]}],
"tables":[{"name":"inventory","index":200,"class":"Item","mask":["sync_view","sync_self"]}],
"containers":[{"name":"bag","index":201,"class":"Item","mask":["sync_view","sync_self"]},
{"name":"private_bag","index":202,"class":"Item","mask":["sync_self"]}]},
{"name":"Item","fields":[{"name":"count","index":1,"type":"uint32"}],"tables":[],"containers":[]}],"configs":[]})";
        output.close();
        std::filesystem::current_path(directory_);
    }

    ~delayed_sync_files() {
        std::error_code ignored;
        std::filesystem::current_path(previous_, ignored);
        std::filesystem::remove_all(directory_, ignored);
    }

private:
    std::filesystem::path previous_, directory_;
};

class sync_guid_sequence final : public ark::guid_module
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

struct delivery
{
    ark::data_mask mask;
    ark::guid_t target;
    ark::protobuf::protobuf_delay_entity message;
};

class delayed_sync_world
{
public:
    delayed_sync_world() {
        ark::logger::init(&logger_);
        logger_.set_log_func([](spdlog::level::level_enum, const std::string&) {});
        add<ark::class_meta_module>(metadata_);
        add<ark::config_module>(config_);
        add<ark::kernel_module>(kernel);
        add<ark::map_module>(maps_);
        add<ark::guid_module>(guid_);
        REQUIRE(maps_.init());
        REQUIRE(kernel.init());
        REQUIRE(config_.init());
        REQUIRE(metadata_.init());
        REQUIRE(config_.post_init());
        REQUIRE(kernel.post_init());
        REQUIRE(maps_.create_map(1));
        for (const auto mask : {ark::data_mask::pf_sync_view, ark::data_mask::pf_sync_self})
        {
            ark::kernel_delayed_sync_test_access::delivery_callbacks(
                kernel)[mask] = [this, mask](const ark::guid_t& target, const google::protobuf::Message& message) {
                const auto* delayed = dynamic_cast<const ark::protobuf::protobuf_delay_entity*>(&message);
                REQUIRE(delayed != nullptr);
                deliveries.push_back({mask, target, *delayed});
                if (on_delivery)
                {
                    on_delivery(mask);
                }
                return true;
            };
        }
        owner = kernel.create_entity(10, 1, 0, "Owner", 0, ark::data_list_impl{});
        REQUIRE(owner != nullptr);
    }

    ~delayed_sync_world() {
        on_delivery = {};
        // Retire pending raw-node batches before destroying their entities.
        kernel.update();
        for (const auto id : children_)
        {
            if (kernel.get_entity(id))
            {
                kernel.destroy_entity(id);
            }
        }
        kernel.pre_shut();
        maps_.shut();
        config_.shut();
        metadata_.shut();
        ark::logger::set_instance(nullptr);
    }

    std::shared_ptr<ark::entity> create_child(std::uint32_t container_index = 201) {
        const auto child = kernel.create_container_entity(owner->get_id(), container_index, "Item", 0);
        REQUIRE(child != nullptr);
        children_.push_back(child->get_id());
        return child;
    }

    void flush(std::size_t count = 2) {
        CHECK(deliveries.empty());
        REQUIRE(kernel.update());
        REQUIRE(deliveries.size() == count);
        for (const auto& event : deliveries)
        {
            CHECK(event.target == owner->get_id());
            CHECK(event.message.id() == owner->get_id());
        }
        if (count == 2)
        {
            CHECK(deliveries[0].mask == ark::data_mask::pf_sync_view);
            CHECK(deliveries[1].mask == ark::data_mask::pf_sync_self);
        }
    }

    ark::kernel_module_impl kernel;
    std::shared_ptr<ark::entity> owner;
    std::vector<delivery> deliveries;
    std::function<void(ark::data_mask)> on_delivery;

private:
    template<class Interface, class Implementation>
    void add(Implementation& module) {
        module.set_plugin_manager(&manager_);
        REQUIRE(manager_.add_module(GET_CLASS_NAME(Interface), &module));
    }

    ark::logger logger_;
    delayed_sync_files files_;
    ark::plugin_manager manager_;
    ark::class_meta_module_impl metadata_;
    ark::config_module_impl config_;
    ark::map_module_impl maps_;
    sync_guid_sequence guid_;
    std::vector<ark::guid_t> children_;
};

} // namespace

TEST_CASE("Kernel delayed container-only changes reach existing mask delivery callbacks") {
    delayed_sync_world world;
    const auto bag = world.owner->find_container(201);
    REQUIRE(bag != nullptr);

    SUBCASE("new contained entity retains its full payload") {
        const auto child = world.create_child();
        REQUIRE(child->set_u_int32(1, 7));
        const auto slot = bag->find(child->get_id());
        world.flush();
        for (const auto& event : world.deliveries)
        {
            const auto& payload = event.message.data().datas_container().at(201).datas_value().at(slot);
            CHECK(payload.id() == child->get_id());
            CHECK(payload.data().datas_uint32().at(1) == 7);
            CHECK(event.message.container_entity().at(201).entity_list().empty());
        }
    }

    SUBCASE("already sent parent uses existing entity references") {
        world.owner->update_sent();
        const auto child = world.create_child();
        const auto slot = bag->find(child->get_id());
        world.flush();
        for (const auto& event : world.deliveries)
        {
            CHECK(event.message.container_entity().at(201).entity_list().at(slot) == child->get_id());
            CHECK(event.message.data().datas_container().empty());
        }
    }

    SUBCASE("remove emits the empty slot marker") {
        const auto child = world.create_child();
        const auto slot = bag->find(child->get_id());
        REQUIRE(world.kernel.update());
        world.deliveries.clear();
        REQUIRE(bag->remove(slot));
        world.flush();
        for (const auto& event : world.deliveries)
        {
            CHECK(event.message.container_entity().at(201).entity_list().at(slot) == ark::null_guid);
            CHECK(event.message.destroy_entity().at(201).entity_list().empty());
        }
    }

    SUBCASE("destroy emits both empty slot and sent child destruction markers") {
        const auto child = world.create_child();
        const auto slot = bag->find(child->get_id());
        REQUIRE(world.kernel.update());
        world.deliveries.clear();
        child->update_sent();
        REQUIRE(bag->destroy(slot));
        world.flush();
        for (const auto& event : world.deliveries)
        {
            CHECK(event.message.container_entity().at(201).entity_list().at(slot) == ark::null_guid);
            CHECK(event.message.destroy_entity().at(201).entity_list().at(slot) == ark::null_guid);
        }
    }

    SUBCASE("self-only container retains mask isolation") {
        world.create_child(202);
        world.flush(1);
        CHECK(world.deliveries.front().mask == ark::data_mask::pf_sync_self);
        CHECK(world.deliveries.front().message.data().datas_container().count(202) == 1);
    }

    world.deliveries.clear();
    REQUIRE(world.kernel.update());
    CHECK(world.deliveries.empty());
}

TEST_CASE("Kernel delayed scalar table mixed and empty controls retain existing batching") {
    delayed_sync_world world;
    bool scalar = false;
    bool table = false;
    bool container = false;
    SUBCASE("empty update") {
    }
    SUBCASE("scalar only") {
        scalar = true;
    }
    SUBCASE("table only") {
        table = true;
    }
    SUBCASE("mixed") {
        scalar = table = container = true;
    }

    if (scalar)
    {
        REQUIRE(world.owner->set_u_int32(100, 9));
    }
    if (table)
    {
        const auto inventory = world.owner->find_table(200);
        REQUIRE(inventory != nullptr);
        inventory->clear();
    }
    if (container)
    {
        world.create_child();
    }
    world.flush(scalar || table || container ? 2 : 0);
    for (const auto& event : world.deliveries)
    {
        CHECK(event.message.data().datas_uint32().count(100) == (scalar ? 1 : 0));
        if (scalar)
        {
            CHECK(event.message.data().datas_uint32().at(100) == 9);
        }
        CHECK(event.message.clear_tables_size() == (table ? 1 : 0));
        if (table)
        {
            CHECK(event.message.clear_tables(0) == 200);
        }
        CHECK(event.message.data().datas_container().count(201) == (container ? 1 : 0));
    }
}

TEST_CASE("Kernel delayed callback mutations remain pending until the next flush") {
    delayed_sync_world world;
    world.create_child();
    bool changed = false;
    world.on_delivery = [&](ark::data_mask) {
        if (changed)
        {
            return;
        }
        changed = true;
        REQUIRE(world.owner->set_u_int32(100, 42));
        const auto inventory = world.owner->find_table(200);
        REQUIRE(inventory != nullptr);
        inventory->clear();
        world.create_child(202);
    };

    world.flush();
    CHECK(changed);
    for (const auto& event : world.deliveries)
    {
        CHECK(event.message.data().datas_container().count(201) == 1);
        CHECK(event.message.data().datas_container().count(202) == 0);
        CHECK(event.message.data().datas_uint32().empty());
        CHECK(event.message.clear_tables().empty());
    }
    world.on_delivery = {};
    world.deliveries.clear();
    world.flush();
    for (const auto& event : world.deliveries)
    {
        CHECK(event.message.data().datas_uint32().at(100) == 42);
        REQUIRE(event.message.clear_tables_size() == 1);
        CHECK(event.message.clear_tables(0) == 200);
        CHECK(event.message.data().datas_container().count(201) == 0);
        CHECK(
            event.message.data().datas_container().count(202) == (event.mask == ark::data_mask::pf_sync_self ? 1 : 0));
    }
    world.deliveries.clear();
    REQUIRE(world.kernel.update());
    CHECK(world.deliveries.empty());
}

TEST_CASE("Kernel rejects recursive delayed flushes and retains their new changes") {
    delayed_sync_world world;
    world.create_child();
    bool attempted = false;
    world.on_delivery = [&](ark::data_mask) {
        if (attempted)
        {
            return;
        }
        attempted = true;
        REQUIRE(world.owner->set_u_int32(100, 27));
        CHECK_FALSE(ark::class_call_back_manager::on_delay_sync());
        // Kernel keeps its existing update result rather than propagating the
        // collector's recursive-flush rejection to the application.
        CHECK(world.kernel.update());
    };
    world.flush();
    CHECK(attempted);
    world.on_delivery = {};
    world.deliveries.clear();
    world.flush();
    for (const auto& event : world.deliveries)
    {
        CHECK(event.message.data().datas_uint32().at(100) == 27);
    }
}

TEST_CASE("Kernel delayed flush guard resets after callback exceptions") {
    delayed_sync_world world;
    world.create_child();
    world.on_delivery = [&](ark::data_mask) {
        REQUIRE(world.owner->set_u_int32(100, 63));
        throw std::runtime_error("controlled delayed delivery failure");
    };
    CHECK_THROWS_WITH_AS(world.kernel.update(), "controlled delayed delivery failure", std::runtime_error);
    REQUIRE(world.deliveries.size() == 1);
    world.on_delivery = {};
    world.deliveries.clear();
    world.flush();
    for (const auto& event : world.deliveries)
    {
        CHECK(event.message.data().datas_uint32().at(100) == 63);
        CHECK(event.message.data().datas_container().empty());
    }
    world.deliveries.clear();
    REQUIRE(world.kernel.update());
    CHECK(world.deliveries.empty());
}
