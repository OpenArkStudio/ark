// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "../network/doctest.h"

#include "ark/kernel/class_meta_module.hpp"
#include "ark/kernel/component.hpp"
#include "ark/kernel/config_module_impl.hpp"
#include "ark/kernel/data_list_impl.hpp"
#include "ark/kernel/entity_impl.hpp"
#include "ark/kernel/event_manager_impl.hpp"
#include "ark/kernel/kernel_module_impl.hpp"
#include "ark/kernel/map_module_impl.hpp"

namespace {

// Exercise registration against the real metadata/callback managers without
// loading external metadata or installing process-wide sync callbacks.
class callback_metadata final : public ark::class_meta_module {
public:
    callback_metadata() {
        auto owner = std::make_shared<ark::class_meta>("Owner");
        owner->create_data_meta("level", 10)->set_type(ark::data_type::dt_int32);
        auto item = std::make_shared<ark::class_meta>("Item");
        item->create_data_meta("count", 1)->set_type(ark::data_type::dt_int32);
        owner->create_table_meta("inventory", 20)->set_class_meta(item);
        owner->create_container_meta("bag", 30, "Item", {});
        owner->create_container_meta("spare_bag", 31, "Item", {});
        classes.insert("Owner", owner);
    }

    bool load() override {
        return true;
    }

    bool add_class_call_back(const std::string& name, ark::class_event_functor&& cb, int32_t prio) override {
        return find_meta(name)->get_class_call_back_manager()->add_class_call_back(std::move(cb), prio);
    }

    bool do_class_event(const ark::guid_t& self, const std::string& name, ark::entity_event event,
        const ark::data_list& args) override {
        return find_meta(name)->get_class_call_back_manager()->on_class_event(self, name, event, args);
    }

    std::shared_ptr<ark::class_meta> find_meta(const std::string& name) const override {
        const auto found = classes.find(name);
        return found == classes.end() ? nullptr : found->second;
    }

    const ark::class_meta_manager::class_meta_list& get_meta_list() const override {
        return classes;
    }

    std::shared_ptr<ark::class_meta> find_static_meta(const std::string&) const override {
        return nullptr;
    }

    const ark::class_meta_manager::class_meta_list& get_static_meta_list() const override {
        return empty;
    }

    ark::class_meta_manager::class_meta_list classes, empty;
};

class callback_guid final : public ark::guid_module {
public:
    ark::guid_t create_guid() override {
        return 100;
    }

    std::string parse_uid(const ark::guid_t&) override {
        return {};
    }
};

class callback_world {
public:
    callback_world() {
        ark::logger::init(&logger_);
        logger_.set_log_func([](spdlog::level::level_enum, const std::string&) {});
        add<ark::class_meta_module>(metadata);
        add<ark::kernel_module>(implementation);
        add<ark::config_module>(config_);
        add<ark::map_module>(maps_);
        add<ark::guid_module>(guid_);
        REQUIRE(implementation.init());
        REQUIRE(maps_.init());
        REQUIRE(maps_.create_map(1));
    }

    ~callback_world() {
        ark::logger::set_instance(nullptr);
    }

    callback_metadata metadata;
    ark::kernel_module_impl implementation;

private:
    template<typename interface_type, typename implementation_type>
    void add(implementation_type& instance) {
        instance.set_plugin_manager(&manager_);
        REQUIRE(manager_.add_module(GET_CLASS_NAME(interface_type), &instance));
    }

    ark::logger logger_;
    ark::plugin_manager manager_;
    ark::config_module_impl config_;
    ark::map_module_impl maps_;
    callback_guid guid_;
};

class callback_receiver {
public:
    callback_receiver() = default;
    callback_receiver(const callback_receiver&) = delete;
    callback_receiver& operator=(const callback_receiver&) = delete;

    int on_class(
        const ark::guid_t& self, const std::string& name, ark::entity_event event, const ark::data_list& args) {
        id = self;
        class_name = &name;
        class_event = event;
        arguments = &args;
        ++calls;
        return 17;
    }

    int on_event(const ark::guid_t& self, int event, const ark::data_list& args) {
        id = self;
        event_id = event;
        arguments = &args;
        ++calls;
        if (throw_on_event) {
            throw std::runtime_error("receiver failure");
        }
        return 17;
    }

    int on_node(const ark::guid_t& self, const std::string& name, uint32_t index, const ark::data& old_data,
        const ark::data& new_data) {
        id = self;
        node_name = name;
        node_index = index;
        old_value = old_data.get_int();
        new_value = new_data.get_int();
        if (order) {
            order->push_back(tag);
        }
        ++calls;
        return 17;
    }

