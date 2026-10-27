// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>

#include <nlohmann/json.hpp>

#include "entity_json.hpp"

namespace ark::entity_json {
namespace {
using json = nlohmann::json;

[[noreturn]] void fail(std::string_view source, const std::string& pointer, const std::string& reason) {
    throw error(std::string(source), pointer.empty() ? "/" : pointer, reason);
}

std::string escaped(std::string_view text) {
    std::string result;
    for (const auto character : text)
    {
        if (character == '~')
        {
            result += "~0";
        }
        else if (character == '/')
        {
            result += "~1";
        }
        else
        {
            result += character;
        }
    }
    return result;
}

void keys(const json& input, std::initializer_list<std::string_view> allowed, std::string_view source,
    const std::string& pointer) {
    if (!input.is_object())
    {
        fail(source, pointer, "expected an object");
    }
    for (auto entry = input.begin(); entry != input.end(); ++entry)
    {
        if (std::find(allowed.begin(), allowed.end(), entry.key()) == allowed.end())
        {
            fail(source, pointer + "/" + escaped(entry.key()), "unknown key");
        }
    }
}

const json& required(const json& input, std::string_view key, std::string_view source, const std::string& pointer) {
    const auto found = input.find(std::string(key));
    if (found == input.end())
    {
        fail(source, pointer + "/" + std::string(key), "required value is missing");
    }
    return *found;
}

const json& array(const json& input, std::string_view source, const std::string& pointer) {
    if (!input.is_array())
    {
        fail(source, pointer, "expected an array");
    }
    return input;
}

std::string text(const json& input, std::string_view source, const std::string& pointer, bool empty = false) {
    if (!input.is_string())
    {
        fail(source, pointer, "expected a string");
    }
    auto result = input.get<std::string>();
    if ((!empty && result.empty()) || result.find('\0') != std::string::npos)
    {
        fail(source, pointer, "expected a nonempty string without NUL bytes");
    }
    return result;
}

std::uint64_t unsigned_integer(
    const json& input, std::uint64_t maximum, std::string_view source, const std::string& pointer) {
    if (!input.is_number_integer() ||
        (input.is_number_integer() && !input.is_number_unsigned() && input.get<std::int64_t>() < 0))
    {
        fail(source, pointer, "expected an unsigned integer");
    }
    const auto number = input.get<std::uint64_t>();
    if (number > maximum)
    {
        fail(source, pointer, "integer is outside the declared range");
    }
    return number;
}

std::int64_t signed_integer(const json& input, std::int64_t minimum, std::int64_t maximum, std::string_view source,
    const std::string& pointer) {
    if (!input.is_number_integer())
    {
        fail(source, pointer, "expected an integer");
    }
    if (input.is_number_unsigned() && input.get<std::uint64_t>() > static_cast<std::uint64_t>(maximum))
    {
        fail(source, pointer, "integer is outside the declared range");
    }
    const auto number = input.get<std::int64_t>();
    if (number < minimum || number > maximum)
    {
        fail(source, pointer, "integer is outside the declared range");
    }
    return number;
}

value scalar(const json& input, value_type type, std::string_view source, const std::string& pointer) {
    switch (type)
    {
    case value_type::boolean:
        if (!input.is_boolean())
        {
            fail(source, pointer, "expected a boolean");
        }
        return input.get<bool>();
    case value_type::int32:
        return signed_integer(input, INT32_MIN, INT32_MAX, source, pointer);
    case value_type::int64:
        return signed_integer(input, INT64_MIN, INT64_MAX, source, pointer);
    case value_type::uint32:
        return unsigned_integer(input, UINT32_MAX, source, pointer);
    case value_type::uint64:
        return unsigned_integer(input, UINT64_MAX, source, pointer);
    case value_type::float32:
    case value_type::float64:
    {
        if (!input.is_number())
        {
            fail(source, pointer, "expected a finite number");
        }
        const auto number = input.get<double>();
        if (!std::isfinite(number) ||
            (type == value_type::float32 && (std::abs(number) > std::numeric_limits<float>::max() ||
                                                (number != 0 && static_cast<float>(number) == 0))))
        {
            fail(source, pointer, "number is outside the declared finite range");
        }
        return type == value_type::float32 ? static_cast<double>(static_cast<float>(number)) : number;
    }
    case value_type::string:
        return text(input, source, pointer, true);
    case value_type::guid:
    {
        const auto result = text(input, source, pointer);
        std::int64_t number = 0;
        const auto converted = std::from_chars(result.data(), result.data() + result.size(), number);
        if (converted.ec != std::errc{} || converted.ptr != result.data() + result.size() ||
            std::to_string(number) != result)
        {
            fail(source, pointer, "expected a canonical signed 64-bit decimal GUID string");
        }
        return result;
    }
    }
    fail(source, pointer, "unsupported scalar type");
}

std::bitset<16> mask(const json& input, std::string_view source, const std::string& pointer) {
    std::bitset<16> result;
    for (std::size_t i = 0; const auto& item : array(input, source, pointer))
    {
        const auto location = pointer + "/" + std::to_string(i++);
        const auto name = text(item, source, location);
        const auto found = std::find(mask_names.begin(), mask_names.end(), name);
        if (found == mask_names.end())
        {
            fail(source, location, "unknown mask name");
        }
        const auto bit = static_cast<std::size_t>(found - mask_names.begin());
        if (result.test(bit))
        {
            fail(source, location, "duplicate mask name");
        }
        result.set(bit);
    }
    return result;
}

void declaration(const json& input, std::string& name, std::uint32_t& index, std::bitset<16>& flags,
    std::string& description, std::string_view source, const std::string& pointer, std::set<std::string>& names,
    std::set<std::uint32_t>& indices) {
    name = text(required(input, "name", source, pointer), source, pointer + "/name");
    index = static_cast<std::uint32_t>(
        unsigned_integer(required(input, "index", source, pointer), UINT32_MAX, source, pointer + "/index"));
    if (index == 0)
    {
        fail(source, pointer + "/index", "field indices start at one");
    }
    if (!names.insert(name).second)
    {
        fail(source, pointer + "/name", "duplicate member name");
    }
    if (!indices.insert(index).second)
    {
        fail(source, pointer + "/index", "duplicate member index");
    }
    if (input.contains("mask"))
    {
        flags = mask(input["mask"], source, pointer + "/mask");
    }
    if (input.contains("description"))
    {
        description = text(input["description"], source, pointer + "/description", true);
    }
}

json strict_json(std::string_view input, std::string_view source) {
    struct level
    {
        std::string path;
        std::string key;
        std::size_t next{0};
        bool object;
        std::set<std::string> keys;
    };

    std::vector<level> levels;
    const auto child = [&levels]() {
        if (levels.empty())
        {
            return std::string{};
        }
        auto& parent = levels.back();
        return parent.path + "/" + (parent.object ? escaped(parent.key) : std::to_string(parent.next++));
    };
    try
    {
        return json::parse(input.begin(), input.end(), [&](int depth, json::parse_event_t event, json& parsed) {
            if (depth > 64)
            {
                fail(source, levels.empty() ? "/" : levels.back().path, "JSON nesting exceeds 64 levels");
            }
            if (event == json::parse_event_t::object_start || event == json::parse_event_t::array_start)
            {
                levels.push_back({child(), {}, 0, event == json::parse_event_t::object_start, {}});
            }
            else if (event == json::parse_event_t::object_end || event == json::parse_event_t::array_end)
            {
                levels.pop_back();
            }
            else if (event == json::parse_event_t::key)
            {
                auto& current = levels.back();
                current.key = parsed.get<std::string>();
                if (!current.keys.insert(current.key).second)
                {
                    fail(source, current.path + "/" + escaped(current.key), "duplicate object key");
                }
            }
            else if (event == json::parse_event_t::value && !levels.empty() && !levels.back().object)
            {
                ++levels.back().next;
            }
            return true;
        });
    }
    catch (const json::exception& exception)
    { fail(source, "/", std::string("invalid JSON: ") + exception.what()); }
}

json encode(const document& input) {
    const auto encode_mask = [](const std::bitset<16>& flags) {
        if ((flags >> mask_names.size()).any())
        {
            throw std::invalid_argument("unsupported mask bits");
        }
        json output = json::array();
        for (std::size_t i = 0; i < mask_names.size(); ++i)
        {
            if (flags.test(i))
            {
                output.push_back(mask_names[i]);
            }
        }
        return output;
    };
    json output{
        {"format", "ark.entity-schema"}, {"version", 1}, {"classes", json::array()}, {"configs", json::array()}};
    for (const auto& definition : input.classes)
    {
        json item{{"name", definition.name}, {"fields", json::array()}, {"tables", json::array()},
            {"containers", json::array()}};
        if (!definition.description.empty())
        {
            item["description"] = definition.description;
        }
        for (const auto& member : definition.fields)
        {
            json field{{"name", member.name}, {"index", member.index}, {"type", type_name(member.type)},
                {"mask", encode_mask(member.mask)}};
            if (!member.description.empty())
            {
                field["description"] = member.description;
            }
            item["fields"].push_back(std::move(field));
        }
        const auto relations = [&](const std::vector<relation>& members, const char* key) {
            for (const auto& member : members)
            {
                json relation{{"name", member.name}, {"index", member.index}, {"class", member.class_name},
                    {"mask", encode_mask(member.mask)}};
                if (!member.description.empty())
                {
                    relation["description"] = member.description;
                }
                item[key].push_back(std::move(relation));
            }
        };
        relations(definition.tables, "tables");
        relations(definition.containers, "containers");
        output["classes"].push_back(std::move(item));
    }
    for (const auto& definition : input.configs)
    {
        json rows = json::array();
        for (const auto& values : definition.rows)
        {
            json row = json::object();
            for (const auto& [name, scalar] : values)
            {
                row[name] = std::visit([](const auto& value) -> json { return value; }, scalar);
            }
            rows.push_back(std::move(row));
        }
        output["configs"].push_back({{"class", definition.class_name}, {"rows", std::move(rows)}});
    }
    return output;
}
} // namespace

error::error(std::string source, std::string pointer, std::string reason)
    : std::runtime_error(source + ": " + pointer + ": " + reason)
    , source_(std::move(source))
    , pointer_(std::move(pointer)) {
}

const std::string& error::source() const noexcept {
    return source_;
}

const std::string& error::pointer() const noexcept {
    return pointer_;
}

std::string_view type_name(value_type type) {
    switch (type)
    {
    case value_type::boolean:
        return "bool";
    case value_type::int32:
        return "int32";
    case value_type::uint32:
        return "uint32";
    case value_type::int64:
        return "int64";
    case value_type::uint64:
        return "uint64";
    case value_type::float32:
        return "float";
    case value_type::float64:
        return "double";
    case value_type::string:
        return "string";
    case value_type::guid:
        return "guid";
    }
    throw std::invalid_argument("unsupported scalar type");
}

value_type parse_type(std::string_view name, std::string_view source, std::string_view pointer) {
    for (const auto type : {value_type::boolean, value_type::int32, value_type::uint32, value_type::int64,
             value_type::uint64, value_type::float32, value_type::float64, value_type::string, value_type::guid})
    {
        if (name == type_name(type))
        {
            return type;
        }
    }
    fail(source, std::string(pointer), "unsupported scalar type: " + std::string(name));
}

document parse(std::string_view text_input, std::string_view source) {
    const auto input = strict_json(text_input, source);
    keys(input, {"format", "version", "classes", "configs"}, source, "");
    if (required(input, "format", source, "") != "ark.entity-schema")
    {
        fail(source, "/format", "expected ark.entity-schema");
    }
    if (unsigned_integer(required(input, "version", source, ""), UINT32_MAX, source, "/version") != 1)
    {
        fail(source, "/version", "unsupported format version");
    }
    document result;
    std::map<std::string, std::size_t> class_names;
    for (std::size_t i = 0; const auto& definition : array(required(input, "classes", source, ""), source, "/classes"))
    {
        const auto pointer = "/classes/" + std::to_string(i++);
        keys(definition, {"name", "fields", "tables", "containers", "description"}, source, pointer);
        class_definition item;
        item.name = text(required(definition, "name", source, pointer), source, pointer + "/name");
        if (!class_names.emplace(item.name, result.classes.size()).second)
        {
            fail(source, pointer + "/name", "duplicate class name");
        }
        if (definition.contains("description"))
        {
            item.description = text(definition["description"], source, pointer + "/description", true);
        }
        std::set<std::string> member_names;
        std::set<std::uint32_t> indices;
        for (std::size_t j = 0;
             const auto& member : array(required(definition, "fields", source, pointer), source, pointer + "/fields"))
        {
            const auto location = pointer + "/fields/" + std::to_string(j++);
            keys(member, {"name", "index", "type", "mask", "description"}, source, location);
            field field;
            declaration(member, field.name, field.index, field.mask, field.description, source, location, member_names,
                indices);
            field.type = parse_type(text(required(member, "type", source, location), source, location + "/type"),
                source, location + "/type");
            item.fields.push_back(std::move(field));
        }
        const auto relations = [&](const char* key, std::vector<relation>& members) {
            for (std::size_t j = 0;
                 const auto& member : array(required(definition, key, source, pointer), source, pointer + "/" + key))
            {
                const auto location = pointer + "/" + key + "/" + std::to_string(j++);
                keys(member, {"name", "index", "class", "mask", "description"}, source, location);
                relation relation;
                declaration(member, relation.name, relation.index, relation.mask, relation.description, source,
                    location, member_names, indices);
                relation.class_name = text(required(member, "class", source, location), source, location + "/class");
                members.push_back(std::move(relation));
            }
        };
        relations("tables", item.tables);
        relations("containers", item.containers);
        result.classes.push_back(std::move(item));
    }
    for (std::size_t i = 0; const auto& definition : result.classes)
    {
        for (const auto* kind : {"tables", "containers"})
        {
            const auto& relations = std::string_view(kind) == "tables" ? definition.tables : definition.containers;
            for (std::size_t j = 0; const auto& relation : relations)
            {
                const auto pointer =
                    "/classes/" + std::to_string(i) + "/" + kind + "/" + std::to_string(j++) + "/class";
                const auto found = class_names.find(relation.class_name);
                if (found == class_names.end())
                {
                    fail(source, pointer, "referenced class does not exist");
                }
                const auto& target = result.classes[found->second];
                if (std::string_view(kind) == "tables" && (!target.tables.empty() || !target.containers.empty()))
                {
                    fail(source, pointer, "table columns must reference a scalar-only class");
                }
            }
        }
        ++i;
    }
    std::set<std::string> config_classes;
    std::set<std::uint32_t> ids;
    for (std::size_t i = 0; const auto& definition : array(required(input, "configs", source, ""), source, "/configs"))
    {
        const auto pointer = "/configs/" + std::to_string(i++);
        keys(definition, {"class", "rows"}, source, pointer);
        config_definition config;
        config.class_name = text(required(definition, "class", source, pointer), source, pointer + "/class");
        const auto found = class_names.find(config.class_name);
        if (found == class_names.end())
        {
            fail(source, pointer + "/class", "configuration class does not exist");
        }
        if (!config_classes.insert(config.class_name).second)
        {
            fail(source, pointer + "/class", "duplicate configuration class");
        }
        const auto& schema = result.classes[found->second];
        const auto id_field = std::find_if(
            schema.fields.begin(), schema.fields.end(), [](const auto& field) { return field.name == "id"; });
        if (id_field == schema.fields.end() || id_field->type != value_type::uint32)
        {
            fail(source, pointer, "configuration requires a uint32 id field");
        }
        for (std::size_t j = 0;
             const auto& values : array(required(definition, "rows", source, pointer), source, pointer + "/rows"))
        {
            const auto location = pointer + "/rows/" + std::to_string(j++);
            if (!values.is_object())
            {
                fail(source, location, "expected a row object");
            }
            for (auto member = values.begin(); member != values.end(); ++member)
            {
                if (std::none_of(schema.fields.begin(), schema.fields.end(),
                        [&](const auto& field) { return field.name == member.key(); }))
                {
                    fail(source, location + "/" + escaped(member.key()), "unknown configuration field");
                }
            }
            row output;
            for (const auto& member : schema.fields)
            {
                output.emplace(member.name, scalar(required(values, member.name, source, location), member.type, source,
                                                location + "/" + escaped(member.name)));
            }
            const auto id = static_cast<std::uint32_t>(std::get<std::uint64_t>(output.at("id")));
            if (id == 0 || !ids.insert(id).second)
            {
                fail(source, location + "/id", "configuration id must be positive and globally unique");
            }
            config.rows.push_back(std::move(output));
        }
        result.configs.push_back(std::move(config));
    }
    return result;
}

document load(const std::string& path) {
    if (path.empty() || path.find('\0') != std::string::npos)
    {
        fail(path, "/", "expected a nonempty input path without NUL bytes");
    }
    std::ifstream stream(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary);
    if (!stream)
    {
        fail(path, "/", "could not open JSON input");
    }
    std::ostringstream contents;
    contents << stream.rdbuf();
    if (stream.bad())
    {
        fail(path, "/", "could not read JSON input");
    }
    return parse(contents.str(), path);
}

void validate(const document& input, std::string_view source) {
    try
    { static_cast<void>(parse(encode(input).dump(), source)); }
    catch (const error&)
    { throw; }
    catch (const std::exception& exception)
    { fail(source, "/", exception.what()); }
}

std::string serialize(const document& input) {
    validate(input);
    return encode(parse(encode(input).dump(), "<model>")).dump(2) + "\n";
}

} // namespace ark::entity_json
