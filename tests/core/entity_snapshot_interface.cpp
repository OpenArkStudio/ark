// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <memory>
#include <type_traits>

#include "../network/doctest.h"

#include "ark/kernel/data_list_impl.hpp"
#include "ark/kernel/entity_impl.hpp"
#include "ark/kernel/kernel_module_impl.hpp"

namespace {
using namespace ark;

// An external source implementation of the original pure interface, without a
// capture override. The newly added default must preserve its concreteness.
class legacy_snapshot_kernel : public ark::kernel_module
{
public:
    bool do_event(
        const guid_t& self, const std::string& name, entity_event event, const data_list& value_list) override {
        return {};
    }

    bool do_event(const guid_t& self, const int event_id, const data_list& value_list) override {
        return {};
    }

    std::shared_ptr<entity> create_entity(const guid_t& self, const int map_id, const int map_instance_id,
        const std::string& class_name, const id_type config_id, const data_list& args) override {
        return {};
    }

    std::shared_ptr<entity> create_container_entity(const guid_t& self, const uint32_t container_index,
        const std::string& class_name, const id_type config_id) override {
        return {};
    }

    std::shared_ptr<entity> get_entity(const guid_t& self) override {
        return {};
    }

    std::shared_ptr<static_entity> get_static_entity(const id_type config_id) override {
        return {};
    }

    bool destroy_entity(const guid_t& self) override {
        return {};
    }

    bool destroy_all() override {
        return {};
    }

    bool exist(const guid_t& self) override {
        return {};
    }

    bool entity_to_db_data(const guid_t& self, ark::protobuf::protobuf_db_entity& protobuf_data) override {
        return {};
    }

    std::shared_ptr<entity> create_entity(const ark::protobuf::protobuf_db_entity& protobuf_data) override {
        return {};
    }

    bool send_custom_message(const guid_t& target, const uint32_t msg_id, const data_list& args) override {
        return {};
    }

    bool log_info(const guid_t& ident) override {
        return {};
    }

    int log_object_data(const guid_t& guid) override {
        return {};
    }

    bool node_to_protobuf_data(
        const uint32_t index, const ark::data& data, ark::protobuf::protobuf_entity_data* protobuf_data) override {
        return {};
    }

    bool row_to_protobuf_data(
        row* row_ptr, const uint32_t index, ark::protobuf::protobuf_entity_data* protobuf_data) override {
        return {};
    }

    bool table_row_data_to_protobuf_data(const uint32_t index, uint32_t row, const uint32_t col, const ark::data& data,
        ark::protobuf::protobuf_entity_data* protobuf_data) override {
        return {};
    }

    bool entity_node_to_protobuf_data(std::shared_ptr<entity> entity_ptr,
        ark::protobuf::protobuf_entity_data* protobuf_data, const mask_type mask = 0) override {
        return {};
    }

    bool entity_table_to_protobuf_data(std::shared_ptr<entity> entity_ptr,
        ark::protobuf::protobuf_entity_data* protobuf_data, const mask_type mask = 0) override {
        return {};
    }

    bool add_event_call_back(const guid_t& self, const int event_id, event_process_functor&& cb) override {
        return {};
    }

    bool add_class_call_back(const std::string& class_name, class_event_functor&& cb, const int32_t prio) override {
        return {};
    }

    bool add_node_call_back(const std::string& class_name, const std::string& name, data_node_event_functor&& cb,
        const int32_t prio) override {
        return {};
    }

    bool add_table_call_back(const std::string& class_name, const std::string& name, data_table_event_functor&& cb,
        const int32_t prio) override {
        return {};
    }

    bool add_node_call_back(const std::string& class_name, const uint32_t index, data_node_event_functor&& cb,
        const int32_t prio) override {
        return {};
    }

    bool add_table_call_back(const std::string& class_name, const uint32_t index, data_table_event_functor&& cb,
        const int32_t prio) override {
        return {};
    }

    bool add_container_call_back(const std::string& class_name, const uint32_t index, container_event_functor&& cb,
        const int32_t prio) override {
        return {};
    }

    bool add_common_container_call_back(container_event_functor&& cb, const int32_t prio) override {
        return {};
    }

    bool add_common_class_event(class_event_functor&& cb, const int32_t prio) override {
        return {};
    }

    bool add_leave_scene_event(const std::string& class_name, scene_event_functor&& cb, const int32_t prio) override {
        return {};
    }

    bool add_enter_scene_event(const std::string& class_name, scene_event_functor&& cb, const int32_t prio) override {
        return {};
    }

    bool add_move_event(const std::string& class_name, move_event_functor&& cb, const int32_t prio) override {
        return {};
    }
};

static_assert(!std::is_abstract_v<legacy_snapshot_kernel>);

class custom_snapshot_kernel final : public legacy_snapshot_kernel
{
public:
    ark::entity_sync::scalar_snapshot capture_scalar_snapshot(
        const ark::entity&, std::span<const std::uint32_t>, const ark::entity_sync::snapshot_limits& = {}) override {
        return {123, "custom", {}};
    }
};
} // namespace

TEST_CASE("Kernel capture defaults preserve source subclasses and support virtual overrides") {
    const auto schema = std::make_shared<ark::class_meta>("Source");
    ark::entity_impl source(schema, 7, 0, 1, 0, ark::data_list_impl{});
    legacy_snapshot_kernel legacy;
    ark::kernel_module* kernel = &legacy;
    CHECK_THROWS_AS(kernel->capture_scalar_snapshot(source, {}), std::logic_error);
    CHECK_THROWS_AS(kernel->capture_nested_snapshot(source, {}), std::logic_error);
    custom_snapshot_kernel custom;
    kernel = &custom;
    CHECK(kernel->capture_scalar_snapshot(source, {}).entity_id == 123);
    ark::kernel_module_impl implementation;
    kernel = &implementation;
    const auto snapshot = kernel->capture_scalar_snapshot(source, {});
    CHECK(snapshot.entity_id == 7);
    CHECK(snapshot.class_name == "Source");
    CHECK(snapshot.fields.empty());
    const auto nested = kernel->capture_nested_snapshot(source, {"source/v1", 7, {"Source", {}, {}, {}}});
    CHECK(nested.root.scalars == snapshot);
    CHECK(nested.root.incarnation != 0);
}
