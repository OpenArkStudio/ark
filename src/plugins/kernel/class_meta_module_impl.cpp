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

#include "ark/kernel/class_meta_module_impl.hpp"
#include "ark/plugin_manager.hpp"
#include "ark/xml.hpp"
#include "detail/entity_json_runtime.hpp"
#include "detail/resource_path.hpp"

namespace ark {

bool class_meta_module_impl::init() {
    class_meta_manager_ = ARK_NEW class_meta_manager;

    try
    {
        if (!load())
        {
            ARK_DELETE(class_meta_manager_);
            return false;
        }
    }
    catch (...)
    {
        ARK_DELETE(class_meta_manager_);
        throw;
    }

    return true;
}

bool class_meta_module_impl::shut() {
    ARK_DELETE(class_meta_manager_);

    return true;
}

bool class_meta_module_impl::load() {
    if (detail::has_entity_json())
    {
        const auto document = entity_json::load(detail::entity_json_file());
        detail::load_entity_json_metadata(*class_meta_manager_, document);
        return true;
    }
    if (!load_config())
    {
        return false;
    }

    if (!load_entity())
    {
        return false;
    }

    return true;
}

bool class_meta_module_impl::load_config() {
    // TODO: will put the configuration fileds in kernel.plugin.conf
    const auto file_path = detail::resource_file(config_class_file_path);

    ARK_LOG_INFO("Load config class files: {}", file_path);

    xml xml_doc(file_path);
    auto root_node = xml_doc.get_root_node();
    ARK_ASSERT_RET_VAL(root_node.is_valid(), false);

    for (auto meta_node = root_node.find_node("config"); meta_node.is_valid(); meta_node.next_node())
    {
        std::string meta_name = meta_node.get_string("id");
        std::string schema_path = meta_node.get_string("meta");
        std::string res_path = meta_node.get_string("res");

        auto class_meta_ptr = class_meta_manager_->create_static_meta(meta_name);
        ARK_ASSERT_RET_VAL(class_meta_ptr != nullptr, false);

        class_meta_ptr->set_res_path(res_path);
        ARK_ASSERT_RET_VAL(load_config_meta(detail::resource_file(schema_path), class_meta_ptr), false);
    }

    return true;
}

bool class_meta_module_impl::load_config_meta(
    const std::string& schema_path, std::shared_ptr<class_meta> class_meta_ptr) {
    ARK_LOG_INFO("Load meta files: {}", schema_path);

    xml xml_doc(schema_path);
    auto root_node = xml_doc.get_root_node();
    ARK_ASSERT_RET_VAL(root_node.is_valid(), false);

    const std::string& class_name = class_meta_ptr->get_name();
    for (auto meta_node = root_node.find_node("meta"); meta_node.is_valid(); meta_node.next_node())
    {
        std::string name = meta_node.get_string("field");
        std::string type_name = meta_node.get_string("type");
        uint32_t index = meta_name_index::get_index(class_name, name);

        data_type data_type = convert_data_type(type_name);
        ARK_ASSERT_RET_VAL(data_type != data_type::dt_empty, false);

        auto data_meta_ptr = class_meta_ptr->create_data_meta(name, index);
        ARK_ASSERT_RET_VAL(data_meta_ptr != nullptr, false);

        data_meta_ptr->set_type(data_type);
    }

    return true;
}

bool class_meta_module_impl::load_entity() {
    // TODO: will put the configuration fileds in kernel.plugin.conf
    const auto file_path = detail::resource_file(entity_class_file_path);

    ARK_LOG_INFO("Load entity class files: {}", file_path);

    xml xml_doc(file_path);
    auto root_node = xml_doc.get_root_node();
    ARK_ASSERT_RET_VAL(root_node.is_valid(), false);

    for (auto meta_node = root_node.find_node("config"); meta_node.is_valid(); meta_node.next_node())
    {
        std::string schema_path = meta_node.get_string("meta");
        ARK_ASSERT_RET_VAL(load_entity_meta(detail::resource_file(schema_path)), false);
    }

    // exact table meta and object meta of a class meta after all loaded
    ARK_ASSERT_RET_VAL(class_meta_manager_->after_loaded(), false);

    return true;
}

bool class_meta_module_impl::load_entity_meta(const std::string& schema_path) {
    ARK_LOG_INFO("Load entity class files: {}", schema_path);

    xml xml_doc(schema_path);
    auto root_node = xml_doc.get_root_node();
    ARK_ASSERT_RET_VAL(root_node.is_valid(), false);

    for (auto meta_node = root_node.find_node("meta"); meta_node.is_valid(); meta_node.next_node())
    {
        std::string class_name = meta_node.get_string("class");
        std::string data_name = meta_node.get_string("field_name");
        std::string type_name = meta_node.get_string("type");
        std::string type_class = meta_node.get_string("type_class");
        uint32_t index = meta_name_index::get_index(class_name, data_name);

        // data mask
        uint32_t feature_sync_view = meta_node.get_uint32("sync_view");
        uint32_t feature_sync_self = meta_node.get_uint32("sync_self");
        uint32_t feature_save = meta_node.get_uint32("save");
        uint32_t feature_real_time = meta_node.get_uint32("real_time");

        // class meta
        auto class_meta_ptr = class_meta_manager_->create_meta(class_name);
        ARK_ASSERT_RET_VAL(class_meta_ptr != nullptr, false);

        if (type_name == "class")
        {
            continue;
        }

        // data mask
        mask_type mask;
        mask[(size_t)data_mask::pf_sync_view] = (feature_sync_view > 0 ? 1 : 0);
        mask[(size_t)data_mask::pf_sync_self] = (feature_sync_self > 0 ? 1 : 0);
        mask[(size_t)data_mask::pf_save] = (feature_save > 0 ? 1 : 0);
        mask[(size_t)data_mask::pf_real_time] = (feature_real_time > 0 ? 1 : 0);

        if (type_name == "container")
        {
            auto container_meta_ptr = class_meta_ptr->create_container_meta(data_name, index, type_class, mask);
            ARK_ASSERT_RET_VAL(container_meta_ptr != nullptr, false);
            continue;
        }

        // convert type
        data_type data_type = convert_data_type(type_name);
        ARK_ASSERT_RET_VAL(data_type != data_type::dt_empty, false);

        if (data_type::dt_table == data_type)
        {
            // create table meta
            auto table_meta_ptr = class_meta_ptr->create_table_meta(data_name, index);
            ARK_ASSERT_RET_VAL(table_meta_ptr != nullptr, false);

            table_meta_ptr->set_type_name(type_class);
            table_meta_ptr->set_mask(mask);
        }
        else
        {
            // default create data meta
            auto data_meta_ptr = class_meta_ptr->create_data_meta(data_name, index);
            ARK_ASSERT_RET_VAL(data_meta_ptr != nullptr, false);

            data_meta_ptr->set_mask(mask);
            data_meta_ptr->set_type(data_type);
        }
    }

    return true;
}

data_type class_meta_module_impl::convert_data_type(const std::string& type_name) {
    data_type data_type = data_type::dt_empty;
    if (type_name == "bool")
    {
        data_type = data_type::dt_boolean;
    }
    else if (type_name == "int")
    {
        data_type = data_type::dt_int32;
    }
    else if (type_name == "int32")
    {
        data_type = data_type::dt_int32;
    }
    else if (type_name == "int64")
    {
        data_type = data_type::dt_int64;
    }
    else if (type_name == "uint32")
    {
        data_type = data_type::dt_uint32;
    }
    else if (type_name == "uint64")
    {
        data_type = data_type::dt_uint64;
    }
    else if (type_name == "float")
    {
        data_type = data_type::dt_float;
    }
    else if (type_name == "double")
    {
        data_type = data_type::dt_double;
    }
    else if (type_name == "string")
    {
        data_type = data_type::dt_string;
    }
    else if (type_name == "table")
    {
        data_type = data_type::dt_table;
    }
    else if (type_name == "object")
    {
        data_type = data_type::dt_guid;
    }

    return data_type;
}

bool class_meta_module_impl::add_class_call_back(
    const std::string& class_name, class_event_functor&& cb, const int32_t prio) {
    auto class_meta_ptr = class_meta_manager_->find_meta(class_name);
    ARK_ASSERT_RET_VAL(class_meta_ptr != nullptr, false);

    auto callback_ptr = class_meta_ptr->get_class_call_back_manager();
    ARK_ASSERT_RET_VAL(callback_ptr != nullptr, false);

    return callback_ptr->add_class_call_back(std::move(cb), prio);
}

bool class_meta_module_impl::do_class_event(
    const guid_t& id, const std::string& class_name, const entity_event class_event, const data_list& args) {
    auto class_meta_ptr = class_meta_manager_->find_meta(class_name);
    ARK_ASSERT_RET_VAL(class_meta_ptr != nullptr, false);

    auto callback_ptr = class_meta_ptr->get_class_call_back_manager();
    ARK_ASSERT_RET_VAL(callback_ptr != nullptr, false);

    return callback_ptr->on_class_event(id, class_name, class_event, args);
}

std::shared_ptr<class_meta> class_meta_module_impl::find_meta(const std::string& class_name) const {
    return class_meta_manager_->find_meta(class_name);
}

const class_meta_manager::class_meta_list& class_meta_module_impl::get_meta_list() const {
    return class_meta_manager_->get_meta_list();
}

std::shared_ptr<class_meta> class_meta_module_impl::find_static_meta(const std::string& class_name) const {
    return class_meta_manager_->find_static_meta(class_name);
}

const class_meta_manager::class_meta_list& class_meta_module_impl::get_static_meta_list() const {
    return class_meta_manager_->get_static_meta_list();
}

} // namespace ark