    int on_table(const ark::guid_t& self, const ark::table_event_data& event, const ark::data& old_data,
        const ark::data& new_data) {
        id = self;
        table_event = event;
        if (event.op_type == static_cast<size_t>(ark::table_op_type::table_update)) {
            old_value = old_data.get_int();
            new_value = new_data.get_int();
        }
        ++calls;
        return 17;
    }

    int on_container(
        const ark::guid_t& self, uint32_t index, ark::container_op_type op, uint32_t source, uint32_t destination) {
        id = self;
        container_index = index;
        container_op = op;
        source_index = source;
        destination_index = destination;
        ++calls;
        return 17;
    }

    int on_scene(const ark::guid_t& self, int map, int instance) {
        id = self;
        map_id = map;
        instance_id = instance;
        ++calls;
        return 17;
    }

    int on_move(const ark::guid_t& self, const ark::vector3d& previous, const ark::vector3d& current) {
        id = self;
        old_position = &previous;
        new_position = &current;
        ++calls;
        return 17;
    }

    std::vector<int>* order{};
    int tag{};
    unsigned calls{};
    ark::guid_t id{};
    int event_id{}, old_value{}, new_value{}, map_id{}, instance_id{};
    uint32_t node_index{}, container_index{}, source_index{}, destination_index{};
    std::string node_name;
    const std::string* class_name{};
    const ark::data_list* arguments{};
    const ark::vector3d* old_position{};
    const ark::vector3d* new_position{};
    ark::entity_event class_event{};
    ark::container_op_type container_op{};
    ark::table_event_data table_event{};
    bool throw_on_event{};
};

class cloned_component final : public ark::component {
public:
    cloned_component()
        : component(0, "clone") {
    }

    using component::create_new_instance;

protected:
    std::shared_ptr<ark::component> create_new_instance() override {
        return std::make_shared<cloned_component>();
    }
};

} // namespace

TEST_CASE("Kernel event adapter retains receiver identity and borrowed arguments across callback copies") {
    callback_receiver receiver;
    ark::event_manager_impl implementation(77);
    ark::event_manager& events = implementation;
    REQUIRE(events.add_event_call_back(9, &receiver, &callback_receiver::on_event));
    ark::data_list_impl arguments;
    arguments.add_int(42);
    REQUIRE(events.do_event(9, arguments));
    REQUIRE(events.do_event(9, arguments));
    CHECK(receiver.calls == 2);
    CHECK(receiver.id == 77);
    CHECK(receiver.event_id == 9);
    CHECK(receiver.arguments == &arguments);
    receiver.throw_on_event = true;
    CHECK_THROWS_AS(events.do_event(9, arguments), std::runtime_error);
    REQUIRE(events.remove_event_call_back(9));
    events.update();
    CHECK_FALSE(events.do_event(9, arguments));
}

TEST_CASE("Kernel entity event adapter stores a callable after member registration returns") {
    callback_world world;
    ark::kernel_module& kernel = world.implementation;
    const auto entity = kernel.create_entity(77, 1, 0, "Owner", 0, ark::data_list_impl{});
    REQUIRE(entity != nullptr);
    callback_receiver receiver;
    REQUIRE(kernel.add_event_call_back(77, 9, &receiver, &callback_receiver::on_event));
    ark::data_list_impl arguments;
    REQUIRE(kernel.do_event(77, 9, arguments));
    CHECK(receiver.calls == 1);
    CHECK(receiver.id == 77);
    CHECK(receiver.event_id == 9);
    CHECK(receiver.arguments == &arguments);
}

