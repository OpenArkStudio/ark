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

#include "simulate_module_factory.hpp"
#include "simulate_net_module_impl.hpp"

namespace ark {

std::unique_ptr<simulate_net_module> detail::make_simulate_net_module() {
    return std::make_unique<simulate_net_module_impl>();
}

//method realization

bool simulate_net_module_impl::init() {
    simulate_module_ = get_plugin_manager()->try_find_module<simulate_module>();
    bus_module_ = get_plugin_manager()->try_find_module<bus_module>();
    net_service_manager_module_ = get_plugin_manager()->try_find_module<net_service_manager_module>();

    if (bus_module_ == nullptr || net_service_manager_module_ == nullptr || simulate_module_ == nullptr)
    {
        ARK_LOG_ERROR("simulate network startup requires Bus, Net and Simulate modules");
        return false;
    }

    simulate_module_->register_trigger_call_back(this, &simulate_net_module_impl::trigger_call_back);

    return true;
}

bool simulate_net_module_impl::post_init() {
    return start_server();
}

bool simulate_net_module_impl::start_server() {
    auto ret = net_service_manager_module_->create_server();
    if (!ret)
    {
        ARK_LOG_ERROR("Cannot start server net, busid = {}", bus_module_->get_self_bus_name());
        return false;
    }

    net_server_service_ = net_service_manager_module_->get_inter_net_server();
    if (net_server_service_ == nullptr)
    {
        ARK_LOG_ERROR("Cannot find server net, busid = {}", bus_module_->get_self_bus_name());
        return false;
    }

    using namespace simulate;
    net_server_service_->reg_msg_callback(
        (int)simulate_msg_type::simulate_msg_req_create_scene, this, &simulate_net_module_impl::on_recv_msg);
    net_server_service_->reg_msg_callback(
        (int)simulate_msg_type::simulate_msg_req_destroy_scene, this, &simulate_net_module_impl::on_recv_msg);
    net_server_service_->reg_msg_callback(
        (int)simulate_msg_type::simulate_msg_req_create_dynamic_box, this, &simulate_net_module_impl::on_recv_msg);
    net_server_service_->reg_msg_callback(
        (int)simulate_msg_type::simulate_msg_req_create_dynamic_capsule, this, &simulate_net_module_impl::on_recv_msg);
    net_server_service_->reg_msg_callback(
        (int)simulate_msg_type::simulate_msg_req_create_dynamic_sphere, this, &simulate_net_module_impl::on_recv_msg);
    net_server_service_->reg_msg_callback(
        (int)simulate_msg_type::simulate_msg_req_create_static_box, this, &simulate_net_module_impl::on_recv_msg);
    net_server_service_->reg_msg_callback(
        (int)simulate_msg_type::simulate_msg_req_create_static_capsule, this, &simulate_net_module_impl::on_recv_msg);
    net_server_service_->reg_msg_callback(
        (int)simulate_msg_type::simulate_msg_req_create_static_sphere, this, &simulate_net_module_impl::on_recv_msg);
    net_server_service_->reg_msg_callback(
        (int)simulate_msg_type::simulate_msg_req_update_actor, this, &simulate_net_module_impl::on_recv_msg);
    net_server_service_->reg_msg_callback(
        (int)simulate_msg_type::simulate_msg_req_destroy_actor, this, &simulate_net_module_impl::on_recv_msg);
    net_server_service_->reg_msg_callback(
        (int)simulate_msg_type::simulate_msg_req_ray_cast, this, &simulate_net_module_impl::on_recv_msg);

    return true;
}

void simulate_net_module_impl::on_recv_msg(const net_msg* msg, conv_id_t session_id) {
    msgpack::object_handle obj_handle;
    msgpack::unpack(obj_handle, msg->get_msg_data(), msg->get_msg_length());
    auto unpack_obj = obj_handle.get();
    if (unpack_obj.is_nil())
    {
        ARK_LOG_ERROR("unpack failed, busid = {} msg id = {}", bus_module_->get_self_bus_name(), msg->get_msg_id());
        return;
    }

    using namespace simulate;
    bool result = false;
    auto msg_id = (simulate_msg_type)msg->get_msg_id();
    switch (msg_id)
    {
    case simulate_msg_type::simulate_msg_req_create_scene:
    {
        auto obj = unpack_obj.as<req_create_scene>();
        update_bus_id(obj.scene_id, obj.index, msg->get_src_bus());

        result = simulate_module_->create_scene(obj.scene_id, obj.index);
    }
    break;
    case simulate_msg_type::simulate_msg_req_destroy_scene:
    {
        auto obj = unpack_obj.as<req_destroy_scene>();
        update_bus_id(obj.scene_id, obj.index, msg->get_src_bus());

        result = simulate_module_->destroy_scene(obj.scene_id, obj.index);
    }
    break;
    case simulate_msg_type::simulate_msg_req_create_dynamic_box:
    {
        auto obj = unpack_obj.as<req_create_dynamic_box>();
        update_bus_id(obj.scene_id, obj.index, msg->get_src_bus());

        auto position = from_simulation_vector(obj.position);
        auto size = from_simulation_vector(obj.size);
        result = simulate_module_->create_box(obj.scene_id, obj.index, obj.obj_id, position, size, true);
    }
    break;
    case simulate_msg_type::simulate_msg_req_create_dynamic_capsule:
    {
        auto obj = unpack_obj.as<req_create_dynamic_capsule>();
        update_bus_id(obj.scene_id, obj.index, msg->get_src_bus());

        auto position = from_simulation_vector(obj.position);
        result = simulate_module_->create_capsule(
            obj.scene_id, obj.index, obj.obj_id, position, obj.raidus, obj.height, true);
    }
    break;
    case simulate_msg_type::simulate_msg_req_create_dynamic_sphere:
    {
        auto obj = unpack_obj.as<req_create_dynamic_sphere>();
        update_bus_id(obj.scene_id, obj.index, msg->get_src_bus());

        auto position = from_simulation_vector(obj.position);
        result = simulate_module_->create_sphere(obj.scene_id, obj.index, obj.obj_id, position, obj.raidus, true);
    }
    break;
    case simulate_msg_type::simulate_msg_req_create_static_box:
    {
        auto obj = unpack_obj.as<req_create_static_box>();
        update_bus_id(obj.scene_id, obj.index, msg->get_src_bus());

        auto position = from_simulation_vector(obj.position);
        auto size = from_simulation_vector(obj.size);
        result = simulate_module_->create_box(obj.scene_id, obj.index, obj.obj_id, position, size, false);
    }
    break;
    case simulate_msg_type::simulate_msg_req_create_static_capsule:
    {
        auto obj = unpack_obj.as<req_create_static_capsule>();
        update_bus_id(obj.scene_id, obj.index, msg->get_src_bus());

        auto position = from_simulation_vector(obj.position);
        result = simulate_module_->create_capsule(
            obj.scene_id, obj.index, obj.obj_id, position, obj.raidus, obj.height, false);
    }
    break;
    case simulate_msg_type::simulate_msg_req_create_static_sphere:
    {
        auto obj = unpack_obj.as<req_create_static_sphere>();
        update_bus_id(obj.scene_id, obj.index, msg->get_src_bus());

        auto position = from_simulation_vector(obj.position);
        result = simulate_module_->create_sphere(obj.scene_id, obj.index, obj.obj_id, position, obj.raidus, false);
    }
    break;
    case simulate_msg_type::simulate_msg_req_update_actor:
    {
        auto obj = unpack_obj.as<req_update_actor>();
        update_bus_id(obj.scene_id, obj.index, msg->get_src_bus());

        auto position = from_simulation_vector(obj.position);
        result = simulate_module_->update_actor(obj.scene_id, obj.index, obj.obj_id, position);
    }
    break;
    case simulate_msg_type::simulate_msg_req_destroy_actor:
    {
        auto obj = unpack_obj.as<req_destroy_actor>();
        update_bus_id(obj.scene_id, obj.index, msg->get_src_bus());

        result = simulate_module_->destroy_actor(obj.scene_id, obj.index, obj.obj_id);
    }
    break;
    case simulate_msg_type::simulate_msg_req_ray_cast:
    {
        auto obj = unpack_obj.as<req_ray_cast>();
        update_bus_id(obj.scene_id, obj.index, msg->get_src_bus());

        auto start = from_simulation_vector(obj.start);
        auto direction = from_simulation_vector(obj.direction);

        ack_ray_cast ack_ray_cast;
        result = simulate_module_->ray_cast(obj.scene_id, obj.index, start, direction, obj.distance, ack_ray_cast);
        if (result)
        {
            send_ack_ray_cast(obj.scene_id, obj.index, ack_ray_cast);
        }
    }
    break;
    default:
    {
        ARK_LOG_ERROR("unknown msg id = {}", msg->get_msg_id());
    }
    break;
    }

    if (!result)
    {
        ARK_LOG_ERROR(
            "deal message failed, busid = {} msg id = {}", bus_module_->get_self_bus_name(), msg->get_msg_id());
        return;
    }
}

physic3_d simulate_net_module_impl::from_simulation_vector(const simulate::simulate_vec3& vec3) {
    return physic3_d(vec3.x, vec3.y, vec3.z);
}

void simulate_net_module_impl::trigger_call_back(
    const std::string& scene_id, const int32_t index, const simulate::trigger_pair& pair) {
    simulate::ack_trigger trigger;
    trigger.scene_id = scene_id;
    trigger.index = index;
    trigger.pairs.push_back(pair);

    send_ack_trigger(scene_id, index, trigger);
}

void simulate_net_module_impl::send_msg(const std::string& scene_id, const int32_t index,
    const simulate::simulate_msg_type msg_id, const msgpack::sbuffer& buffer) {
    int32_t target_bus_id;
    if (!get_bus_id(scene_id, index, target_bus_id))
    {
        ARK_LOG_ERROR("get bus id failed scene id = {} index = {} msg id = {}", scene_id, index, (int)msg_id);
        return;
    }

    auto bus_id = bus_module_->get_self_bus_id();
    auto net_service_ptr = net_service_manager_module_->get_inter_net_server();
    if (!net_service_ptr)
    {
        ARK_LOG_ERROR("get net service failed scene id = {} index = {} msg id = {}", scene_id, index, (int)msg_id);
        return;
    }

    auto net_ptr = net_service_ptr->get_net();
    if (!net_ptr)
    {
        ARK_LOG_ERROR("get net failed scene id = {} index = {} msg id = {}", scene_id, index, (int)msg_id);
        return;
    }

    auto session_id = net_service_manager_module_->get_session_id(target_bus_id);
    if (session_id == 0)
    {
        ARK_LOG_ERROR("get session id failed scene id = {} index = {} msg id = {}", scene_id, index, (int)msg_id);
        return;
    }

    ss_msg_header head;
    head.set_message_id((msg_id_t)msg_id);
    head.set_message_length(buffer.size());
    head.set_source_bus_id(bus_id);
    head.set_dest_bus_id(target_bus_id);

    net_ptr->send_msg(&head, buffer.data(), session_id);
}

void simulate_net_module_impl::send_ack_ray_cast(
    const std::string& scene_id, const int32_t index, const simulate::ack_ray_cast& ack_msg) {
    msgpack::sbuffer buffer;
    msgpack::pack(buffer, ack_msg);

    send_msg(scene_id, index, simulate::simulate_msg_type::simulate_msg_ack_ray_cast, buffer);
}

void simulate_net_module_impl::send_ack_trigger(
    const std::string& scene_id, const int32_t index, const simulate::ack_trigger& ack_msg) {
    msgpack::sbuffer buffer;
    msgpack::pack(buffer, ack_msg);

    send_msg(scene_id, index, simulate::simulate_msg_type::simulate_msg_ack_trigger, buffer);
}

void simulate_net_module_impl::update_bus_id(const std::string& scene_id, const int32_t index, const int32_t bus_id) {
    auto iter = scene_index_bus_list_.find(scene_id);
    if (iter == scene_index_bus_list_.end())
    {
        index_bus_map index_bus;
        iter = scene_index_bus_list_.insert(std::make_pair(scene_id, index_bus)).first;
        if (iter == scene_index_bus_list_.end())
        {
            return;
        }
    }

    auto& index_bus_map = iter->second;
    auto iter_index = index_bus_map.find(index);
    if (iter_index == index_bus_map.end())
    {
        index_bus_map.insert(std::make_pair(index, bus_id));
    }
    else
    {
        iter_index->second = bus_id;
    }
}

bool simulate_net_module_impl::get_bus_id(const std::string& scene_id, const int32_t index, int32_t& bus_id) {
    auto iter = scene_index_bus_list_.find(scene_id);
    if (iter == scene_index_bus_list_.end())
    {
        return false;
    }

    auto& index_bus_map = iter->second;
    auto iter_index = index_bus_map.find(index);
    if (iter_index == index_bus_map.end())
    {
        return false;
    }

    bus_id = iter_index->second;
    return true;
}

} // namespace ark
