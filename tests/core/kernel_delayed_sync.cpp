// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include "../network/doctest.h"

#include "ark/kernel/class_meta_module_impl.hpp"
#include "ark/kernel/config_module_impl.hpp"
#include "ark/kernel/data_list_impl.hpp"
#include "ark/kernel/kernel_module_impl.hpp"
#include "ark/kernel/map_module_impl.hpp"
#include "ark/kernel/nested_stream.hpp"

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

TEST_CASE("Kernel container swaps preserve child ownership parent bindings and callback order") {
    using operation = ark::container_op_type;
    using record = std::tuple<std::uint32_t, operation, std::uint32_t, std::uint32_t>;

    struct observer
    {
        std::vector<record> events;

        int on_event(const ark::guid_t&, std::uint32_t container, operation op, std::uint32_t from, std::uint32_t to) {
            events.emplace_back(container, op, from, to);
            return 0;
        }
    } observed;

    auto& events = observed.events;
    delayed_sync_world world;
    const auto source = world.owner->find_container(201);
    const auto destination = world.owner->find_container(202);
    REQUIRE(source != nullptr);
    REQUIRE(destination != nullptr);
    const auto first = world.create_child();
    REQUIRE(first->set_u_int32(1, 17));
    const auto first_slot = source->find(first->get_id());
    REQUIRE(first_slot == 1);
    const std::weak_ptr<ark::entity> first_lifetime = first;
    for (const auto index : {201u, 202u})
    {
        REQUIRE(static_cast<ark::kernel_module&>(world.kernel)
                    .add_container_call_back("Owner", index, &observed, &observer::on_event, 100));
    }

    SUBCASE("same container move into an empty slot") {
        REQUIRE(source->swap(first_slot, 7u));
        CHECK(source->find(first_slot) == nullptr);
        CHECK(source->find(7u) == first);
        CHECK(first->get_parent_container() == source);
        CHECK(events == std::vector<record>{{201u, operation::op_swap, first_slot, 7u}});
    }
    SUBCASE("same container exchange occupied slots") {
        const auto second = world.create_child();
        const auto second_slot = source->find(second->get_id());
        events.clear();
        REQUIRE(source->swap(first_slot, second_slot));
        CHECK(source->find(first_slot) == second);
        CHECK(source->find(second_slot) == first);
        CHECK(first->get_parent_container() == source);
        CHECK(second->get_parent_container() == source);
        CHECK(events == std::vector<record>{{201u, operation::op_swap, first_slot, second_slot}});
    }
    SUBCASE("cross container move into an empty slot") {
        REQUIRE(destination->swap(source, first_slot, 7u));
        CHECK(source->find(first_slot) == nullptr);
        CHECK(destination->find(7u) == first);
        CHECK(first->get_parent_container() == destination);
        CHECK(events ==
              std::vector<record>{{201u, operation::op_remove, first_slot, 0u}, {202u, operation::op_place, 7u, 0u}});
    }
    SUBCASE("cross container exchange occupied slots") {
        const auto second = world.create_child(202);
        REQUIRE(second->set_u_int32(1, 23));
        const auto second_slot = destination->find(second->get_id());
        const std::weak_ptr<ark::entity> second_lifetime = second;
        events.clear();
        REQUIRE(destination->swap(source, first_slot, second_slot));
        CHECK(source->find(first_slot) == second);
        CHECK(destination->find(second_slot) == first);
        CHECK(first->get_parent_container() == destination);
        CHECK(second->get_parent_container() == source);
        CHECK(second->get_u_int32(1) == 23);
        CHECK_FALSE(second_lifetime.expired());
        CHECK(events == std::vector<record>{{201u, operation::op_remove, first_slot, 0u},
                            {202u, operation::op_remove, second_slot, 0u}, {201u, operation::op_place, first_slot, 0u},
                            {202u, operation::op_place, second_slot, 0u}});
    }
    CHECK(first->get_u_int32(1) == 17);
    CHECK_FALSE(first_lifetime.expired());
    REQUIRE(world.kernel.update());
}

