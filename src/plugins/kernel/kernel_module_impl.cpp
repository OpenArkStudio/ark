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

#include "ark/define.hpp"
#include "ark/kernel/container_impl.hpp"
#include "ark/kernel/data_list_impl.hpp"
#include "ark/kernel/entity_impl.hpp"
#include "ark/kernel/kernel_module_impl.hpp"
#include "ark/kernel/table_impl.hpp"

namespace ark {

kernel_module_impl::kernel_module_impl() {
    inner_nodes_.add_element(entity_meta_base_entity::config_id(), ARK_NEW int32_t(0));
    inner_nodes_.add_element(entity_meta_base_entity::class_name(), ARK_NEW int32_t(0));
    inner_nodes_.add_element(entity_meta_base_entity::map_id(), ARK_NEW int32_t(0));
    inner_nodes_.add_element(entity_meta_base_entity::map_inst_id(), ARK_NEW int32_t(0));
}

kernel_module_impl::~kernel_module_impl() {
    objects_.clear();
}

bool kernel_module_impl::init() {
    delete_list_.clear();

    map_module_ = find_module<map_module>();
    class_meta_module_ = find_module<class_meta_module>();
    config_module_ = find_module<config_module>();
    guid_module_ = find_module<guid_module>();

    return true;
}

bool kernel_module_impl::post_init() {
    // Schema loading completes in the metadata module's init phase.
    auto container_func = std::bind(&kernel_module_impl::on_container_call_back, this, std::placeholders::_1,
        std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5);
    add_common_container_call_back(std::move(container_func), 999999); // after other callbacks being done

    add_sync_call_back();

    return true;
}

bool kernel_module_impl::update() {
    cur_exec_object_ = null_guid;

    if (!delete_list_.empty())
    {
        for (auto it : delete_list_)
        {
            destroy_entity(it);
        }

        delete_list_.clear();
    }

    for (auto& iter : objects_)
    {
        auto entity_ptr = iter.second;
        if (entity_ptr == nullptr)
        {
            continue;
        }

        entity_ptr->update();
    }

    class_call_back_manager::on_delay_sync();

    return true;
}

bool kernel_module_impl::pre_shut() {
    return destroy_all();
}

bool kernel_module_impl::copy_data(
    std::shared_ptr<entity> entity_ptr, std::shared_ptr<static_entity> static_entity_ptr) {
    if (entity_ptr == nullptr || nullptr == static_entity_ptr)
    {
        return false;
    }

    // static node manager must be not empty
    auto static_node_manager_ptr = get_node_manager(static_entity_ptr);
    if (static_node_manager_ptr == nullptr || static_node_manager_ptr->is_empty())
    {
        return false;
    }

    auto node_manager_ptr = get_node_manager(entity_ptr);
    if (node_manager_ptr == nullptr)
    {
        return false;
    }

    // Fill absent nodes without overwriting explicit initialization arguments.
    auto& data_list = static_node_manager_ptr->get_data_list();
    for (auto& iter : data_list)
    {
        if (node_manager_ptr->get_node(iter.first) == nullptr)
        {
            node_manager_ptr->create_data(iter.second);
        }
    }

    return true;
}

std::shared_ptr<entity> kernel_module_impl::create_entity(const guid_t& self, const int map_id,
    const int map_instance_id, const std::string& class_name, const id_type config_id, const data_list& args) {
    guid_t object_id = self;

    auto map_info_ptr = map_module_->get_map_info(map_id);
    if (map_info_ptr == nullptr)
    {
        ARK_LOG_ERROR("There is no scene, scene = {}", map_id);
        return nullptr;
    }

    if (!map_info_ptr->exist_instance(map_instance_id))
    {
        ARK_LOG_ERROR("There is no group, scene = {} group = {}", map_id, map_instance_id);
        return nullptr;
    }

    auto class_meta_ptr = class_meta_module_->find_meta(class_name);
    if (nullptr == class_meta_ptr)
    {
        ARK_LOG_ERROR("There is no class meta, name = {}", class_name);
        return nullptr;
    }

    std::shared_ptr<static_entity> static_entity_ptr = nullptr;
    if (config_id > 0)
    {
        static_entity_ptr = get_static_entity(config_id);
        if (nullptr == static_entity_ptr)
        {
            ARK_LOG_ERROR("There is no config, config_id = {}", config_id);
            return nullptr;
        }

        if (static_entity_ptr->get_class_name() != class_name)
        {
            ARK_LOG_ERROR("Config class does not match entity class, config_id = {}", config_id);
            return nullptr;
        }
    }

    // check args num
    size_t arg_count = args.get_count();
    if (arg_count % 2 != 0)
    {
        ARK_LOG_ERROR("Args count is wrong, count = {}", arg_count);
        return nullptr;
    }

    if (object_id == null_guid)
    {
        object_id = guid_module_->create_guid();
    }

    // Check if the entity exists
    if (get_entity(object_id) != nullptr)
    {
        ARK_LOG_ERROR("The entity has existed, id = {}", object_id);
        return nullptr;
    }

    std::shared_ptr<entity> entity_ptr =
        std::make_shared<entity_impl>(class_meta_ptr, object_id, config_id, map_id, map_instance_id, args);

    objects_.insert(object_id, entity_ptr);

    if (class_name == entity_meta_player::self_name())
    {
        map_info_ptr->add_entity_to_instance(map_instance_id, object_id, true);
    }
    //else if (class_name == AFEntityMetaPlayer::self_name()) // to do : npc type for now we do not have
    //{
    //}

    copy_data(entity_ptr, static_entity_ptr);

    do_event(object_id, class_name, entity_event::entity_evt_pre_load_data, args);

    // original args here

    do_event(object_id, class_name, entity_event::entity_evt_load_data, args);
    do_event(object_id, class_name, entity_event::entity_evt_pre_effect_data, args);
    do_event(object_id, class_name, entity_event::entity_evt_effect_data, args);
    do_event(object_id, class_name, entity_event::entity_evt_post_effect_data, args);
    do_event(object_id, class_name, entity_event::entity_evt_data_finished, args);

    return entity_ptr;
}

std::shared_ptr<entity> kernel_module_impl::create_container_entity(
    const guid_t& self, const uint32_t container_index, const std::string& class_name, const id_type config_id) {
    auto entity_ptr = get_entity(self);
    if (entity_ptr == nullptr)
    {
        ARK_LOG_ERROR("There is no object, object = {}", self);
        return nullptr;
    }

    auto container_ptr = entity_ptr->find_container(container_index);
    if (container_ptr == nullptr)
    {
        ARK_LOG_ERROR("There is no container, container = {}", container_index);
        return nullptr;
    }

    auto class_meta_ptr = class_meta_module_->find_meta(class_name);
    if (nullptr == class_meta_ptr)
    {
        ARK_LOG_ERROR("There is no class meta, name = {}", class_name);
        return nullptr;
    }

    std::shared_ptr<static_entity> static_entity_ptr = nullptr;
    if (config_id > 0)
    {
        static_entity_ptr = get_static_entity(config_id);
        if (nullptr == static_entity_ptr)
        {
            ARK_LOG_ERROR("There is no config, config_id = {}", config_id);
            return nullptr;
        }

        if (static_entity_ptr->get_class_name() != class_name)
        {
            ARK_LOG_ERROR("Config class does not match entity class, config_id = {}", config_id);
            return nullptr;
        }
    }

    auto map_id = entity_ptr->get_map_id();
    auto map_info_ptr = map_module_->get_map_info(map_id);
    if (map_info_ptr == nullptr)
    {
        ARK_LOG_ERROR("There is no scene, scene = {}", map_id);
        return nullptr;
    }

    auto map_instance_id = entity_ptr->get_map_entity_id();
    if (!map_info_ptr->exist_instance(map_instance_id))
    {
        ARK_LOG_ERROR("There is no group, scene = {} group = {}", map_id, map_instance_id);
        return nullptr;
    }

    guid_t object_id = guid_module_->create_guid();

    // Check if the entity exists
    if (get_entity(object_id) != nullptr)
    {
        ARK_LOG_ERROR("The entity has existed, id = {}", object_id);
        return nullptr;
    }

    std::shared_ptr<entity> container_entity_ptr =
        std::make_shared<entity_impl>(class_meta_ptr, object_id, config_id, map_id, map_instance_id, data_list_impl());

    objects_.insert(object_id, container_entity_ptr);

    copy_data(container_entity_ptr, static_entity_ptr);

    container_ptr->place(container_entity_ptr);

    data_list_impl args;
    do_event(object_id, class_name, entity_event::entity_evt_pre_load_data, args);

    do_event(object_id, class_name, entity_event::entity_evt_load_data, args);
    do_event(object_id, class_name, entity_event::entity_evt_pre_effect_data, args);
    do_event(object_id, class_name, entity_event::entity_evt_effect_data, args);
    do_event(object_id, class_name, entity_event::entity_evt_post_effect_data, args);
    do_event(object_id, class_name, entity_event::entity_evt_data_finished, args);

    return container_entity_ptr;
}

std::shared_ptr<static_entity> kernel_module_impl::get_static_entity(const id_type config_id) {
    return config_module_->find_static_entity(config_id);
}

std::shared_ptr<entity> kernel_module_impl::get_entity(const guid_t& self) {
    return objects_.find_value(self);
}

bool kernel_module_impl::destroy_all() {
    for (auto& iter : objects_)
    {
        auto& entity_ptr = iter.second;
        if (entity_ptr->get_parent_container() != nullptr)
        {
            continue;
        }

        delete_list_.push_back(iter.second->get_id());
    }

    // run another frame
    update();

    return true;
}

bool kernel_module_impl::destroy_entity(const guid_t& self) {
    if (self == cur_exec_object_ && self != null_guid)
    {
        return destroy_self(self);
    }

    auto entity_ptr = get_entity(self);
    if (entity_ptr == nullptr)
    {
        ARK_LOG_ERROR("Cannot find this object, self={}", null_guid);
        return false;
    }

    auto parent_container_ptr = entity_ptr->get_parent_container();
    if (parent_container_ptr)
    {
        // use container to destroy its entity
        return parent_container_ptr->destroy(self);
    }
    else
    {
        return inner_destroy_entity(entity_ptr);
    }
}

bool kernel_module_impl::destroy_self(const guid_t& self) {
    delete_list_.push_back(self);
    return true;
}

bool kernel_module_impl::inner_destroy_entity(std::shared_ptr<entity> entity_ptr) {
    if (entity_ptr == nullptr)
    {
        ARK_LOG_ERROR("Cannot find this object, self={}", null_guid);
        return false;
    }

    auto& self = entity_ptr->get_id();
    int32_t map_id = entity_ptr->get_map_id();
    int32_t inst_id = entity_ptr->get_map_entity_id();

    std::shared_ptr<map_info> map_info_ptr = map_module_->get_map_info(map_id);
    if (map_info_ptr != nullptr)
    {
        const std::string& class_name = entity_ptr->get_class_name();

        map_info_ptr->remove_entity_from_instance(
            inst_id, self, ((class_name == entity_meta_player::self_name()) ? true : false));

        do_event(self, class_name, entity_event::entity_evt_pre_destroy, data_list_impl());
        do_event(self, class_name, entity_event::entity_evt_destroy, data_list_impl());

        return objects_.erase(self);
    }
    else
    {
        ARK_LOG_ERROR("Cannot find this map, object_id={} map={} inst={}", self, map_id, inst_id);
        return false;
    }
}

bool kernel_module_impl::add_event_call_back(const guid_t& self, const int event_id, event_process_functor&& cb) {
    std::shared_ptr<entity> entity_ptr = get_entity(self);
    ARK_ASSERT_RET_VAL(entity_ptr != nullptr, false);

    auto event_manager_ptr = get_event_manager(entity_ptr);
    ARK_ASSERT_RET_VAL(event_manager_ptr != nullptr, false);

    return event_manager_ptr->add_event_call_back(event_id, std::move(cb));
}

bool kernel_module_impl::add_class_call_back(
    const std::string& class_name, class_event_functor&& cb, const int32_t prio) {
    return class_meta_module_->add_class_call_back(class_name, std::move(cb), prio);
}

bool kernel_module_impl::add_node_call_back(
    const std::string& class_name, const std::string& name, data_node_event_functor&& cb, const int32_t prio) {
    auto class_meta_ptr = class_meta_module_->find_meta(class_name);
    ARK_ASSERT_RET_VAL(class_meta_ptr != nullptr, false);

    auto index = class_meta_ptr->get_index(name);
    if (index == 0)
    {
        return false;
    }

    add_node_call_back(class_name, index, std::move(cb), prio);

    return true;
}

bool kernel_module_impl::add_table_call_back(
    const std::string& class_name, const std::string& name, data_table_event_functor&& cb, const int32_t prio) {
    auto class_meta_ptr = class_meta_module_->find_meta(class_name);
    ARK_ASSERT_RET_VAL(class_meta_ptr != nullptr, false);

    auto index = class_meta_ptr->get_index(name);
    if (index == 0)
    {
        return false;
    }

    add_table_call_back(class_name, index, std::move(cb), prio);

    return true;
}

bool kernel_module_impl::add_node_call_back(
    const std::string& class_name, const uint32_t index, data_node_event_functor&& cb, const int32_t prio) {
    auto class_meta_ptr = class_meta_module_->find_meta(class_name);
    ARK_ASSERT_RET_VAL(class_meta_ptr != nullptr, false);

    auto data_meta_ptr = class_meta_ptr->find_data_meta(index);
    ARK_ASSERT_RET_VAL(data_meta_ptr != nullptr, false);

    auto callback_ptr = class_meta_ptr->get_class_call_back_manager();
    ARK_ASSERT_RET_VAL(callback_ptr != nullptr, false);

    callback_ptr->add_data_call_back(index, std::move(cb), prio);

    return true;
}

bool kernel_module_impl::add_table_call_back(
    const std::string& class_name, const uint32_t index, data_table_event_functor&& cb, const int32_t prio) {
    auto class_meta_ptr = class_meta_module_->find_meta(class_name);
    ARK_ASSERT_RET_VAL(class_meta_ptr != nullptr, false);

    auto table_meta_ptr = class_meta_ptr->find_table_meta(index);
    ARK_ASSERT_RET_VAL(table_meta_ptr != nullptr, false);

    auto callback_ptr = class_meta_ptr->get_class_call_back_manager();
    ARK_ASSERT_RET_VAL(callback_ptr != nullptr, false);

    callback_ptr->add_table_call_back(index, std::move(cb), prio);

    return true;
}

bool kernel_module_impl::add_container_call_back(
    const std::string& class_name, const uint32_t index, container_event_functor&& cb, const int32_t prio) {
    auto class_meta_ptr = class_meta_module_->find_meta(class_name);
    ARK_ASSERT_RET_VAL(class_meta_ptr != nullptr, false);

    auto container_meta_ptr = class_meta_ptr->find_container_meta(index);
    ARK_ASSERT_RET_VAL(container_meta_ptr != nullptr, false);

    auto callback_ptr = class_meta_ptr->get_class_call_back_manager();
    ARK_ASSERT_RET_VAL(callback_ptr != nullptr, false);

    callback_ptr->add_container_call_back(index, std::move(cb), prio);

    return true;
}

bool kernel_module_impl::add_common_container_call_back(container_event_functor&& cb, const int32_t prio) {
    for (const auto& entry : class_meta_module_->get_meta_list())
    {
        const auto& meta = entry.second;
        if (!meta)
        {
            continue;
        }
        for (const auto& container : meta->get_container_meta_list())
        {
            auto callback = cb;
            if (!add_container_call_back(meta->get_name(), container.first, std::move(callback), prio))
            {
                return false;
            }
        }
    }

    return true;
}

bool kernel_module_impl::add_common_class_event(class_event_functor&& cb, const int32_t prio) {
    auto& class_meta_list = class_meta_module_->get_meta_list();
    for (auto& iter : class_meta_list)
    {
        auto class_meta_ptr = iter.second;
        if (nullptr == class_meta_ptr)
        {
            continue;
        }

        add_class_call_back(iter.first, std::move(cb), prio);
    }

    return true;
}

bool kernel_module_impl::add_leave_scene_event(
    const std::string& class_name, scene_event_functor&& cb, const int32_t prio) {
    auto class_meta_ptr = class_meta_module_->find_meta(class_name);
    ARK_ASSERT_RET_VAL(class_meta_ptr != nullptr, false);

    auto callback_ptr = class_meta_ptr->get_class_call_back_manager();
    ARK_ASSERT_RET_VAL(callback_ptr != nullptr, false);

    return callback_ptr->add_leave_scene_event(std::move(cb), prio);
}

bool kernel_module_impl::add_enter_scene_event(
    const std::string& class_name, scene_event_functor&& cb, const int32_t prio) {
    auto class_meta_ptr = class_meta_module_->find_meta(class_name);
    ARK_ASSERT_RET_VAL(class_meta_ptr != nullptr, false);

    auto callback_ptr = class_meta_ptr->get_class_call_back_manager();
    ARK_ASSERT_RET_VAL(callback_ptr != nullptr, false);

    return callback_ptr->add_enter_scene_event(std::move(cb), prio);
}

bool kernel_module_impl::add_move_event(const std::string& class_name, move_event_functor&& cb, const int32_t prio) {
    auto class_meta_ptr = class_meta_module_->find_meta(class_name);
    ARK_ASSERT_RET_VAL(class_meta_ptr != nullptr, false);

    auto callback_ptr = class_meta_ptr->get_class_call_back_manager();
    ARK_ASSERT_RET_VAL(callback_ptr != nullptr, false);

    return callback_ptr->add_move_event(std::move(cb), prio);
}

void kernel_module_impl::add_sync_call_back() {
    // node sync call back
    auto node_func = std::bind(&kernel_module_impl::on_sync_node, this, std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3, std::placeholders::_4);

    class_call_back_manager::add_node_sync_call_back(data_mask::pf_sync_view, std::move(node_func));
    class_call_back_manager::add_node_sync_call_back(data_mask::pf_sync_self, std::move(node_func));

    // table sync call back
    auto table_func = std::bind(&kernel_module_impl::on_sync_table, this, std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3, std::placeholders::_4);

    class_call_back_manager::add_table_sync_call_back(data_mask::pf_sync_view, std::move(table_func));
    class_call_back_manager::add_table_sync_call_back(data_mask::pf_sync_self, std::move(table_func));

    // container sync call back
    auto container_func =
        std::bind(&kernel_module_impl::on_sync_container, this, std::placeholders::_1, std::placeholders::_2,
            std::placeholders::_3, std::placeholders::_4, std::placeholders::_5, std::placeholders::_6);

    class_call_back_manager::add_container_sync_call_back(data_mask::pf_sync_view, std::move(container_func));
    class_call_back_manager::add_container_sync_call_back(data_mask::pf_sync_self, std::move(container_func));

    // data delay call back
    auto delay_func = std::bind(&kernel_module_impl::on_delay_sync_data, this, std::placeholders::_1,
        std::placeholders::_2, std::placeholders::_3);

    class_call_back_manager::add_delay_sync_call_back(data_mask::pf_sync_view, std::move(delay_func));
    class_call_back_manager::add_delay_sync_call_back(data_mask::pf_sync_self, std::move(delay_func));

    // send msg functor
    auto view_func = std::bind(&kernel_module_impl::send_to_view, this, std::placeholders::_1, std::placeholders::_2);
    sync_functors_.insert(std::make_pair(data_mask::pf_sync_view, std::forward<sync_functor>(view_func)));

    auto self_func = std::bind(&kernel_module_impl::send_to_self, this, std::placeholders::_1, std::placeholders::_2);
    sync_functors_.insert(std::make_pair(data_mask::pf_sync_self, std::forward<sync_functor>(self_func)));
}

int kernel_module_impl::on_sync_node(
    const guid_t& self, const uint32_t index, const data_mask mask_value, const data& data) {
    // find parent entity
    auto entity_ptr = get_entity(self);
    if (entity_ptr == nullptr)
    {
        ARK_LOG_ERROR("sync node failed entity do no exist, self={}", self);
        return -1;
    }

    ark::protobuf::protobuf_entity protobuf_data;
    auto entity_id = self;
    auto protobuf_entity = protobuf_data.mutable_data();

    auto parent_container_ptr = entity_ptr->get_parent_container();
    if (parent_container_ptr != nullptr)
    {
        if (!try_add_container_protobuf_entity(
                parent_container_ptr, self, *protobuf_data.mutable_data(), entity_id, protobuf_entity))
        {
            ARK_LOG_ERROR("sync node failed container entity do no exist, self={}", self);
            return -1;
        }
    }

    if (!node_to_protobuf_data(index, data, protobuf_entity))
    {
        ARK_LOG_ERROR("node to pb failed, self={}, index={}", self, index);
        return -1;
    }

    protobuf_data.set_id(entity_id);
    if (send_sync_msg(entity_id, mask_value, protobuf_data) < 0)
    {
        ARK_LOG_ERROR("send sync msg failed, self={}, index={}", entity_id, index);
        return -1;
    }

    return 0;
}

int kernel_module_impl::on_sync_table(
    const guid_t& self, const table_event_data& event, const data_mask mask_value, const data& data) {
    table_op_type op_type = static_cast<table_op_type>(event.op_type);
    switch (op_type)
    {
    case table_op_type::table_add:
    {
        on_sync_table_add(self, event, mask_value);
    }
    break;
    case table_op_type::table_delete:
    {
        on_sync_table_delete(self, event, mask_value);
    }
    break;
    case table_op_type::table_swap:
        // do not have yet
        break;
    case table_op_type::table_update:
    {
        on_sync_table_update(self, event, mask_value, data);
    }
    break;
    case table_op_type::table_coverage:
        // will do something
        break;
    default:
        break;
    }

    return 0;
}

int kernel_module_impl::on_sync_container(const guid_t& self, const uint32_t index, const data_mask mask,
    const container_op_type op_type, uint32_t src_index, uint32_t dest_index) {
    switch (op_type)
    {
    case container_op_type::op_place:
    {
        on_sync_container_place(self, index, mask, src_index);
    }
    break;
    case container_op_type::op_remove:
    {
        on_sync_container_remove(self, index, mask, src_index);
    }
    break;
    case container_op_type::op_destroy:
    {
        on_sync_container_destroy(self, index, mask, src_index);
    }
    break;
    case container_op_type::op_swap:
    {
        on_sync_container_swap(self, index, mask, src_index, dest_index);
    }
    break;
    default:
        break;
    }
    return 0;
}

int kernel_module_impl::on_delay_sync_data(
    const guid_t& self, const data_mask mask_value, const delay_sync_data& data) {
    if (data.node_list.size() == 0 && data.table_list.size() == 0)
    {
        return 0;
    }

    // find parent entity
    auto entity_ptr = get_entity(self);
    if (entity_ptr == nullptr)
    {
        ARK_LOG_ERROR("sync delay data failed entity do no exist, self={}", self);
        return -1;
    }

    ark::protobuf::protobuf_delay_entity protobuf_data;
    auto entity_id = self;
    auto protobuf_entity = protobuf_data.mutable_data();

    auto parent_container_ptr = entity_ptr->get_parent_container();
    if (parent_container_ptr != nullptr)
    {
        if (!try_add_container_protobuf_entity(
                parent_container_ptr, self, *protobuf_data.mutable_data(), entity_id, protobuf_entity))
        {
            ARK_LOG_ERROR("sync delay data failed container entity do no exist, self={}", self);
            return -1;
        }
    }

    // node to pb
    for (auto& iter : data.node_list)
    {
        auto node_ptr = iter;
        if (!node_to_protobuf_data(node_ptr, protobuf_entity))
        {
            continue;
        }
    }

    // table to pb
    for (auto& iter : data.table_list)
    {
        auto table_index = iter.first;
        auto& table = iter.second;
        delay_table_to_protobuf(table, table_index, protobuf_data, *protobuf_entity);
    }

    // container to pb
    for (auto& iter : data.container_list)
    {
        auto container_index = iter.first;
        auto& container = iter.second;
        delay_container_to_protobuf(entity_ptr, container, container_index, protobuf_data);
    }

    protobuf_data.set_id(entity_id);

    if (send_sync_msg(entity_id, mask_value, protobuf_data) < 0)
    {
        ARK_LOG_ERROR("send sync msg failed, self={}", self);
        return -1;
    }

    return 0;
}

int kernel_module_impl::on_sync_table_add(
    const guid_t& self, const table_event_data& event, const data_mask mask_value) {
    // find parent entity
    auto entity_ptr = get_entity(self);
    if (entity_ptr == nullptr)
    {
        ARK_LOG_ERROR("sync table failed entity do no exist, self={}", self);
        return -1;
    }

    auto table_ptr = entity_ptr->find_table(event.table_index);
    if (nullptr == table_ptr)
    {
        ARK_LOG_ERROR("sync table failed table do no exist, self={}, table={}", self, event.table_name);
        return -1;
    }

    auto row_ptr = table_ptr->find_row(event.row);
    if (nullptr == row_ptr)
    {
        ARK_LOG_ERROR(
            "sync table failed table row do no exist, self={}, table={}, row={}", self, event.table_name, event.row);
        return -1;
    }

    ark::protobuf::protobuf_entity_table_add protobuf_data;
    auto entity_id = self;
    auto protobuf_entity = protobuf_data.mutable_data();

    auto parent_container_ptr = entity_ptr->get_parent_container();
    if (parent_container_ptr != nullptr)
    {
        if (!try_add_container_protobuf_entity(
                parent_container_ptr, self, *protobuf_data.mutable_data(), entity_id, protobuf_entity))
        {
            ARK_LOG_ERROR("sync node failed container entity do no exist, self={}", self);
            return -1;
        }
    }

    ark::protobuf::protobuf_entity_data protobuf_row;
    if (!row_to_protobuf_data(row_ptr, event.row, &protobuf_row))
    {
        ARK_LOG_ERROR(
            "sync table failed table row do no exist, self={}, table={}, row={}", self, event.table_name, event.row);
        return -1;
    }

    ark::protobuf::protobuf_table protobuf_table;
    protobuf_table.mutable_datas_value()->insert({event.row, protobuf_row});
    protobuf_entity->mutable_datas_table()->insert({event.table_index, protobuf_table});

    protobuf_data.set_id(entity_id);
    send_sync_msg(entity_id, mask_value, protobuf_data);

    return 0;
}

int kernel_module_impl::on_sync_table_delete(
    const guid_t& self, const table_event_data& event, const data_mask mask_value) {
    // find parent entity
    auto entity_ptr = get_entity(self);
    if (entity_ptr == nullptr)
    {
        ARK_LOG_ERROR("sync table failed entity do no exist, self={}", self);
        return -1;
    }

    ark::protobuf::protobuf_entity_table_delete protobuf_data;
    auto entity_id = self;
    auto protobuf_entity = protobuf_data.mutable_data();

    auto parent_container_ptr = entity_ptr->get_parent_container();
    if (parent_container_ptr != nullptr)
    {
        if (!try_add_container_protobuf_entity(
                parent_container_ptr, self, *protobuf_data.mutable_data(), entity_id, protobuf_entity))
        {
            ARK_LOG_ERROR("sync node failed container entity do no exist, self={}", self);
            return -1;
        }
    }

    ark::protobuf::protobuf_entity_data protobuf_row;
    ark::protobuf::protobuf_table protobuf_table;
    protobuf_table.mutable_datas_value()->insert({event.row, protobuf_row});
    protobuf_entity->mutable_datas_table()->insert({event.table_index, protobuf_table});

    send_sync_msg(entity_id, mask_value, protobuf_data);
    return 0;
}

int kernel_module_impl::on_sync_table_update(
    const guid_t& self, const table_event_data& event, const data_mask mask_value, const data& data) {
    // find parent entity
    auto entity_ptr = get_entity(self);
    if (entity_ptr == nullptr)
    {
        ARK_LOG_ERROR("sync table failed entity do no exist, self={}", self);
        return -1;
    }

    ark::protobuf::protobuf_entity_table_update protobuf_data;
    auto entity_id = self;
    auto protobuf_entity = protobuf_data.mutable_data();

    auto parent_container_ptr = entity_ptr->get_parent_container();
    if (parent_container_ptr != nullptr)
    {
        if (!try_add_container_protobuf_entity(
                parent_container_ptr, self, *protobuf_data.mutable_data(), entity_id, protobuf_entity))
        {
            ARK_LOG_ERROR("sync node failed container entity do no exist, self={}", self);
            return -1;
        }
    }

    ark::protobuf::protobuf_entity_data protobuf_row;
    if (!node_to_protobuf_data(event.data_index, data, &protobuf_row))
    {
        ARK_LOG_ERROR(
            "sync table failed table node do no exist, self={}, table={}, row={}", self, event.table_name, event.row);
        return -1;
    }

    ark::protobuf::protobuf_table protobuf_table;
    protobuf_table.mutable_datas_value()->insert({event.row, protobuf_row});
    protobuf_entity->mutable_datas_table()->insert({event.table_index, protobuf_table});

    protobuf_data.set_id(entity_id);
    send_sync_msg(entity_id, mask_value, protobuf_data);
    return 0;
}

int kernel_module_impl::on_sync_container_place(
    const guid_t& self, const uint32_t index, const data_mask mask, uint32_t src_index) {
    // find parent entity
    auto entity_ptr = get_entity(self);
    if (entity_ptr == nullptr)
    {
        ARK_LOG_ERROR("sync container failed entity do no exist, self={}", self);
        return -1;
    }

    auto container_ptr = entity_ptr->find_container(index);
    if (entity_ptr == nullptr)
    {
        ARK_LOG_ERROR("sync container failed container do no exist, self={}, container={}", self, index);
        return -1;
    }

    auto container_entity_ptr = container_ptr->find(src_index);
    if (container_entity_ptr == nullptr)
    {
        ARK_LOG_ERROR("sync container failed container entity do no exist, self={}, container={}, entity={}", self,
            index, src_index);
        return -1;
    }

    if (container_entity_ptr->is_sent())
    {
        ark::protobuf::protobuf_container_place protobuf_data;
        protobuf_data.set_id(self);
        protobuf_data.set_index(index);
        protobuf_data.set_entity_index(src_index);
        protobuf_data.set_entity_id(container_entity_ptr->get_id());

        send_sync_msg(self, mask, protobuf_data);
    }
    else
    {
        ark::protobuf::protobuf_container_create protobuf_data;
        if (!entity_to_protobuf_data(container_entity_ptr, protobuf_data.mutable_data()))
        {
            ARK_LOG_ERROR("sync container failed container entity to pb failed, self={}, container={}, entity={}", self,
                index, src_index);
            return -1;
        }

        protobuf_data.set_id(self);
        protobuf_data.set_index(index);
        protobuf_data.set_entity_index(src_index);

        send_sync_msg(self, mask, protobuf_data);
    }

    return 0;
}

int kernel_module_impl::on_sync_container_remove(
    const guid_t& self, const uint32_t index, const data_mask mask, uint32_t src_index) {
    // find parent entity
    auto entity_ptr = get_entity(self);
    if (entity_ptr == nullptr)
    {
        ARK_LOG_ERROR("sync container failed entity do no exist, self={}", self);
        return -1;
    }

    ark::protobuf::protobuf_container_remove protobuf_data;
    protobuf_data.set_id(self);
    protobuf_data.set_index(index);
    protobuf_data.set_entity_index(src_index);

    send_sync_msg(self, mask, protobuf_data);

    return 0;
}

int kernel_module_impl::on_sync_container_destroy(
    const guid_t& self, const uint32_t index, const data_mask mask, uint32_t src_index) {
    // find parent entity
    auto entity_ptr = get_entity(self);
    if (entity_ptr == nullptr)
    {
        ARK_LOG_ERROR("sync container failed entity do no exist, self={}", self);
        return -1;
    }

    ark::protobuf::protobuf_container_destroy protobuf_data;
    protobuf_data.set_id(self);
    protobuf_data.set_index(index);
    protobuf_data.set_entity_index(src_index);

    send_sync_msg(self, mask, protobuf_data);

    return 0;
}

int kernel_module_impl::on_sync_container_swap(
    const guid_t& self, const uint32_t index, const data_mask mask, uint32_t src_index, uint32_t dest_index) {
    // find parent entity
    auto entity_ptr = get_entity(self);
    if (entity_ptr == nullptr)
    {
        ARK_LOG_ERROR("sync container failed entity do no exist, self={}", self);
        return -1;
    }

    ark::protobuf::protobuf_container_swap protobuf_data;
    protobuf_data.set_id(self);
    protobuf_data.set_index(index);
    protobuf_data.set_src_index(src_index);
    protobuf_data.set_dest_index(dest_index);

    send_sync_msg(self, mask, protobuf_data);

    return 0;
}

bool kernel_module_impl::delay_table_to_protobuf(const delay_sync_table& table, const uint32_t index,
    ark::protobuf::protobuf_delay_entity& data, ark::protobuf::protobuf_entity_data& protobuf_entity) {
    ark::protobuf::protobuf_table protobuf_table;
    ark::protobuf::delay_clear_row clear_row_list;
    for (auto& iter : table.row_list)
    {
        ark::protobuf::protobuf_entity_data protobuf_row;

        auto row_index = iter.first;
        auto& row_data = iter.second;
        for (auto& iter_row : row_data.node_list)
        {
            auto node_ptr = iter_row;
            if (node_to_protobuf_data(node_ptr, &protobuf_row))
            {
                protobuf_table.mutable_datas_value()->insert({row_index, protobuf_row});
            }
        }

        if (row_data.need_clear)
        {
            clear_row_list.add_row_list(row_index);
        }
    }

    if (table.need_clear)
    {
        data.add_clear_tables(index);
    }

    if (clear_row_list.row_list_size() > 0)
    {
        data.mutable_clear_rows()->insert({index, clear_row_list});
    }

    protobuf_entity.mutable_datas_table()->insert({index, protobuf_table});

    return true;
}

bool kernel_module_impl::delay_container_to_protobuf(std::shared_ptr<entity> entity_ptr,
    const delay_sync_container& container, const uint32_t index, ark::protobuf::protobuf_delay_entity& data) {
    ark::protobuf::delay_container protobuf_delay_container;
    ark::protobuf::protobuf_container protobuf_conatiner;
    for (auto& iter : container.index_list)
    {
        auto entity_index = iter;
        auto container_ptr = entity_ptr->find_container(index);
        if (container_ptr == nullptr)
        {
            continue;
        }

        auto container_entity_ptr = container_ptr->find(entity_index);
        if (container_entity_ptr == nullptr)
        {
            protobuf_delay_container.mutable_entity_list()->insert({entity_index, null_guid});
        }
        else if (entity_ptr->is_sent())
        {
            protobuf_delay_container.mutable_entity_list()->insert({entity_index, container_entity_ptr->get_id()});
        }
        else
        {
            ark::protobuf::protobuf_entity protobuf_container_entity;
            if (!entity_to_protobuf_data(container_entity_ptr, &protobuf_container_entity))
            {
                continue;
            }

            protobuf_conatiner.mutable_datas_value()->insert({entity_index, protobuf_container_entity});
        }
    }

    if (protobuf_conatiner.datas_value_size() > 0)
    {
        data.mutable_data()->mutable_datas_container()->insert({index, protobuf_conatiner});
    }
    data.mutable_container_entity()->insert({index, protobuf_delay_container});

    ark::protobuf::delay_container protobuf_destroy_entity;
    for (auto& iter : container.destroy_list)
    {
        auto entity_index = iter;
        protobuf_destroy_entity.mutable_entity_list()->insert({entity_index, null_guid});
    }

    data.mutable_destroy_entity()->insert({index, protobuf_destroy_entity});

    return true;
}

bool kernel_module_impl::try_add_container_protobuf_entity(std::shared_ptr<container> container_ptr, const guid_t& self,
    ark::protobuf::protobuf_entity_data& protobuf_entity_data, guid_t& parent_id,
    ark::protobuf::protobuf_entity_data*& protobuf_container_entity) {
    parent_id = container_ptr->get_parent_id();
    auto parent_entity_ptr = get_entity(parent_id);
    if (parent_entity_ptr == nullptr)
    {
        ARK_LOG_ERROR("parent entity do no exist, parent={}", parent_id);
        return false;
    }

    auto self_index = container_ptr->find(self);
    if (self_index == 0)
    {
        ARK_LOG_ERROR("entity is not in container, self={}", self);
        return false;
    }

    ark::protobuf::protobuf_container protobuf_container;
    auto result_container =
        protobuf_entity_data.mutable_datas_container()->insert({container_ptr->get_index(), protobuf_container});
    if (!result_container.second)
    {
        ARK_LOG_ERROR("entity insert container failed, self={} container index = {}", self, container_ptr->get_index());
        return false;
    }
    auto& container = result_container.first->second;

    ark::protobuf::protobuf_entity protobuf_entity;
    auto result_entity = container.mutable_datas_value()->insert({self_index, protobuf_entity});
    if (!result_entity.second)
    {
        ARK_LOG_ERROR("container insert entity failed, self={} container index = {}", self, container_ptr->get_index());
        return false;
    }

    auto& entity = result_entity.first->second;
    protobuf_container_entity = entity.mutable_data();
    return true;
}

int kernel_module_impl::send_sync_msg(
    const guid_t& self, const data_mask mask_value, const google::protobuf::Message& msg) {
    auto iter = sync_functors_.find(mask_value);
    if (iter == sync_functors_.end())
    {
        return -1;
    }

    iter->second(self, msg);

    return 0;
}

int kernel_module_impl::send_to_view(const guid_t& self, const google::protobuf::Message& msg) {
    auto entity_ptr = get_entity(self);
    if (nullptr == entity_ptr)
    {
        return -1;
    }

    auto map_id = entity_ptr->get_map_id();
    auto inst_id = entity_ptr->get_map_entity_id();

    data_list_impl map_inst_entity_list;
    map_module_->get_inst_entity_list(map_id, inst_id, map_inst_entity_list);

    for (size_t i = 0; i < map_inst_entity_list.get_count(); i++)
    {
        auto view_entity_ptr = get_entity(map_inst_entity_list.int64(i));
        if (view_entity_ptr == nullptr)
        {
            continue;
        }

        const std::string& obj_class_name = view_entity_ptr->get_class_name();
        if (entity_meta_player::self_name() == obj_class_name)
        {
            send_to_self(view_entity_ptr->get_id(), msg);
        }
    }

    return 0;
}

int kernel_module_impl::send_to_self(const guid_t& self, const google::protobuf::Message& msg) {
    //SendMsgPBToGate(ark::protobuf::egmi_ack_node_data, entity, ident);
    return 0;
}

bool kernel_module_impl::do_event(
    const guid_t& self, const std::string& class_name, entity_event class_event, const data_list& args) {
    return class_meta_module_->do_class_event(self, class_name, class_event, args);
}

bool kernel_module_impl::do_event(const guid_t& self, const int event_id, const data_list& args) {
    std::shared_ptr<entity> entity_ptr = get_entity(self);
    ARK_ASSERT_RET_VAL(entity_ptr != nullptr, false);

    auto event_manager_ptr = get_event_manager(entity_ptr);
    ARK_ASSERT_RET_VAL(event_manager_ptr != nullptr, false);

    return event_manager_ptr->do_event(event_id, args);
}

bool kernel_module_impl::exist(const guid_t& self) {
    return (objects_.find_value(self) != nullptr);
}

bool kernel_module_impl::log_self_info(const guid_t& id) {
    return false;
}

int kernel_module_impl::log_object_data(const guid_t& guid) {
    auto entity = get_entity(guid);
    if (entity == nullptr)
    {
        return -1;
    }

    auto node_manager_ptr = get_node_manager(entity);
    ARK_ASSERT_RET_VAL(node_manager_ptr != nullptr, -1);

    auto table_manager_ptr = get_table_manager(entity);
    ARK_ASSERT_RET_VAL(table_manager_ptr != nullptr, -1);

    auto& node_list = node_manager_ptr->get_data_list();
    for (auto& iter : node_list)
    {
        auto data_ptr = iter.second;
        if (!data_ptr)
        {
            continue;
        }

        ARK_LOG_TRACE("Player[{}] Node[{}] Value[{}]", guid, data_ptr->get_name(), data_ptr->to_string());
    }

    auto& table_list = table_manager_ptr->get_table_list();
    for (auto& iter : table_list)
    {
        auto table_ptr = iter.second;
        if (!table_ptr)
        {
            continue;
        }

        for (auto row_ptr = table_ptr->first(); row_ptr != nullptr; row_ptr = table_ptr->next())
        {
            auto row_node_manager_ptr = get_node_manager(row_ptr);
            if (!row_node_manager_ptr)
            {
                continue;
            }

            auto& row_data_list = row_node_manager_ptr->get_data_list();
            for (auto& iter_row : row_data_list)
            {
                auto node_ptr = iter_row.second;
                if (!node_ptr)
                {
                    continue;
                }

                ARK_LOG_TRACE("Player[{}] Table[{}] Row[{}] Col[{}] Value[{}]", guid, table_ptr->get_name(),
                    row_ptr->get_row(), node_ptr->get_name(), node_ptr->to_string());
            }
        }
    }

    return 0;
}

bool kernel_module_impl::log_info(const guid_t& id) {
    std::shared_ptr<entity> entity_ptr = get_entity(id);
    if (entity_ptr == nullptr)
    {
        ARK_LOG_ERROR("Cannot find entity, id = {}", id);
        return false;
    }

    if (map_module_->is_in_map_instance(id))
    {
        int map_id = entity_ptr->get_map_id();

        ARK_LOG_INFO("----------child object list-------- , id = {} mapid = {}", id, map_id);
        data_list_impl entity_list;
        int online_count = map_module_->get_map_online_list(map_id, entity_list);
        for (int i = 0; i < online_count; ++i)
        {
            guid_t target_entity_id = entity_list.int64(i);
            ARK_LOG_INFO("id = {} mapid = {}", target_entity_id, map_id);
        }
    }
    else
    {
        ARK_LOG_INFO("---------print object start--------, id = {}", id);
        ARK_LOG_INFO("---------print object end--------, id = {}", id);
    }

    return true;
}

//--------------entity to pb db data------------------
bool kernel_module_impl::entity_to_db_data(const guid_t& self, ark::protobuf::protobuf_db_entity& protobuf_data) {
    std::shared_ptr<entity> entity_ptr = get_entity(self);
    return entity_to_db_data(entity_ptr, protobuf_data);
}

bool kernel_module_impl::entity_to_db_data(
    std::shared_ptr<entity> entity_ptr, ark::protobuf::protobuf_db_entity& protobuf_data) {
    ARK_ASSERT_RET_VAL(entity_ptr != nullptr, false);

    auto node_manager_ptr = get_node_manager(entity_ptr);
    ARK_ASSERT_RET_VAL(node_manager_ptr != nullptr, false);

    auto table_manager_ptr = get_table_manager(entity_ptr);
    ARK_ASSERT_RET_VAL(table_manager_ptr != nullptr, false);

    auto container_manager_ptr = get_container_manager(entity_ptr);
    ARK_ASSERT_RET_VAL(container_manager_ptr != nullptr, false);

    protobuf_data.set_id(entity_ptr->get_id());
    protobuf_data.set_config_id(entity_ptr->get_config_id());
    protobuf_data.set_map_id(entity_ptr->get_map_id());
    protobuf_data.set_map_inst_id(entity_ptr->get_map_entity_id());
    protobuf_data.set_class_name(entity_ptr->get_class_name());

    // node to db
    auto& node_list = node_manager_ptr->get_data_list();
    for (auto& iter : node_list)
    {
        auto node_ptr = iter.second;
        if (!node_ptr)
        {
            continue;
        }

        if (!node_ptr->have_mask(data_mask::pf_save))
        {
            continue;
        }

        node_to_db_data(node_ptr, *protobuf_data.mutable_data());
    }

    // table to db
    auto& table_list = table_manager_ptr->get_table_list();
    for (auto& iter : table_list)
    {
        auto table_ptr = iter.second;
        if (!table_ptr)
        {
            continue;
        }

        if (!table_ptr->have_mask(data_mask::pf_save))
        {
            continue;
        }

        ark::protobuf::protobuf_db_table protobuf_table;
        if (!table_to_db_data(table_ptr, protobuf_table))
        {
            continue;
        }

        protobuf_data.mutable_data()->mutable_datas_table()->insert({table_ptr->get_name(), protobuf_table});
    }

    // container to db
    auto& container_list = container_manager_ptr->get_container_list();
    for (auto& iter : container_list)
    {
        auto container_ptr = iter.second;
        if (!container_ptr)
        {
            continue;
        }

        ark::protobuf::protobuf_db_container protobuf_container;
        for (auto index = container_ptr->first(); index > 0; index = container_ptr->next())
        {
            auto sub_entity_ptr = container_ptr->find(index);
            if (!sub_entity_ptr)
            {
                continue;
            }

            ark::protobuf::protobuf_db_entity protobuf_container_entity;
            if (!entity_to_db_data(sub_entity_ptr, protobuf_container_entity))
            {
                continue;
            }

            protobuf_container.mutable_datas_value()->insert({index, protobuf_container_entity});
        }

        if (protobuf_container.datas_value_size() > 0)
        {
            protobuf_data.mutable_data()->mutable_datas_entity()->insert(
                {container_ptr->get_name(), protobuf_container});
        }
    }

    return true;
}

std::shared_ptr<entity> kernel_module_impl::create_entity(const ark::protobuf::protobuf_db_entity& protobuf_data) {
    auto entity_id = protobuf_data.id();
    auto entity_ptr = get_entity(entity_id);
    if (entity_ptr != nullptr)
    {
        ARK_LOG_ERROR("entity already exists, object = {}", entity_id);
        return nullptr;
    }

    const std::string& class_name = protobuf_data.class_name();
    auto class_meta_ptr = class_meta_module_->find_meta(class_name);
    if (nullptr == class_meta_ptr)
    {
        ARK_LOG_ERROR("There is no class meta, name = {}", class_name);
        return nullptr;
    }

    auto map_id = protobuf_data.map_id();
    auto map_inst_id = protobuf_data.map_inst_id();
    auto map_info_ptr = map_module_->get_map_info(map_id);
    if (map_info_ptr == nullptr)
    {
        ARK_LOG_ERROR("There is no scene, scene = {}", map_id);
        return nullptr;
    }

    auto entity_impl_ptr =
        std::make_shared<entity_impl>(class_meta_ptr, entity_id, null_int, map_id, map_inst_id, data_list_impl());
    entity_ptr = std::static_pointer_cast<entity>(entity_impl_ptr);

    objects_.insert(entity_id, entity_ptr);

    if (class_name == entity_meta_player::self_name())
    {
        map_info_ptr->add_entity_to_instance(map_inst_id, entity_id, true);
    }
    //else if (class_name == AFEntityMetaPlayer::self_name()) // to do : npc type for now we do not have
    //{
    //}

    // init data
    auto& protobuf_db_entity_data = protobuf_data.data();

    // node data
    auto node_manager_ptr = entity_impl_ptr->get_node_manager();
    if (nullptr != node_manager_ptr)
    {
        db_data_to_node(node_manager_ptr, protobuf_db_entity_data);
    }

    // table data
    auto table_manager_ptr = entity_impl_ptr->get_table_manager();
    if (nullptr != table_manager_ptr)
    {
        for (auto& iter : protobuf_db_entity_data.datas_table())
        {
            db_data_to_table(entity_ptr, iter.first, iter.second);
        }
    }

    // container data
    for (auto& iter : protobuf_db_entity_data.datas_entity())
    {
        db_data_to_container(entity_ptr, iter.first, iter.second);
    }

    // todo : add new event?
    data_list_impl args;
    do_event(entity_id, class_name, entity_event::entity_evt_pre_load_data, args);

    do_event(entity_id, class_name, entity_event::entity_evt_load_data, args);
    do_event(entity_id, class_name, entity_event::entity_evt_pre_effect_data, args);
    do_event(entity_id, class_name, entity_event::entity_evt_effect_data, args);
    do_event(entity_id, class_name, entity_event::entity_evt_post_effect_data, args);
    do_event(entity_id, class_name, entity_event::entity_evt_data_finished, args);

    return entity_ptr;
}

bool kernel_module_impl::send_custom_message(const guid_t& target, const uint32_t msg_id, const data_list& args) {
    ARK_ASSERT_RET_VAL(exist(target) && msg_id > 0, false);

    ark::protobuf::protobuf_custom_message custom_message;
    custom_message.set_message_id(msg_id);

    size_t count = args.get_count();
    for (size_t i = 0; i < count; i++)
    {
        auto data_type = args.get_type(i);
        switch (data_type)
        {
        case ark::data_type::dt_boolean:
            custom_message.add_data_list()->set_bool_value(args.get_bool(i));
            break;
        case ark::data_type::dt_int32:
            custom_message.add_data_list()->set_int_value(args.get_int(i));
            break;
        case ark::data_type::dt_uint32:
            custom_message.add_data_list()->set_uint_value(args.u_int(i));
            break;
        case ark::data_type::dt_int64:
            custom_message.add_data_list()->set_int64_value(args.int64(i));
            break;
        case ark::data_type::dt_uint64:
            custom_message.add_data_list()->set_uint64_value(args.u_int64(i));
            break;
        case ark::data_type::dt_float:
            custom_message.add_data_list()->set_float_value(args.get_float(i));
            break;
        case ark::data_type::dt_double:
            custom_message.add_data_list()->set_double_value(args.get_double(i));
            break;
        case ark::data_type::dt_string:
            custom_message.add_data_list()->set_str_value(args.string(i));
            break;
        default:
            break;
        }
    }

    // send message

    return true;
}

// pb table to entity table
bool kernel_module_impl::db_data_to_table(std::shared_ptr<entity> entity_ptr, const std::string& name,
    const ark::protobuf::protobuf_db_table& protobuf_table) {
    auto table_ptr = entity_ptr->find_table(name);
    ARK_ASSERT_RET_VAL(table_ptr != nullptr, false);

    auto table_impl_ptr = dynamic_cast<table_impl*>(table_ptr);
    ARK_ASSERT_RET_VAL(table_impl_ptr != nullptr, false);

    for (auto& iter : protobuf_table.datas_value())
    {
        auto row_index = iter.first;
        if (row_index == null_int)
        {
            continue;
        }

        auto& protobuf_db_entity_data = iter.second;
        auto row_ptr = table_impl_ptr->create_row(row_index, data_list_impl());
        if (row_ptr == nullptr)
        {
            continue;
        }

        auto node_manager_ptr = get_node_manager(row_ptr);
        if (node_manager_ptr == nullptr)
        {
            continue;
        }

        db_data_to_node(node_manager_ptr, protobuf_db_entity_data);
    }

    return true;
}

bool kernel_module_impl::db_data_to_container(std::shared_ptr<entity> entity_ptr, const std::string& name,
    const ark::protobuf::protobuf_db_container& protobuf_data) {
    auto container_ptr = entity_ptr->find_container(name);
    if (nullptr == container_ptr)
    {
        return false;
    }

    auto container_impl_ptr = std::dynamic_pointer_cast<container_impl>(container_ptr);
    if (nullptr == container_impl_ptr)
    {
        return false;
    }

    for (auto& iter : protobuf_data.datas_value())
    {
        auto container_entity_ptr = create_entity(iter.second);
        if (nullptr == container_entity_ptr)
        {
            continue;
        }

        container_impl_ptr->init_entity_list(iter.first, container_entity_ptr);
    }

    return true;
}

int kernel_module_impl::on_container_call_back(const guid_t& self, const uint32_t index,
    const container_op_type op_type, const uint32_t src_index, const uint32_t dest_index) {
    if (op_type == container_op_type::op_destroy)
    {
        // destroy entity
        auto entity_ptr = get_entity(self);
        ARK_ASSERT_RET_VAL(entity_ptr != nullptr, 0);

        auto container_ptr = entity_ptr->find_container(index);
        ARK_ASSERT_RET_VAL(container_ptr != nullptr, 0);

        auto container_entity_ptr = container_ptr->find(src_index);
        ARK_ASSERT_RET_VAL(container_entity_ptr != nullptr, 0);

        inner_destroy_entity(container_entity_ptr);
    }

    return 0;
}

bool kernel_module_impl::db_data_to_node(std::shared_ptr<node_manager> node_manager_ptr,
    const ark::protobuf::protobuf_db_entity_data& protobuf_db_entity_data) {
    //bool data
    for (auto& iter : protobuf_db_entity_data.datas_bool())
    {
        auto node_ptr = node_manager_ptr->create_data(iter.first);
        if (node_ptr == nullptr)
        {
            continue;
        }

        node_ptr->set_bool(iter.second);
    }

    //int32 data
    for (auto& iter : protobuf_db_entity_data.datas_int32())
    {
        auto node_ptr = node_manager_ptr->create_data(iter.first);
        if (node_ptr == nullptr)
        {
            continue;
        }

        node_ptr->set_int32(iter.second);
    }

    //uint32 data
    for (auto& iter : protobuf_db_entity_data.datas_uint32())
    {
        auto node_ptr = node_manager_ptr->create_data(iter.first);
        if (node_ptr == nullptr)
        {
            continue;
        }

        node_ptr->set_u_int32(iter.second);
    }

    //int64 data
    for (auto& iter : protobuf_db_entity_data.datas_int64())
    {
        auto node_ptr = node_manager_ptr->create_data(iter.first);
        if (node_ptr == nullptr)
        {
            continue;
        }

        node_ptr->set_int64(iter.second);
    }

    //uint64 data
    for (auto& iter : protobuf_db_entity_data.datas_uint64())
    {
        auto node_ptr = node_manager_ptr->create_data(iter.first);
        if (node_ptr == nullptr)
        {
            continue;
        }

        node_ptr->set_u_int64(iter.second);
    }

    //float data
    for (auto& iter : protobuf_db_entity_data.datas_float())
    {
        auto node_ptr = node_manager_ptr->create_data(iter.first);
        if (node_ptr == nullptr)
        {
            continue;
        }

        node_ptr->set_float(iter.second);
    }

    //double data
    for (auto& iter : protobuf_db_entity_data.datas_double())
    {
        auto node_ptr = node_manager_ptr->create_data(iter.first);
        if (node_ptr == nullptr)
        {
            continue;
        }

        node_ptr->set_double(iter.second);
    }

    //string data
    for (auto& iter : protobuf_db_entity_data.datas_string())
    {
        auto node_ptr = node_manager_ptr->create_data(iter.first);
        if (node_ptr == nullptr)
        {
            continue;
        }

        node_ptr->set_string(iter.second);
    }

    return true;
}

//-----------------------------
bool kernel_module_impl::node_to_db_data(node* node_ptr, ark::protobuf::protobuf_db_entity_data& protobuf_data) {
    ARK_ASSERT_RET_VAL(node_ptr != nullptr, false);

    auto& name = node_ptr->get_name();
    switch (node_ptr->get_type())
    {
    case data_type::dt_boolean:
        protobuf_data.mutable_datas_bool()->insert({name, node_ptr->get_bool()});
        break;
    case data_type::dt_int32:
        protobuf_data.mutable_datas_int32()->insert({name, node_ptr->get_int32()});
        break;
    case data_type::dt_uint32:
        protobuf_data.mutable_datas_uint32()->insert({name, node_ptr->get_u_int32()});
        break;
    case data_type::dt_int64:
        protobuf_data.mutable_datas_int64()->insert({name, node_ptr->get_int64()});
        break;
    case data_type::dt_uint64:
        protobuf_data.mutable_datas_uint64()->insert({name, node_ptr->get_u_int64()});
        break;
    case data_type::dt_float:
        protobuf_data.mutable_datas_float()->insert({name, node_ptr->get_float()});
        break;
    case data_type::dt_double:
        protobuf_data.mutable_datas_double()->insert({name, node_ptr->get_double()});
        break;
    case data_type::dt_string:
        protobuf_data.mutable_datas_string()->insert({name, node_ptr->get_string()});
        break;
    default:
        ARK_ASSERT_RET_VAL(0, false);
        break;
    }

    return true;
}

bool kernel_module_impl::table_to_db_data(table* table_ptr, ark::protobuf::protobuf_db_table& protobuf_data) {
    ARK_ASSERT_RET_VAL(table_ptr != nullptr, false);

    for (auto row_ptr = table_ptr->first(); row_ptr != nullptr; row_ptr = table_ptr->next())
    {
        auto node_manager_ptr = get_node_manager(row_ptr);
        if (!node_manager_ptr)
        {
            continue;
        }

        ark::protobuf::protobuf_db_entity_data row_data;

        auto& data_list = node_manager_ptr->get_data_list();
        for (auto& iter : data_list)
        {
            node_to_db_data(iter.second, row_data);
        }

        protobuf_data.mutable_datas_value()->insert({row_ptr->get_row(), row_data});
    }

    return true;
}

//----------entity to pb client data---------------
bool kernel_module_impl::node_to_protobuf_data(node* node_ptr, ark::protobuf::protobuf_entity_data* protobuf_data) {
    ARK_ASSERT_RET_VAL(node_ptr != nullptr && protobuf_data != nullptr, false);

    auto index = node_ptr->get_index();
    switch (node_ptr->get_type())
    {
    case data_type::dt_boolean:
        protobuf_data->mutable_datas_bool()->insert({index, node_ptr->get_bool()});
        break;
    case data_type::dt_int32:
        protobuf_data->mutable_datas_int32()->insert({index, node_ptr->get_int32()});
        break;
    case data_type::dt_uint32:
        protobuf_data->mutable_datas_uint32()->insert({index, node_ptr->get_u_int32()});
        break;
    case data_type::dt_int64:
        protobuf_data->mutable_datas_int64()->insert({index, node_ptr->get_int64()});
        break;
    case data_type::dt_uint64:
        protobuf_data->mutable_datas_uint64()->insert({index, node_ptr->get_u_int64()});
        break;
    case data_type::dt_float:
        protobuf_data->mutable_datas_float()->insert({index, node_ptr->get_float()});
        break;
    case data_type::dt_double:
        protobuf_data->mutable_datas_double()->insert({index, node_ptr->get_double()});
        break;
    case data_type::dt_string:
        protobuf_data->mutable_datas_string()->insert({index, node_ptr->get_string()});
        break;
    default:
        ARK_ASSERT_RET_VAL(0, false);
        break;
    }

    return true;
}

bool kernel_module_impl::node_to_protobuf_data(
    const uint32_t index, const data& data, ark::protobuf::protobuf_entity_data* protobuf_data) {
    ARK_ASSERT_RET_VAL(index > 0 && protobuf_data != nullptr, false);

    switch (data.get_type())
    {
    case data_type::dt_boolean:
        protobuf_data->mutable_datas_bool()->insert({index, data.get_bool()});
        break;
    case data_type::dt_int32:
        protobuf_data->mutable_datas_int32()->insert({index, data.get_int()});
        break;
    case data_type::dt_uint32:
        protobuf_data->mutable_datas_uint32()->insert({index, data.get_u_int()});
        break;
    case data_type::dt_int64:
        protobuf_data->mutable_datas_int64()->insert({index, data.get_int64()});
        break;
    case data_type::dt_uint64:
        protobuf_data->mutable_datas_uint64()->insert({index, data.get_u_int64()});
        break;
    case data_type::dt_float:
        protobuf_data->mutable_datas_float()->insert({index, data.get_float()});
        break;
    case data_type::dt_double:
        protobuf_data->mutable_datas_double()->insert({index, data.get_double()});
        break;
    case data_type::dt_string:
        protobuf_data->mutable_datas_string()->insert({index, data.get_string()});
        break;
    default:
        ARK_ASSERT_RET_VAL(0, false);
        break;
    }

    return true;
}

bool kernel_module_impl::table_to_protobuf_data(
    table* table_ptr, const uint32_t index, ark::protobuf::protobuf_table* protobuf_data) {
    ARK_ASSERT_RET_VAL(table_ptr != nullptr && index > 0 && protobuf_data != nullptr, false);

    for (row* row_ptr = table_ptr->first(); row_ptr != nullptr; row_ptr = table_ptr->next())
    {
        ark::protobuf::protobuf_entity_data row_data;
        if (!row_to_protobuf_data(row_ptr, row_ptr->get_row(), &row_data))
        {
            continue;
        }

        protobuf_data->mutable_datas_value()->insert({index, row_data});
    }

    return true;
}

bool kernel_module_impl::container_to_protobuf_data(
    std::shared_ptr<container> container_ptr, ark::protobuf::protobuf_container* protobuf_data) {
    ARK_ASSERT_RET_VAL(container_ptr != nullptr && protobuf_data != nullptr, false);

    for (auto index = container_ptr->first(); index > 0; index = container_ptr->next())
    {
        auto entity_ptr = container_ptr->find(index);
        if (nullptr == entity_ptr)
        {
            continue;
        }

        ark::protobuf::protobuf_entity protobuf_entity;
        if (!entity_to_protobuf_data(entity_ptr, &protobuf_entity))
        {
            continue;
        }

        protobuf_data->mutable_datas_value()->insert({index, protobuf_entity});
    }

    return true;
}

bool kernel_module_impl::row_to_protobuf_data(
    row* row_ptr, const uint32_t index, ark::protobuf::protobuf_entity_data* protobuf_data) {
    ARK_ASSERT_RET_VAL(row_ptr != nullptr && index > 0 && protobuf_data != nullptr, false);

    auto node_manager_ptr = get_node_manager(row_ptr);
    if (!node_manager_ptr)
    {
        return false;
    }

    auto& data_list = node_manager_ptr->get_data_list();
    for (auto& iter : data_list)
    {
        node_to_protobuf_data(iter.second, protobuf_data);
    }

    return true;
}

bool kernel_module_impl::table_row_data_to_protobuf_data(const uint32_t index, uint32_t row, const uint32_t col,
    const data& data, ark::protobuf::protobuf_entity_data* protobuf_data) {
    ARK_ASSERT_RET_VAL(index > 0 && row > 0 && col > 0 && protobuf_data != nullptr, false);

    ark::protobuf::protobuf_entity_data row_data;
    if (!node_to_protobuf_data(col, data, &row_data))
    {
        return false;
    }

    ark::protobuf::protobuf_table table_data;
    table_data.mutable_datas_value()->insert({row, row_data});

    protobuf_data->mutable_datas_table()->insert({index, table_data});

    return true;
}

bool kernel_module_impl::entity_to_protobuf_data(
    std::shared_ptr<entity> entity_ptr, ark::protobuf::protobuf_entity* protobuf_data) {
    ARK_ASSERT_RET_VAL(entity_ptr != nullptr && protobuf_data != nullptr, false);

    protobuf_data->set_id(entity_ptr->get_id());

    entity_node_to_protobuf_data(entity_ptr, protobuf_data->mutable_data());

    entity_table_to_protobuf_data(entity_ptr, protobuf_data->mutable_data());

    entity_container_to_protobuf_data(entity_ptr, protobuf_data->mutable_data());

    return true;
}

bool kernel_module_impl::entity_to_protobuf_data_by_mask(
    std::shared_ptr<entity> entity_ptr, mask_type mask, ark::protobuf::protobuf_entity* protobuf_data) {
    ARK_ASSERT_RET_VAL(entity_ptr != nullptr && protobuf_data != nullptr, false);

    protobuf_data->set_id(entity_ptr->get_id());

    entity_node_to_protobuf_data(entity_ptr, protobuf_data->mutable_data(), mask);

    entity_table_to_protobuf_data(entity_ptr, protobuf_data->mutable_data(), mask);

    entity_container_to_protobuf_data(entity_ptr, protobuf_data->mutable_data(), mask);

    return true;
}

bool kernel_module_impl::entity_container_to_protobuf_data(
    std::shared_ptr<entity> entity_ptr, ark::protobuf::protobuf_entity_data* protobuf_data, const mask_type mask) {
    ARK_ASSERT_RET_VAL(entity_ptr != nullptr && protobuf_data != nullptr, false);

    auto container_manager_ptr = get_container_manager(entity_ptr);
    ARK_ASSERT_RET_VAL(container_manager_ptr != nullptr, false);

    auto& container_list = container_manager_ptr->get_container_list();
    for (auto& iter : container_list)
    {
        auto container_ptr = iter.second;
        if (!container_ptr)
        {
            continue;
        }

        if (!mask.none())
        {
            auto result = (container_ptr->get_mask() & mask);
            if (!result.any())
            {
                continue;
            }
        }

        ark::protobuf::protobuf_container protobuf_container;
        if (!container_to_protobuf_data(container_ptr, &protobuf_container))
        {
            continue;
        }

        protobuf_data->mutable_datas_container()->insert({iter.first, protobuf_container});
    }

    return true;
}

//node all to pb data
bool kernel_module_impl::entity_node_to_protobuf_data(std::shared_ptr<entity> entity_ptr,
    ark::protobuf::protobuf_entity_data* protobuf_data, const mask_type mask /* = 0*/) {
    ARK_ASSERT_RET_VAL(entity_ptr != nullptr && protobuf_data != nullptr, false);

    auto node_manager_ptr = get_node_manager(entity_ptr);
    ARK_ASSERT_RET_VAL(node_manager_ptr != nullptr, false);

    auto& data_list = node_manager_ptr->get_data_list();
    for (auto& iter : data_list)
    {
        auto node_ptr = iter.second;
        if (!node_ptr)
        {
            continue;
        }

        if (!mask.none())
        {
            auto result = (node_ptr->get_mask() & mask);
            if (!result.any())
            {
                continue;
            }
        }

        node_to_protobuf_data(node_ptr, protobuf_data);
    }

    return true;
}

bool kernel_module_impl::entity_table_to_protobuf_data(std::shared_ptr<entity> entity_ptr,
    ark::protobuf::protobuf_entity_data* protobuf_data, const mask_type mask /* = 0*/) {
    ARK_ASSERT_RET_VAL(entity_ptr != nullptr && protobuf_data != nullptr, false);

    auto table_manager_ptr = get_table_manager(entity_ptr);
    ARK_ASSERT_RET_VAL(table_manager_ptr != nullptr, false);

    auto& data_list = table_manager_ptr->get_table_list();
    for (auto& iter : data_list)
    {
        auto table_ptr = iter.second;
        if (!table_ptr)
        {
            continue;
        }

        if (!mask.none())
        {
            auto result = (table_ptr->get_mask() & mask);
            if (!result.any())
            {
                continue;
            }
        }

        const auto index = table_ptr->get_index();

        ark::protobuf::protobuf_table table_data;
        if (!table_to_protobuf_data(table_ptr, index, &table_data))
        {
            continue;
        }

        protobuf_data->mutable_datas_table()->insert({index, table_data});
    }

    return true;
}

// -----------get entity manager--------------
std::shared_ptr<node_manager> kernel_module_impl::get_node_manager(
    std::shared_ptr<static_entity> static_entity_ptr) const {
    if (static_entity_ptr == nullptr)
    {
        return nullptr;
    }

    auto static_entity_impl_ptr = std::dynamic_pointer_cast<static_entity_impl>(static_entity_ptr);
    if (static_entity_impl_ptr == nullptr)
    {
        return nullptr;
    }

    return static_entity_impl_ptr->get_node_manager();
}

std::shared_ptr<node_manager> kernel_module_impl::get_node_manager(std::shared_ptr<entity> entity_ptr) const {
    if (entity_ptr == nullptr)
    {
        return nullptr;
    }

    auto entity_impl_ptr = std::dynamic_pointer_cast<entity_impl>(entity_ptr);
    if (entity_impl_ptr == nullptr)
    {
        return nullptr;
    }

    return entity_impl_ptr->get_node_manager();
}

std::shared_ptr<node_manager> kernel_module_impl::get_node_manager(row* row_ptr) const {
    if (row_ptr == nullptr)
    {
        return nullptr;
    }

    auto row_impl_ptr = dynamic_cast<row_impl*>(row_ptr);
    if (row_impl_ptr == nullptr)
    {
        return nullptr;
    }

    return row_impl_ptr->get_node_manager();
}

std::shared_ptr<table_manager> kernel_module_impl::get_table_manager(std::shared_ptr<entity> entity_ptr) const {
    if (entity_ptr == nullptr)
    {
        return nullptr;
    }

    auto entity_impl_ptr = std::dynamic_pointer_cast<entity_impl>(entity_ptr);
    if (entity_impl_ptr == nullptr)
    {
        return nullptr;
    }

    return entity_impl_ptr->get_table_manager();
}

std::shared_ptr<container_manager> kernel_module_impl::get_container_manager(std::shared_ptr<entity> entity_ptr) const {
    if (entity_ptr == nullptr)
    {
        return nullptr;
    }

    auto entity_impl_ptr = std::dynamic_pointer_cast<entity_impl>(entity_ptr);
    if (entity_impl_ptr == nullptr)
    {
        return nullptr;
    }

    return entity_impl_ptr->get_container_manager();
}

std::shared_ptr<event_manager> kernel_module_impl::get_event_manager(std::shared_ptr<entity> entity_ptr) const {
    if (entity_ptr == nullptr)
    {
        return nullptr;
    }

    auto entity_impl_ptr = std::dynamic_pointer_cast<entity_impl>(entity_ptr);
    if (entity_impl_ptr == nullptr)
    {
        return nullptr;
    }

    return entity_impl_ptr->get_event_manager();
}

} // namespace ark
