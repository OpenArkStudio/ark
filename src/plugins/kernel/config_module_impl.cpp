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

#include <memory>

#include "ark/kernel/config_module_impl.hpp"
#include "ark/kernel/node_impl.hpp"
#include "ark/kernel/node_manager.hpp"
#include "ark/kernel/static_entity.hpp"
#include "ark/kernel/static_entity_impl.hpp"
#include "ark/macros.hpp"
#include "ark/module.hpp"
#include "ark/plugin_manager.hpp"
#include "ark/xml.hpp"
#include "detail/entity_json_runtime.hpp"
#include "detail/resource_path.hpp"

namespace ark {

bool config_module_impl::init() {
    class_meta_module_ = find_module<class_meta_module>();

    static_entity_manager_ = ARK_NEW static_entity_manager();

    return true;
}

bool config_module_impl::post_init() {
    // Static data requires the schema loaded during the metadata module's init phase.
    return load();
}

bool config_module_impl::shut() {
    return clear();
}

bool config_module_impl::load() {
    if (loaded_)
    {
        return false;
    }

    auto& class_meta_list = class_meta_module_->get_static_meta_list();
    for (auto iter : class_meta_list)
    {
        auto class_meta = iter.second;
        ARK_ASSERT_RET_VAL(load_config(class_meta), false);
    }

    loaded_ = true;

    return true;
}

bool config_module_impl::save() {
    return false;
}

bool config_module_impl::clear() {
    ARK_DELETE(static_entity_manager_);
    return true;
}

bool config_module_impl::load_config(std::shared_ptr<class_meta> class_meta_ptr) {
    ARK_ASSERT_RET_VAL(class_meta_ptr != nullptr, true);

    if (const auto& config = class_meta_ptr->get_json_config())
    {
        detail::load_entity_json_config(*static_entity_manager_, class_meta_ptr, *config);
        return true;
    }

    const std::string& res_path = class_meta_ptr->get_res_path();
    if (res_path.empty())
    {
        return true;
    }

    // TODO: put the config filed in kernel.plgin.conf
    const auto file_path = detail::resource_file(res_path);

    ARK_LOG_INFO("Load data config files: {}", file_path);

    xml xml_doc(file_path);
    auto root_node = xml_doc.get_root_node();
    ARK_ASSERT_RET_VAL(root_node.is_valid(), false);

    auto& data_meta_list = class_meta_ptr->get_data_meta_list();
    for (auto data_node = root_node.find_node("data"); data_node.is_valid(); data_node.next_node())
    {
        // must have id
        auto idx = class_meta_ptr->get_index("id");
        auto id_meta_ptr = data_meta_list.find_value(idx);
        ARK_ASSERT_RET_VAL(id_meta_ptr != nullptr, false);

        const auto delete_id = [](node* value) { ARK_DELETE(value); };
        std::unique_ptr<node, decltype(delete_id)> id_data_ptr(create_data_by_meta(id_meta_ptr), delete_id);
        ARK_ASSERT_RET_VAL(id_data_ptr != nullptr, false);

        id_data_ptr->from_string(data_node.get_string(id_data_ptr->get_name().c_str()));

        // create a new object
        auto node_manager_ptr = std::make_shared<node_manager>(class_meta_ptr);
        ARK_ASSERT_RET_VAL(node_manager_ptr != nullptr, false);

        auto obj_ptr =
            static_entity_manager_->create_static_entity(class_meta_ptr, node_manager_ptr, id_data_ptr->get_value());
        ARK_ASSERT_RET_VAL(obj_ptr != nullptr, false);

        // The temporary ID is no longer needed after the record is created.
        id_data_ptr.reset();

        // read by class meta
        for (auto iter : data_meta_list)
        {
            auto& data_name = iter.first;
            auto data_meta_ptr = iter.second;
            ARK_ASSERT_RET_VAL(data_meta_ptr != nullptr, false);

            node* data_ptr = node_manager_ptr->create_data(data_meta_ptr);
            ARK_ASSERT_RET_VAL(data_ptr != nullptr, false);

            data_ptr->from_string(data_node.get_string(data_meta_ptr->get_name().c_str()));
        }
    }

    return true;
}

std::shared_ptr<static_entity> config_module_impl::find_static_entity(const id_type config_id) {
    ARK_ASSERT_RET_VAL(static_entity_manager_ != nullptr, nullptr);

    return static_entity_manager_->find_object(config_id);
}

} // namespace ark