TEST_CASE("Real Kernel captures converge through trusted nested streams across rows child moves and destruction") {
    using namespace ark::entity_sync;
    std::shared_ptr<const nested_baseline> retained;
    std::vector<std::shared_ptr<const nested_delta>> packets;
    nested_stream_binding trusted;
    nested_snapshot final_state;
    {
        delayed_sync_world world;
        auto* table = world.owner->find_table(200);
        auto* row = table->add_row(1);
        REQUIRE(row != nullptr);
        REQUIRE(row->set_u_int32(1, 3));
        const auto child = world.create_child();
        REQUIRE(child->set_u_int32(1, 9));
        const auto id = child->get_id();
        nested_projection selected{"owner/v1", world.owner->get_id(),
            {"Owner", {100}, {{200, {1}}},
                {{201, {{id, {"Item", {1}, {}, {}}}}}, {202, {{id, {"Item", {1}, {}, {}}}}}}}};
        const auto initial = world.kernel.capture_nested_snapshot(*world.owner, selected);
        trusted = {"owner/v1", world.owner->get_id(), initial.root.incarnation, 1,
            {"Owner", {{100, nested_scalar_type::uint32}}, {{200, {"Item", {{1, nested_scalar_type::uint32}}}}},
                {{201, {{id, {"Item", {{1, nested_scalar_type::uint32}}, {}, {}}}}},
                    {202, {{id, {"Item", {{1, nested_scalar_type::uint32}}, {}, {}}}}}}}};
        nested_publisher publisher(trusted, {1, 1, initial});
        nested_replica replica(trusted);
        retained = publisher.baseline();
        REQUIRE(replica.apply(*retained) == nested_apply_result::applied);
        auto advance = [&] {
            auto snapshot = world.kernel.capture_nested_snapshot(*world.owner, selected);
            const auto packet = publisher.update(snapshot);
            REQUIRE(packet != nullptr);
            REQUIRE(replica.apply(*packet) == nested_apply_result::applied);
            CHECK(*replica.state() == snapshot);
            packets.push_back(packet);
        };
        auto hidden_projection = selected;
        hidden_projection.root.containers.at(201).clear();
        hidden_projection.root.containers.at(202).clear();
        auto restricted_binding = trusted;
        restricted_binding.root.containers.at(201).clear();
        restricted_binding.root.containers.at(202).clear();
        nested_publisher restricted(
            restricted_binding, {1, 1, world.kernel.capture_nested_snapshot(*world.owner, hidden_projection)});
        REQUIRE(child->set_u_int32(1, 8));
        CHECK_FALSE(restricted.update(world.kernel.capture_nested_snapshot(*world.owner, hidden_projection)));
        CHECK(restricted.baseline()->revision == 1);
        REQUIRE(world.owner->set_u_int32(100, 11));
        REQUIRE(row->set_u_int32(1, 4));
        advance();
        REQUIRE(world.kernel.update());
        REQUIRE(table->remove_row(1));
        row = table->add_row(1);
        REQUIRE(row != nullptr);
        REQUIRE(row->set_u_int32(1, 4));
        advance();
        CHECK(packets.back()->root.tables.at(200).at(1).incarnation >
              packets.back()->root.tables.at(200).at(1).required_incarnation);
        REQUIRE(world.kernel.update());
        table->clear();
        row = table->add_row(2);
        REQUIRE(row != nullptr);
        REQUIRE(row->set_u_int32(1, 4));
        advance();
        const auto source = world.owner->find_container(201);
        const auto destination = world.owner->find_container(202);
        REQUIRE(source->remove(id));
        REQUIRE(destination->place(7, child));
        advance();
        CHECK(packets.back()->root.fields.empty());
        CHECK(packets.back()->root.tables.empty());
        CHECK(packets.back()->root.containers.size() == 2);
        REQUIRE(world.kernel.destroy_entity(id));
        advance();
        final_state = *replica.state();
        CHECK(final_state.root.containers.at(202).empty());
    }
    nested_replica replay(trusted);
    REQUIRE(replay.apply(*retained) == nested_apply_result::applied);
    for (const auto& packet : packets)
    {
        REQUIRE(replay.apply(*packet) == nested_apply_result::applied);
    }
    CHECK(*replay.state() == final_state);
    CHECK(
        std::get<std::uint32_t>(retained->snapshot.root.containers.at(201).begin()->second.scalars.fields.at(1)) == 9);
}
