/*
 * This source file is part of ARK
 * For the latest info, see https://github.com/ArkNX
 *
 * Copyright (c) 2013-2020 ArkNX authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"),
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#pragma once

#include <span>
#include <stdexcept>

#include "ark/kernel/entity.hpp"
#include "ark/kernel/entity_nested_snapshot.hpp"
#include "ark/kernel/entity_snapshot.hpp"
#include "ark/kernel/static_entity.hpp"
#include "ark/module.hpp"
#include "ark/proto.hpp"

namespace ark {

class kernel_module : public module
{
public:
    template<typename base_type>
    bool add_common_class_event(base_type* base_ptr,
        int (base_type::*handler)(const guid_t&, const std::string&, const entity_event, const data_list&),
        const int32_t prio = 0) {
        auto functor = std::bind(handler, base_ptr, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3,
            std::placeholders::_4);
        return add_common_class_event(std::move(functor), prio);
    }

    /////////////////////////////////////////////////////////////////
    template<typename base_type>
    bool add_event_call_back(const guid_t& self, const int event_id, base_type* base_ptr,
        int (base_type::*handler)(const guid_t&, const int, const data_list&)) {
        auto functor =
            std::bind(handler, base_ptr, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
        return add_event_call_back(self, event_id, std::move(functor));
    }

    template<typename base_type>
    bool add_class_call_back(const std::string& name, base_type* base_ptr,
        int (base_type::*handler)(const guid_t&, const std::string&, const entity_event, const data_list&),
        const int32_t prio = 0) {
        auto functor = std::bind(handler, base_ptr, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3,
            std::placeholders::_4);
        return add_class_call_back(name, std::move(functor), prio);
    }

    template<typename base_type>
    bool add_node_call_back(const std::string& class_name, const std::string& name, base_type* base_ptr,
        int (base_type::*handler)(const guid_t&, const std::string&, const uint32_t, const data&, const data&),
        const int32_t prio = 0) {
        return add_node_call_back(class_name, name,
            std::move(std::bind(handler, base_ptr, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3,
                std::placeholders::_4, std::placeholders::_5)),
            prio);
    }

    template<typename base_type>
    bool add_table_call_back(const std::string& class_name, const std::string& name, base_type* base_ptr,
        int (base_type::*handler)(const guid_t&, const table_event_data&, const data&, const data&),
        const int32_t prio = 0) {
        return add_table_call_back(class_name, name,
            std::move(std::bind(handler, base_ptr, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3,
                std::placeholders::_4)),
            prio);
    }

    // call back by index
    template<typename base_type>
    bool add_node_call_back(const std::string& class_name, const uint32_t index, base_type* base_ptr,
        int (base_type::*handler)(const guid_t&, const std::string&, const uint32_t, const data&, const data&),
        const int32_t prio = 0) {
        return add_node_call_back(class_name, index,
            std::move(std::bind(handler, base_ptr, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3,
                std::placeholders::_4, std::placeholders::_5)),
            prio);
    }

    template<typename base_type>
    bool add_table_call_back(const std::string& class_name, const uint32_t index, base_type* base_ptr,
        int (base_type::*handler)(const guid_t&, const table_event_data&, const data&, const data&),
        const int32_t prio = 0) {
        return add_table_call_back(class_name, index,
            std::move(std::bind(handler, base_ptr, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3,
                std::placeholders::_4)),
            prio);
    }

    // container call back
    template<typename base_type>
    bool add_container_call_back(const std::string& class_name, const uint32_t index, base_type* base_ptr,
        int (base_type::*handler)(
            const guid_t&, const uint32_t, const container_op_type, const uint32_t, const uint32_t),
        const int32_t prio = 0) {
        return add_container_call_back(class_name, index,
            std::move(std::bind(handler, base_ptr, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3,
                std::placeholders::_4, std::placeholders::_5)),
            prio);
    }

    // only for player
    template<typename base_type>
    bool add_common_container_call_back(base_type* base_ptr,
        int (base_type::*handler)(
            const guid_t&, const uint32_t, const container_op_type, const uint32_t, const uint32_t),
        const int32_t prio = 0) {
        return add_common_container_call_back(
            std::move(std::bind(handler, base_ptr, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3,
                std::placeholders::_4, std::placeholders::_5)),
            prio);
    }

    // scene event call back
    template<typename base_type>
    bool add_leave_scene_event(const std::string& class_name, base_type* base_ptr,
        int (base_type::*handler)(const guid_t&, const int, const int), const int32_t prio = 0) {
        return add_leave_scene_event(class_name,
            std::move(
                std::bind(handler, base_ptr, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)),
            prio);
    }

    template<typename base_type>
    bool add_enter_scene_event(const std::string& class_name, base_type* base_ptr,
        int (base_type::*handler)(const guid_t&, const int, const int), const int32_t prio = 0) {
        return add_enter_scene_event(class_name,
            std::move(
                std::bind(handler, base_ptr, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)),
            prio);
    }

    // move event call back
    template<typename base_type>
    bool add_move_event(const std::string& class_name, base_type* base_ptr,
        int (base_type::*handler)(const guid_t&, const vector3d&, const vector3d&), const int32_t prio = 0) {
        return add_move_event(class_name,
            std::move(
                std::bind(handler, base_ptr, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)),
            prio);
    }

    //////////////////////////////////////////////////////////////////////////

    virtual bool do_event(
        const guid_t& self, const std::string& name, entity_event event, const data_list& value_list) = 0;
    virtual bool do_event(const guid_t& self, const int event_id, const data_list& value_list) = 0;

    /////////////////////////////////////////////////////////////////
    virtual std::shared_ptr<entity> create_entity(const guid_t& self, const int map_id, const int map_instance_id,
        const std::string& class_name, const id_type config_id, const data_list& args) = 0;

    virtual std::shared_ptr<entity> create_container_entity(
        const guid_t& self, const uint32_t container_index, const std::string& class_name, const id_type config_id) = 0;

    virtual std::shared_ptr<entity> get_entity(const guid_t& self) = 0;
    virtual std::shared_ptr<static_entity> get_static_entity(const id_type config_id) = 0;

    virtual bool destroy_entity(const guid_t& self) = 0;
    virtual bool destroy_all() = 0;

    virtual bool exist(const guid_t& self) = 0;

    // entity to db data for save
    virtual bool entity_to_db_data(const guid_t& self, ark::protobuf::protobuf_db_entity& protobuf_data) = 0;
    // create entity by db data
    virtual std::shared_ptr<entity> create_entity(const ark::protobuf::protobuf_db_entity& protobuf_data) = 0;
    // send message
    virtual bool send_custom_message(const guid_t& target, const uint32_t msg_id, const data_list& args) = 0;

    virtual bool log_info(const guid_t& ident) = 0;

    virtual int log_object_data(const guid_t& guid) = 0;

    // entity to pb
    virtual bool node_to_protobuf_data(
        const uint32_t index, const data& data, ark::protobuf::protobuf_entity_data* protobuf_data) = 0;
    virtual bool row_to_protobuf_data(
        row* row_ptr, const uint32_t index, ark::protobuf::protobuf_entity_data* protobuf_data) = 0;
    virtual bool table_row_data_to_protobuf_data(const uint32_t index, uint32_t row, const uint32_t col,
        const data& data, ark::protobuf::protobuf_entity_data* protobuf_data) = 0;
    virtual bool entity_node_to_protobuf_data(std::shared_ptr<entity> entity_ptr,
        ark::protobuf::protobuf_entity_data* protobuf_data, const mask_type mask = 0) = 0;
    virtual bool entity_table_to_protobuf_data(std::shared_ptr<entity> entity_ptr,
        ark::protobuf::protobuf_entity_data* protobuf_data, const mask_type mask = 0) = 0;

    // The caller authorizes every selected index; empty selects no fields.
    // Framework capture owns values, preserves lazy defaults without mutation,
    // and throws invalid_argument/length_error on invalid input/limits. Capture
    // requires a stable source on its owner thread. Legacy source subclasses may
    // retain this default, which reports that capture is unsupported.
    virtual entity_sync::scalar_snapshot capture_scalar_snapshot(
        const entity&, std::span<const std::uint32_t>, const entity_sync::snapshot_limits& = {}) {
        throw std::logic_error("scalar snapshot capture is not supported by this Kernel implementation");
    }

    // Owner-thread capture with an explicit root/session-authorized projection.
    virtual entity_sync::nested_snapshot capture_nested_snapshot(
        const entity&, const entity_sync::nested_projection&, const entity_sync::nested_snapshot_limits& = {}) {
        throw std::logic_error("Nested snapshots are not supported by this Kernel implementation");
    }

protected:
    virtual bool add_event_call_back(const guid_t& self, const int event_id, event_process_functor&& cb) = 0;
    virtual bool add_class_call_back(const std::string& class_name, class_event_functor&& cb, const int32_t prio) = 0;

    virtual bool add_node_call_back(
        const std::string& class_name, const std::string& name, data_node_event_functor&& cb, const int32_t prio) = 0;
    virtual bool add_table_call_back(
        const std::string& class_name, const std::string& name, data_table_event_functor&& cb, const int32_t prio) = 0;

    virtual bool add_node_call_back(
        const std::string& class_name, const uint32_t index, data_node_event_functor&& cb, const int32_t prio) = 0;
    virtual bool add_table_call_back(
        const std::string& class_name, const uint32_t index, data_table_event_functor&& cb, const int32_t prio) = 0;

    virtual bool add_container_call_back(
        const std::string& class_name, const uint32_t index, container_event_functor&& cb, const int32_t prio) = 0;
    virtual bool add_common_container_call_back(container_event_functor&& cb, const int32_t prio) = 0;

    virtual bool add_common_class_event(class_event_functor&& cb, const int32_t prio) = 0;

    virtual bool add_leave_scene_event(const std::string& class_name, scene_event_functor&& cb, const int32_t prio) = 0;
    virtual bool add_enter_scene_event(const std::string& class_name, scene_event_functor&& cb, const int32_t prio) = 0;
    virtual bool add_move_event(const std::string& class_name, move_event_functor&& cb, const int32_t prio) = 0;
};

} // namespace ark
