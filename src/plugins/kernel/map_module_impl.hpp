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

#include "ark/kernel/entity.hpp"
#include "ark/kernel/kernel_module.hpp"
#include "ark/kernel/map_module.hpp"
#include "ark/module_base.hpp"

namespace ark {

class map_module_impl final : public module_base<map_module> {
public:
    bool init() override;
    bool shut() override;

    std::shared_ptr<map_info> get_map_info(const int map_id) override;

    bool is_in_map_instance(const guid_t& self) override;
    bool exist_map(const int map_id) override;

    bool switch_map(const guid_t& self, const int target_map, const int target_inst, const vector3d& pos,
        const float orient, const data_list& args) override;

    bool create_map(const int map_id) override;
    bool destroy_map(const int map_id) override;

    int get_online_count() override;
    int get_max_online_count() override;
    int get_map_online_count(const int map_id) override;
    int get_map_online_count(const int map_id, const int inst_id) override;
    int get_map_online_list(const int map_id, data_list& list) override;

    int create_map_instance(const int map_id) override;
    bool release_map_instance(const int map_id, const int inst_id) override;
    bool exit_map_instance(const int map_id, const int inst_id) override;

    bool get_inst_entity_list(const int map_id, const int inst_id, data_list& list) override;
    int get_entity_by_data_node(
        const int map_id, const std::string& name, const data_list& value_args, data_list& list) override;

private:
    kernel_module* kernel_module_;

    shared_object_map<int, map_info> map_infos_;
};

} // namespace ark
