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

#include "ark/bus/bus_module.hpp"
#include "ark/module_base.hpp"
#include "ark/net/net_service_manager_module.hpp"
#include "simulate_module.hpp"
#include "simulate_msg_define.hpp"
#include "simulate_net_module.hpp"

namespace ark {

class simulate_net_module_impl final : public module_base<simulate_net_module>
{
public:
    bool init() override;
    bool post_init() override;

private:
    bool start_server();
    void on_recv_msg(const net_msg* msg, conv_id_t session_id);

    physic3_d from_simulation_vector(const simulate::simulate_vec3& vec3);

    void trigger_call_back(const std::string& scene_id, const int32_t index, const simulate::trigger_pair& pair);

    void send_msg(const std::string& scene_id, const int32_t index, const simulate::simulate_msg_type msg_id,
        const msgpack::sbuffer& buffer);
    void send_ack_ray_cast(
        const std::string& scene_id, const int32_t index, const simulate::ack_ray_cast& ack_ray_cast);
    void send_ack_trigger(const std::string& scene_id, const int32_t index, const simulate::ack_trigger& ack_ray_cast);

    void update_bus_id(const std::string& scene_id, const int32_t index, const int32_t bus_id);
    bool get_bus_id(const std::string& scene_id, const int32_t index, int32_t& bus_id);

private:
    simulate_module* simulate_module_{nullptr};

    using index_bus_map = std::map<int32_t, int32_t>;
    using scene_index_bus_map = std::unordered_map<std::string, index_bus_map>;
    scene_index_bus_map scene_index_bus_list_;

    bus_module* bus_module_{nullptr};
    net_service_manager_module* net_service_manager_module_{nullptr};
    std::shared_ptr<net_server_service> net_server_service_;
};

} // namespace ark
