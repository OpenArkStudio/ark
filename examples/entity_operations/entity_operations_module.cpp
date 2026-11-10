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

#include "ark/kernel/class_meta.hpp"
#include "ark/kernel/data.hpp"
#include "ark/kernel/data_list_impl.hpp"
#include "ark/meta_define.hpp"
#include "entity_operations_module.hpp"
#include "entity_operations_plugin.hpp"

static const int map_id = 1;
static const size_t item_config_id = 100001;
static const std::string record_name = ark::entity_meta_player::items();
static const uint32_t bag_index = ark::entity_meta_player::bag_index();
static const uint32_t col_count = ark::entity_meta_item_bag::count_index();
static const uint32_t col_guid = ark::entity_meta_item_bag::guid_index();

namespace ark {

std::unique_ptr<entity_operations_module> make_entity_operations_module() {
    return std::make_unique<entity_operations_module>();
}

bool entity_operations_module::init() {
    std::cout << GET_CLASS_NAME(entity_operations_module) << ", Init" << std::endl;
    return true;
}

bool entity_operations_module::post_init() {
    std::cout << GET_CLASS_NAME(entity_operations_module) << ", PostInit" << std::endl;

    kernel_module_ = find_module<kernel_module>();
    map_module_ = find_module<map_module>();

    ARK_LOG_INFO("Init finished...");

    // Create scene, all entity need in scene
    map_module_->create_map(map_id);

    //DestroyTest();
    //CalssCallBackTest();
    //DataCallBackTest();
    //TableCallBackTest();
    //EventTest();
    container_call_back_test();

    create_entity_test();
    data_test();
    table_test();

    // m_pKernelModule->DestroyAll();

    return true;
}

bool entity_operations_module::pre_shut() {
    std::cout << GET_CLASS_NAME(entity_operations_module) << ", PreShut" << std::endl;
    kernel_module_->destroy_all();

    return true;
}

bool entity_operations_module::shut() {
    std::cout << GET_CLASS_NAME(entity_operations_module) << ", Shut" << std::endl;
    return true;
}

// create object test
void entity_operations_module::create_entity_test() {
    std::cout << GET_CLASS_NAME(entity_operations_module) << ", CreateEntityTest" << std::endl;

    // create player
    auto entity_ptr = create_an_player_and_init();
    ARK_ASSERT_RET_NONE(entity_ptr != nullptr);

    std::cout << "Player Name = " << entity_ptr->get_string(entity_meta_player::name()).c_str() << std::endl;

    const guid_t& id = entity_ptr->get_id();

    // create an item for player
    //auto pItem = m_pKernelModule->CreateContainerEntity(id, BAG, AFConfigMetaItem::self_name(), ITEM_CONFIG_ID);
    //ARK_ASSERT_RET_NONE(pItem != nullptr);

    //pItem->SetInt32(AFConfigMetaItem::level(), 22);
    //std::cout << "Item Config = " << pItem->GetConfigID() << std::endl;
}

void entity_operations_module::data_test() {
    std::cout << GET_CLASS_NAME(entity_operations_module) << ", DataTest" << std::endl;

    // create player
    auto entity_ptr = create_an_player_and_init();
    ARK_ASSERT_RET_NONE(entity_ptr != nullptr);

    const guid_t& id = entity_ptr->get_id();
    ARK_ASSERT_RET_NONE(entity_ptr->get_string(entity_meta_player::name_index()) == "Xiaoming");
    ARK_ASSERT_RET_NONE(entity_ptr->get_string(entity_meta_player::name()) == "Xiaoming");
    ARK_ASSERT_RET_NONE(entity_ptr->get_int32(entity_meta_player::gender_index()) == 1);
    ARK_ASSERT_RET_NONE(entity_ptr->get_int32(entity_meta_player::gender()) == 1);
    entity_ptr->set_int32(entity_meta_player::gender_index(), 22);
    entity_ptr->set_int32(entity_meta_player::gender(), 22);
    kernel_module_->get_entity(id)->set_int32(entity_meta_player::gender_index(), 11);
    kernel_module_->get_entity(id)->set_int32(entity_meta_player::gender(), 11);
    custom_data_test(id, 1, 1);
    std::cout << "Entity data demonstration: name=Xiaoming gender=11 custom=111" << std::endl;
}

void entity_operations_module::table_test() {
    std::cout << GET_CLASS_NAME(entity_operations_module) << ", TableTest" << std::endl;

    // create player
    auto entity_ptr = create_an_player_and_init();
    ARK_ASSERT_RET_NONE(entity_ptr != nullptr);

    table* table_ptr = entity_ptr->find_table(record_name);
    ARK_ASSERT_RET_NONE(table_ptr != nullptr);

    auto row_data_ptr = table_ptr->add_row(0);
    ARK_ASSERT_RET_NONE(row_data_ptr != nullptr);

    row_data_ptr->set_int32(col_count, 22);
}

void entity_operations_module::destroy_test() {
    ARK_LOG_DEBUG("DestroyTest Start...");

    size_t count = 1;
    for (size_t i = 0; i < count; i++) {
        auto entity_ptr = create_an_player_and_init();
        kernel_module_->destroy_entity(entity_ptr->get_id());
    }

    ARK_LOG_DEBUG("DestroyTest End...");
}

void entity_operations_module::calss_call_back_test() {
    std::cout << GET_CLASS_NAME(entity_operations_module) << ", TableTest" << std::endl;

    kernel_module_->add_class_call_back(
        entity_meta_player::self_name(), this, &entity_operations_module::on_class_call_back_event);

    auto entity_ptr = create_an_player_and_init();
    ARK_ASSERT_RET_NONE(entity_ptr != nullptr);

    kernel_module_->destroy_entity(entity_ptr->get_id());
}

void entity_operations_module::data_call_back_test() {
    std::cout << GET_CLASS_NAME(entity_operations_module) << ", TableTest" << std::endl;

    kernel_module_->add_node_call_back(entity_meta_player::self_name(), entity_meta_player::gender(), this,
        &entity_operations_module::on_data_call_back_event);

    auto entity_ptr = create_an_player_and_init();
    ARK_ASSERT_RET_NONE(entity_ptr != nullptr);

    entity_ptr->set_int32(entity_meta_player::gender(), 1);
    entity_ptr->set_int32(entity_meta_player::gender(), 2);
}

void entity_operations_module::table_call_back_test() {
    std::cout << GET_CLASS_NAME(entity_operations_module) << ", TableTest" << std::endl;

    kernel_module_->add_table_call_back(entity_meta_player::self_name(), entity_meta_player::items(), this,
        &entity_operations_module::on_table_call_back_event);

    auto entity_ptr = create_an_player_and_init();
    ARK_ASSERT_RET_NONE(entity_ptr != nullptr);

    table* table_ptr = entity_ptr->find_table(record_name);
    ARK_ASSERT_RET_NONE(table_ptr != nullptr);

    auto row_data_ptr = table_ptr->add_row(0);
    ARK_ASSERT_RET_NONE(row_data_ptr != nullptr);

    row_data_ptr->set_int32(col_count, 22);

    row_data_ptr->set_int32(col_count, 33);
}

void entity_operations_module::container_call_back_test() {
    std::cout << GET_CLASS_NAME(entity_operations_module) << ", ContainerTest" << std::endl;

    kernel_module_->add_container_call_back(
        entity_meta_player::self_name(), bag_index, this, &entity_operations_module::on_container_call_back_event);

    create_entity_test();
}

void entity_operations_module::event_test() {
    std::cout << GET_CLASS_NAME(entity_operations_module) << ", TableTest" << std::endl;

    auto entity_ptr = create_an_player_and_init();
    ARK_ASSERT_RET_NONE(entity_ptr != nullptr);
}

//-----------------------------inner interface--------------------------------------------
std::shared_ptr<ark::entity> entity_operations_module::create_an_player_and_init() {
    // create object
    auto entity_ptr = kernel_module_->create_entity(
        null_guid, map_id, 0, entity_meta_player::self_name(), null_int, data_list_impl());
    ARK_ASSERT_RET_VAL(entity_ptr != nullptr, nullptr);

    // init data
    entity_ptr->set_string(entity_meta_player::name(), "Xiaoming");
    entity_ptr->set_int32(entity_meta_player::gender(), 1);
    entity_ptr->set_int32(entity_meta_player::career(), 1);
    entity_ptr->set_int32(entity_meta_player::camp(), 1);
    entity_ptr->set_int32(entity_meta_player::last_map_id(), 1);
    entity_ptr->set_int32(entity_meta_player::level(), 1);

    table* table_ptr = entity_ptr->find_table(record_name);
    ARK_ASSERT_RET_VAL(table_ptr != nullptr, nullptr);

    auto row_ptr = table_ptr->add_row(0);
    ARK_ASSERT_RET_VAL(row_ptr != nullptr, nullptr);
    row_ptr->set_int64(col_guid, 1111);

    if (!entity_ptr->find_custom_data("test")) {
        entity_ptr->add_custom_int32("test", 111);
    }

    std::cout << "CreatePlayer name " << entity_ptr->get_string(entity_meta_player::name()).c_str() << " "
              << entity_meta_player::gender() << entity_ptr->get_int32(entity_meta_player::gender()) << std::endl;

    return entity_ptr;
}

void entity_operations_module::custom_data_test(const guid_t& guid, size_t count, const uint32_t index) {
    std::cout << "----------[index] time [custom data] test start----------" << std::endl;

    auto entity_ptr = kernel_module_->get_entity(guid);
    if (nullptr == entity_ptr) {
        return;
    }

    if (!entity_ptr->find_custom_data("test") && !entity_ptr->add_custom_int32("test", 0)) {
        return;
    }

    std::cout << "CustomDataTest find test = " << (entity_ptr->find_custom_data("test") ? "true" : "false")
              << " value = " << entity_ptr->get_custom_int32("test") << std::endl;

    std::cout << "----------[index] time [custom data] test end----------" << std::endl;
}

int entity_operations_module::on_class_call_back_event(
    const guid_t& self, const std::string& class_name, const entity_event event, const data_list& arg) {
    std::cout << "---------- OnClassCallBackEvent start----------" << std::endl;

    std::cout << "ClassName: " << class_name << " ID: " << self << " Event: " << (int)event << std::endl;

    if (event == entity_event::entity_evt_data_finished) {
        //
    }

    std::cout << "---------- OnClassCallBackEvent end----------" << std::endl;

    return 0;
}

int entity_operations_module::on_data_call_back_event(const guid_t& self, const std::string& data_name,
    const uint32_t index, const ark::data& old_value, const ark::data& new_value) {
    std::cout << "---------- OnDataCallBackEvent start----------" << std::endl;

    std::cout << "data name: " << data_name << " old value : " << old_value.to_string()
              << " new value : " << new_value.to_string() << std::endl;

    std::cout << "---------- OnDataCallBackEvent end----------" << std::endl;

    return 0;
}

int entity_operations_module::on_table_call_back_event(
    const guid_t& self, const table_event_data& data, const ark::data& old_value, const ark::data& new_value) {
    std::cout << "---------- OnTableCallBackEvent start----------" << std::endl;

    if (data.op_type == (size_t)table_op_type::table_add) {
        std::cout << "table name: " << data.table_name << " row : " << data.row << std::endl;
    } else {
        std::cout << "table name: " << data.table_name << " row : " << data.row << " data index : " << data.data_index
                  << " old value : " << old_value.to_string() << " new value : " << new_value.to_string() << std::endl;
    }

    std::cout << "---------- OnTableCallBackEvent end----------" << std::endl;

    return 0;
}

int entity_operations_module::on_container_call_back_event(const guid_t& self, const uint32_t index,
    const container_op_type op_type, const uint32_t src_index, const uint32_t dest_index) {
    std::cout << "---------- OnContainerCallBackEvent start----------" << std::endl;

    std::cout << "container index: " << index << " op : " << (size_t)op_type << " src index : " << src_index
              << " dest index : " << dest_index << std::endl;

    std::cout << "---------- OnContainerCallBackEvent end----------" << std::endl;

    return 0;
}

} // namespace ark
