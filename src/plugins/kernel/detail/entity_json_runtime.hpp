// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <limits>

#include "ark/kernel/class_meta_manager.hpp"
#include "ark/kernel/class_meta_module.hpp"
#include "ark/kernel/entity_json.hpp"
#include "ark/kernel/node_impl.hpp"
#include "ark/kernel/static_entity_manager.hpp"
#include "resource_path.hpp"

namespace ark::detail {

inline std::string entity_json_file() {
    return resource_file("meta/entity_schema.json");
}

inline bool has_entity_json() {
    std::error_code error;
    const auto exists = std::filesystem::exists(entity_json_file(), error);
    if (error)
    {
        throw entity_json::error(entity_json_file(), "/", "could not inspect selected JSON: " + error.message());
    }
    return exists;
}

inline data_type entity_json_type(entity_json::value_type type) {
    using type_t = entity_json::value_type;
    switch (type)
    {
    case type_t::boolean:
        return data_type::dt_boolean;
    case type_t::int32:
        return data_type::dt_int32;
    case type_t::uint32:
        return data_type::dt_uint32;
    case type_t::int64:
        return data_type::dt_int64;
    case type_t::uint64:
        return data_type::dt_uint64;
    case type_t::float32:
        return data_type::dt_float;
    case type_t::float64:
        return data_type::dt_double;
    case type_t::string:
        return data_type::dt_string;
    case type_t::guid:
        return data_type::dt_guid;
    }
    throw entity_json::error(entity_json_file(), "/", "unsupported runtime scalar type");
}

inline void entity_json_fields(const entity_json::class_definition& schema, const std::shared_ptr<class_meta>& meta) {
    for (const auto& field : schema.fields)
    {
        const auto member = meta->create_data_meta(field.name, field.index);
        if (!member)
        {
            throw entity_json::error(entity_json_file(), "/classes/" + schema.name, "could not create field metadata");
        }
        member->set_type(entity_json_type(field.type));
        member->set_mask(field.mask);
    }
}

inline void load_entity_json_metadata(class_meta_manager& manager, const entity_json::document& document) {
    for (const auto& schema : document.classes)
    {
        const auto meta = manager.create_meta(schema.name);
        entity_json_fields(schema, meta);
        for (const auto& table : schema.tables)
        {
            const auto member = meta->create_table_meta(table.name, table.index);
            member->set_type_name(table.class_name);
            member->set_mask(table.mask);
        }
        for (const auto& container : schema.containers)
        {
            meta->create_container_meta(container.name, container.index, container.class_name, container.mask);
        }
    }
    for (std::size_t i = 0; const auto& config : document.configs)
    {
        const auto schema = std::find_if(document.classes.begin(), document.classes.end(),
            [&](const auto& schema) { return schema.name == config.class_name; });
        const auto meta = manager.create_static_meta(config.class_name);
        entity_json_fields(*schema, meta);
        meta->set_json_config(
            std::make_shared<const entity_json::config_definition>(config), "/configs/" + std::to_string(i++));
    }
    if (!manager.after_loaded())
    {
        throw entity_json::error(entity_json_file(), "/classes", "could not resolve table metadata");
    }
}

inline void assign_entity_json(node& node, const entity_json::value& value, const std::string& pointer) {
    switch (node.get_type())
    {
    case data_type::dt_boolean:
        node.set_bool(std::get<bool>(value));
        break;
    case data_type::dt_int32:
        node.set_int32(static_cast<std::int32_t>(std::get<std::int64_t>(value)));
        break;
    case data_type::dt_uint32:
        node.set_u_int32(static_cast<std::uint32_t>(std::get<std::uint64_t>(value)));
        break;
    case data_type::dt_int64:
        node.set_int64(std::get<std::int64_t>(value));
        break;
    case data_type::dt_uint64:
        node.set_u_int64(std::get<std::uint64_t>(value));
        break;
    case data_type::dt_float:
        node.set_float(static_cast<float>(std::get<double>(value)));
        break;
    case data_type::dt_double:
        node.set_double(std::get<double>(value));
        break;
    case data_type::dt_string:
        node.set_string(std::get<std::string>(value));
        break;
    case data_type::dt_guid:
    {
        const auto& text = std::get<std::string>(value);
        guid_t guid{};
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), guid);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        {
            throw entity_json::error(entity_json_file(), pointer, "GUID exceeds the configured runtime width");
        }
        node.set_object(guid);
        break;
    }
    default:
        throw entity_json::error(entity_json_file(), pointer, "unsupported runtime scalar type");
    }
}

inline void load_entity_json_config(static_entity_manager& manager, const std::shared_ptr<class_meta>& meta,
    const entity_json::config_definition& config) {
    for (std::size_t j = 0; const auto& row : config.rows)
    {
        const auto pointer = meta->get_json_config_pointer() + "/rows/" + std::to_string(j++);
        const auto nodes = std::make_shared<node_manager>(meta);
        for (const auto& [name, value] : row)
        {
            const auto index = meta->get_index(name);
            const auto field = meta->find_data_meta(index);
            if (!field)
            {
                throw entity_json::error(
                    entity_json_file(), pointer + "/" + name, "selected schema changed during startup");
            }
            auto* node = nodes->create_data(field);
            assign_entity_json(*node, value, pointer + "/" + name);
        }
        const auto id = static_cast<id_type>(std::get<std::uint64_t>(row.at("id")));
        if (!manager.create_static_entity(meta, nodes, id))
        {
            throw entity_json::error(
                entity_json_file(), pointer + "/id", "could not create static configuration entity");
        }
    }
}

} // namespace ark::detail
