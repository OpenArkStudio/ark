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

#include "ark/kernel/kernel_module.hpp"
#include "ark/kernel/map_module.hpp"
#include "ark/module_base.hpp"

namespace ark {

class entity_operations_module final : public module_base<module> {
public:
    explicit entity_operations_module() = default;

    bool init() override;
    bool post_init() override;
    bool pre_shut() override;
    bool shut() override;

protected:
    // create test
    void create_entity_test();

    // query test
    void data_test();
    void table_test();
    void destroy_test();

    // call back test
    void calss_call_back_test();
    void data_call_back_test();
    void table_call_back_test();
    void container_call_back_test();

    // event test
    void event_test();

    //------------inner interface----------------
    std::shared_ptr<entity> create_an_player_and_init();
    void custom_data_test(const guid_t& guid, size_t count, const uint32_t index);

    //------------callback----------------------
    int on_class_call_back_event(
        const guid_t& self, const std::string& class_name, const entity_event event, const data_list& arg);

    int on_data_call_back_event(const guid_t& self, const std::string& data_name, const uint32_t index,
        const ark::data& old_value, const ark::data& new_value);

    int on_table_call_back_event(
        const guid_t& self, const table_event_data& data, const ark::data& old_value, const ark::data& new_value);

    int on_container_call_back_event(const guid_t& self, const uint32_t index, const container_op_type op_type,
        const uint32_t src_index, const uint32_t dest_index);

protected:
    kernel_module* kernel_module_;
    map_module* map_module_;
};

} // namespace ark
