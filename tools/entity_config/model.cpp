// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <algorithm>
#include <charconv>
#include <cmath>
#include <deque>
#include <limits>
#include <set>
#include <stdexcept>
#include <tuple>

#include "exporter.hpp"

namespace ark::entity_export {
namespace {

std::string trim(std::string value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
    {
        return {};
    }
    return value.substr(begin, value.find_last_not_of(" \t\r\n") - begin + 1);
}

std::vector<std::string> split(const std::string& text) {
    std::vector<std::string> output;
    std::size_t start = 0;
    while (start < text.size())
    {
        const auto end = text.find('|', start);
        auto item = trim(text.substr(start, end == std::string::npos ? end : end - start));
        if (item.empty())
        {
            throw std::runtime_error("empty item in | separated list");
        }
        output.push_back(std::move(item));
        if (end == std::string::npos)
        {
            break;
        }
        start = end + 1;
        if (start == text.size())
        {
            throw std::runtime_error("trailing empty item in | separated list");
        }
    }
    return output;
}

struct columns
{
    explicit columns(const table& input, bool schema_aliases = false) {
        if (input.rows.empty())
        {
            throw std::runtime_error(input.source + ": empty table");
        }
        width = input.rows.front().size();
        for (std::size_t i = 0; i < width; ++i)
        {
            auto name = trim(input.rows.front()[i].text);
            if (schema_aliases && name == "field_name")
            {
                name = "name";
            }
            else if (schema_aliases && name == "field_index")
            {
                name = "index";
            }
            else if (schema_aliases && name == "type_class")
            {
                name = "reference";
            }
            if (name.empty() || !indices.emplace(name, i).second)
            {
                throw std::runtime_error(input.source + ": blank or duplicate column " + name);
            }
        }
    }

    cell get(const std::vector<cell>& row, const std::string& name) const {
        const auto it = indices.find(name);
        return it == indices.end() || it->second >= row.size() ? cell{"", cell_kind::empty} : row[it->second];
    }

    std::map<std::string, std::size_t> indices;
    std::size_t width;
};

bool empty(const std::vector<cell>& row) {
    return std::all_of(row.begin(), row.end(), [](const auto& item) { return item.text.empty(); });
}

template<typename number>
number integer(std::string text) {
    text = trim(std::move(text));
    number result{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
    {
        throw std::runtime_error("expected an in-range decimal integer");
    }
    return result;
}

bool boolean(std::string text) {
    text = trim(std::move(text));
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : static_cast<char>(c);
    });
    if (text == "true" || text == "1")
    {
        return true;
    }
    if (text == "false" || text == "0")
    {
        return false;
    }
    throw std::runtime_error("expected true/false or 1/0");
}

entity_json::value value(const cell& input, entity_json::value_type type) {
    using enum entity_json::value_type;
    if (type == string || type == guid)
    {
        if (input.kind != cell_kind::text && input.kind != cell_kind::empty)
        {
            throw std::runtime_error("string/GUID values require text cells in XLSX");
        }
        return input.text;
    }
    if (type == boolean)
    {
        return entity_export::boolean(input.text);
    }
    if (input.kind == cell_kind::boolean)
    {
        throw std::runtime_error("boolean spreadsheet cell is not a numeric value");
    }
    if (type == int32 || type == int64)
    {
        const auto result = integer<std::int64_t>(input.text);
        if (input.kind == cell_kind::number && (result > 9007199254740991LL || result < -9007199254740991LL))
        {
            throw std::runtime_error("wide integer must be an XLSX text cell to preserve its exact value");
        }
        return result;
    }
    if (type == uint32 || type == uint64)
    {
        const auto result = integer<std::uint64_t>(input.text);
        if (input.kind == cell_kind::number && result > 9007199254740991ULL)
        {
            throw std::runtime_error("wide integer must be an XLSX text cell to preserve its exact value");
        }
        return result;
    }
    const auto text = trim(input.text);
    double result = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !std::isfinite(result))
    {
        throw std::runtime_error("expected a finite number");
    }
    return result;
}