TEST_CASE("Kernel member adapters preserve scalar and table callback routing by name and index") {
    callback_world world;
    ark::kernel_module& kernel = world.implementation;
    callback_receiver by_name, by_index;
    std::vector<int> order;
    by_name.order = by_index.order = &order;
    by_name.tag = 1;
    by_index.tag = 2;
    REQUIRE(kernel.add_node_call_back("Owner", "level", &by_name, &callback_receiver::on_node, -2));
    REQUIRE(kernel.add_node_call_back("Owner", 10, &by_index, &callback_receiver::on_node, 3));
    REQUIRE(kernel.add_table_call_back("Owner", "inventory", &by_name, &callback_receiver::on_table, -2));
    REQUIRE(kernel.add_table_call_back("Owner", 20, &by_index, &callback_receiver::on_table, 3));
    ark::entity_impl entity(world.metadata.find_meta("Owner"), 77, 0, 1, 0, ark::data_list_impl{});
    REQUIRE(entity.set_int32(10, 13));
    REQUIRE(entity.set_int32(10, 21));
    for (const auto* receiver : {&by_name, &by_index}) {
        CHECK(receiver->calls == 2);
        CHECK(receiver->id == 77);
        CHECK(receiver->node_name == "level");
        CHECK(receiver->node_index == 10);
        CHECK(receiver->old_value == 13);
        CHECK(receiver->new_value == 21);
    }
    CHECK(order == std::vector<int>{1, 2, 1, 2});
    auto* table = entity.find_table(20);
    REQUIRE(table != nullptr);
    auto* row = table->add_row(7);
    REQUIRE(row != nullptr);
    REQUIRE(row->set_int32(1, 4));
    REQUIRE(row->set_int32(1, 9));
    for (const auto* receiver : {&by_name, &by_index}) {
        CHECK(receiver->calls == 5);
        CHECK(receiver->id == 77);
        CHECK(receiver->table_event.table_index == 20);
        CHECK(receiver->table_event.row == 7);
        CHECK(receiver->table_event.data_index == 1);
        CHECK(receiver->old_value == 4);
        CHECK(receiver->new_value == 9);
    }
}

TEST_CASE("Kernel member adapters preserve class scene move and copied container registrations") {
    callback_world world;
    ark::kernel_module& kernel = world.implementation;
    callback_receiver class_receiver, common_class, enter, leave, move, container, common_container;
    REQUIRE(kernel.add_class_call_back("Owner", &class_receiver, &callback_receiver::on_class));
    REQUIRE(kernel.add_common_class_event(&common_class, &callback_receiver::on_class));
    REQUIRE(kernel.add_enter_scene_event("Owner", &enter, &callback_receiver::on_scene));
    REQUIRE(kernel.add_leave_scene_event("Owner", &leave, &callback_receiver::on_scene));
    REQUIRE(kernel.add_move_event("Owner", &move, &callback_receiver::on_move));
    REQUIRE(kernel.add_container_call_back("Owner", 30, &container, &callback_receiver::on_container));
    REQUIRE(kernel.add_common_container_call_back(&common_container, &callback_receiver::on_container));
    auto callbacks = world.metadata.find_meta("Owner")->get_class_call_back_manager();
    const ark::guid_t self = 77;
    const std::string name = "Owner";
    ark::data_list_impl arguments;
    REQUIRE(callbacks->on_class_event(self, name, ark::entity_event::entity_evt_all_finished, arguments));
    for (const auto* receiver : {&class_receiver, &common_class}) {
        CHECK(receiver->calls == 1);
        CHECK(receiver->id == self);
        CHECK(receiver->class_name == &name);
        CHECK(receiver->arguments == &arguments);
        CHECK(receiver->class_event == ark::entity_event::entity_evt_all_finished);
    }
    REQUIRE(callbacks->on_enter_scene_event(self, 5, 8));
    REQUIRE(callbacks->on_leave_scene_event(self, 6, 9));
    CHECK(enter.calls == 1);
    CHECK(enter.map_id == 5);
    CHECK(enter.instance_id == 8);
    CHECK(leave.calls == 1);
    CHECK(leave.map_id == 6);
    CHECK(leave.instance_id == 9);
    const ark::vector3d previous{1, 2, 3}, current{4, 5, 6};
    REQUIRE(callbacks->on_move_event(self, previous, current));
    CHECK(move.calls == 1);
    CHECK(move.old_position == &previous);
    CHECK(move.new_position == &current);
    REQUIRE(callbacks->on_container_call_back(self, 30, {}, ark::container_op_type::op_swap, 2, 4));
    for (const auto* receiver : {&container, &common_container}) {
        CHECK(receiver->calls == 1);
        CHECK(receiver->id == self);
        CHECK(receiver->container_index == 30);
        CHECK(receiver->container_op == ark::container_op_type::op_swap);
        CHECK(receiver->source_index == 2);
        CHECK(receiver->destination_index == 4);
    }
    REQUIRE(callbacks->on_container_call_back(self, 31, {}, ark::container_op_type::op_swap, 7, 9));
    CHECK(container.calls == 1);
    CHECK(common_container.calls == 2);
    CHECK(common_container.container_index == 31);
    CHECK(common_container.source_index == 7);
    CHECK(common_container.destination_index == 9);
}

TEST_CASE("Kernel component clone dispatch retains typed and unrelated target behavior") {
    cloned_component component;
    auto clone = component.create_new_instance<cloned_component>();
    REQUIRE(clone != nullptr);
    CHECK(clone.get() != &component);
    CHECK(component.create_new_instance<callback_receiver>() == nullptr);
}