std::bitset<16> mask(const columns& header, const std::vector<cell>& row) {
    std::bitset<16> output;
    for (const auto& name : split(header.get(row, "mask").text))
    {
        const auto it = std::find(entity_json::mask_names.begin(), entity_json::mask_names.end(), name);
        if (it == entity_json::mask_names.end())
        {
            throw std::runtime_error("unknown mask " + name);
        }
        const auto bit = static_cast<std::size_t>(it - entity_json::mask_names.begin());
        if (output.test(bit))
        {
            throw std::runtime_error("duplicate mask " + name);
        }
        output.set(bit);
    }
    for (std::size_t i = 0; i < entity_json::mask_names.size(); ++i)
    {
        const auto text = header.get(row, std::string(entity_json::mask_names[i])).text;
        if (!text.empty() && boolean(text))
        {
            if (output.test(i))
            {
                throw std::runtime_error("mask specified twice: " + std::string(entity_json::mask_names[i]));
            }
            output.set(i);
        }
    }
    return output;
}

bool same(const entity_json::field& a, const entity_json::field& b) {
    return std::tie(a.name, a.index, a.type, a.mask, a.description) ==
           std::tie(b.name, b.index, b.type, b.mask, b.description);
}

bool same(const entity_json::relation& a, const entity_json::relation& b) {
    return std::tie(a.name, a.index, a.class_name, a.mask, a.description) ==
           std::tie(b.name, b.index, b.class_name, b.mask, b.description);
}

template<typename member>
void inherit(std::vector<member>& into, const std::vector<member>& from) {
    for (const auto& item : from)
    {
        const auto existing = std::find_if(into.begin(), into.end(),
            [&](const auto& candidate) { return candidate.name == item.name || candidate.index == item.index; });
        if (existing == into.end())
        {
            into.push_back(item);
        }
        else if (!same(*existing, item))
        {
            throw std::runtime_error("conflicting inherited name/index: " + item.name);
        }
    }
}

} // namespace

entity_json::document read_fields(const table& input) {
    const columns header(input, true);
    for (const auto* required : {"class", "index", "name", "type"})
    {
        if (!header.indices.contains(required))
        {
            throw std::runtime_error(
                input.source + ": missing required column " + required + "; field indices must be explicit and stable");
        }
    }
    for (const auto& [name, index] : header.indices)
    {
        if (name != "class" && name != "index" && name != "name" && name != "type" && name != "reference" &&
            name != "mask" && name != "description" &&
            std::find(entity_json::mask_names.begin(), entity_json::mask_names.end(), name) ==
                entity_json::mask_names.end())
        {
            throw std::runtime_error(input.source + ": unknown field-table column " + name);
        }
    }
    entity_json::document result;
    std::map<std::string, std::size_t> indices;
    std::map<std::string, std::vector<std::string>> parents;
    std::set<std::string> declarations;
    for (std::size_t i = 1; i < input.rows.size(); ++i)
    {
        const auto& row = input.rows[i];
        if (empty(row))
        {
            continue;
        }
        try
        {
            if (row.size() > header.width)
            {
                throw std::runtime_error("row has more cells than the header");
            }
            const auto name = trim(header.get(row, "class").text);
            if (name.empty())
            {
                throw std::runtime_error("class must not be blank");
            }
            if (!indices.contains(name))
            {
                indices.emplace(name, result.classes.size());
                result.classes.push_back({name, {}, {}, {}, {}});
            }
            auto& definition = result.classes[indices.at(name)];
            const auto type = trim(header.get(row, "type").text);
            const auto reference = trim(header.get(row, "reference").text);
            const auto description = header.get(row, "description").text;
            if (type == "class")
            {
                if (!declarations.insert(name).second || !header.get(row, "index").text.empty() ||
                    !header.get(row, "name").text.empty() || mask(header, row).any())
                {
                    throw std::runtime_error("invalid or duplicate class declaration");
                }
                definition.description = description;
                parents[name] = split(reference);
                continue;
            }
            const auto field_name = trim(header.get(row, "name").text);
            const auto index = integer<std::uint32_t>(header.get(row, "index").text);
            if (type == "table" || type == "container")
            {
                auto& destination = type == "table" ? definition.tables : definition.containers;
                destination.push_back({field_name, index, reference, mask(header, row), description});
            }
            else
            {
                if (!reference.empty())
                {
                    throw std::runtime_error("only class/table/container rows accept a reference");
                }
                const auto normalized = type == "int" ? "int32" : type == "object" ? "guid" : type;
                definition.fields.push_back({field_name, index, entity_json::parse_type(normalized, input.source),
                    mask(header, row), description});
            }
        }
        catch (const std::exception& error)
        { throw std::runtime_error(input.source + ": row " + std::to_string(i + 1) + ": " + error.what()); }
    }
    // Validate local declarations before flattening so duplicate source rows cannot disappear.
    entity_json::validate(result, input.source);
    std::vector<std::vector<std::size_t>> children(result.classes.size());
    std::vector<std::size_t> remaining(result.classes.size(), 0);
    for (const auto& [child, bases] : parents)
    {
        std::set<std::string> seen;
        for (const auto& base : bases)
        {
            if (!indices.contains(base) || !seen.insert(base).second)
            {
                throw std::runtime_error(input.source + ": missing or duplicate parent " + base);
            }
            children[indices.at(base)].push_back(indices.at(child));
            ++remaining[indices.at(child)];
        }
    }
    std::deque<std::size_t> ready;
    for (std::size_t i = 0; i < remaining.size(); ++i)
    {
        if (remaining[i] == 0)
        {
            ready.push_back(i);
        }
    }
    std::size_t processed = 0;
    while (!ready.empty())
    {
        const auto index = ready.front();
        ready.pop_front();
        auto& definition = result.classes[index];
        for (const auto& base : parents[definition.name])
        {
            const auto& inherited = result.classes[indices.at(base)];
            inherit(definition.fields, inherited.fields);
            inherit(definition.tables, inherited.tables);
            inherit(definition.containers, inherited.containers);
        }
        ++processed;
        for (const auto child : children[index])
        {
            if (--remaining[child] == 0)
            {
                ready.push_back(child);
            }
        }
    }
    if (processed != result.classes.size())
    {
        throw std::runtime_error(input.source + ": class inheritance cycle");
    }
    entity_json::validate(result, input.source);
    return result;
}

void add_config(entity_json::document& output, const std::string& class_name, const table& input) {
    const auto definition = std::find_if(
        output.classes.begin(), output.classes.end(), [&](const auto& item) { return item.name == class_name; });
    if (definition == output.classes.end())
    {
        throw std::runtime_error(input.source + ": unknown config class " + class_name);
    }
    const columns header(input);
    if (header.indices.size() != definition->fields.size())
    {
        throw std::runtime_error(input.source + ": data columns must match every scalar field exactly");
    }
    for (const auto& field : definition->fields)
    {
        if (!header.indices.contains(field.name))
        {
            throw std::runtime_error(input.source + ": missing data column " + field.name);
        }
    }
    entity_json::config_definition config{class_name, {}};
    for (std::size_t i = 1; i < input.rows.size(); ++i)
    {
        const auto& row = input.rows[i];
        if (empty(row))
        {
            continue;
        }
        if (row.size() > header.width)
        {
            throw std::runtime_error(input.source + ": extra data cells at row " + std::to_string(i + 1));
        }
        entity_json::row values;
        for (const auto& field : definition->fields)
        {
            try
            { values.emplace(field.name, value(header.get(row, field.name), field.type)); }
            catch (const std::exception& error)
            {
                throw std::runtime_error(
                    input.source + ": row " + std::to_string(i + 1) + ", field " + field.name + ": " + error.what());
            }
        }
        config.rows.push_back(std::move(values));
    }
    output.configs.push_back(std::move(config));
    try
    { entity_json::validate(output, input.source); }
    catch (...)
    {
        output.configs.pop_back();
        throw;
    }
}

} // namespace ark::entity_export
