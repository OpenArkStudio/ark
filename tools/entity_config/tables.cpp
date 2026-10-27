// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <charconv>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>

#include <OpenXLSX.hpp>

#include "exporter.hpp"

namespace ark::entity_export {
namespace {

std::string decimal(double value) {
    if (!std::isfinite(value))
    {
        throw std::runtime_error("non-finite spreadsheet number");
    }
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (result.ec != std::errc{})
    {
        throw std::runtime_error("could not represent spreadsheet number");
    }
    return std::string(buffer, result.ptr);
}

table worksheet(OpenXLSX::XLWorksheet sheet, const std::string& source) {
    table output{source + "[" + sheet.name() + "]", {}};
    const auto columns = sheet.columnCount();
    const auto rows = sheet.rowCount();
    for (std::uint32_t row = 1; row <= rows; ++row)
    {
        std::vector<cell> values;
        for (std::uint32_t column = 1; column <= columns; ++column)
        {
            auto item = sheet.cell(row, static_cast<std::uint16_t>(column));
            if (item.hasFormula())
            {
                throw std::runtime_error(output.source + ": row " + std::to_string(row) +
                                         " contains a formula; export evaluated values or CSV instead");
            }
            const auto& value = item.value();
            switch (value.type())
            {
            case OpenXLSX::XLValueType::Empty:
                values.push_back({"", cell_kind::empty});
                break;
            case OpenXLSX::XLValueType::String:
                values.push_back({value.get<std::string>(), cell_kind::text});
                break;
            case OpenXLSX::XLValueType::Integer:
                values.push_back({std::to_string(value.get<std::int64_t>()), cell_kind::number});
                break;
            case OpenXLSX::XLValueType::Float:
            {
                const auto number = value.get<double>();
                // Small integral values should retain integer spelling for typed integer columns.
                if (std::isfinite(number) && std::trunc(number) == number && std::abs(number) <= 9007199254740991.0)
                {
                    values.push_back({std::to_string(static_cast<std::int64_t>(number)), cell_kind::number});
                }
                else
                {
                    values.push_back({decimal(number), cell_kind::number});
                }
                break;
            }
            case OpenXLSX::XLValueType::Boolean:
                values.push_back({value.get<bool>() ? "true" : "false", cell_kind::boolean});
                break;
            default:
                throw std::runtime_error(
                    output.source + ": row " + std::to_string(row) + " contains an Excel error value");
            }
        }
        output.rows.push_back(std::move(values));
    }
    return output;
}

} // namespace

table parse_csv(std::string_view input, std::string source) {
    if (input.starts_with("\xef\xbb\xbf"))
    {
        input.remove_prefix(3);
    }
    table output{std::move(source), {}};
    std::vector<cell> row;
    std::string field;
    bool quoted = false, closed = false, pending = false;
    const auto finish_field = [&] {
        row.push_back({std::move(field), cell_kind::text});
        field.clear();
        closed = false;
        pending = false;
    };
    for (std::size_t i = 0; i < input.size(); ++i)
    {
        const char c = input[i];
        if (quoted)
        {
            if (c == '"')
            {
                if (i + 1 < input.size() && input[i + 1] == '"')
                {
                    field += '"';
                    ++i;
                }
                else
                {
                    quoted = false;
                    closed = true;
                }
            }
            else
            {
                field += c;
            }
            continue;
        }
        if (c == ',')
        {
            finish_field();
            pending = true;
        }
        else if (c == '\r' || c == '\n')
        {
            finish_field();
            output.rows.push_back(std::move(row));
            row.clear();
            if (c == '\r' && i + 1 < input.size() && input[i + 1] == '\n')
            {
                ++i;
            }
        }
        else if (c == '"' && field.empty() && !closed)
        {
            quoted = true;
            pending = true;
        }
        else
        {
            if (closed || c == '"')
            {
                throw std::runtime_error(
                    output.source + ": malformed CSV quoting near record " + std::to_string(output.rows.size() + 1));
            }
            field += c;
            pending = true;
        }
    }
    if (quoted)
    {
        throw std::runtime_error(output.source + ": unterminated CSV quoted field");
    }
    if (pending || closed || !row.empty() || !field.empty())
    {
        finish_field();
        output.rows.push_back(std::move(row));
    }
    return output;
}

std::map<std::string, table> read_workbook(const std::filesystem::path& path) {
    OpenXLSX::XLDocument document;
    document.open(utf8_string(path));
    std::map<std::string, table> result;
    for (const auto& name : document.workbook().worksheetNames())
    {
        result.emplace(name, worksheet(document.workbook().worksheet(name), utf8_string(path)));
    }
    document.close();
    return result;
}

table read_table(const std::filesystem::path& path, const std::string& sheet) {
    auto extension = utf8_string(path.extension());
    for (auto& c : extension)
    {
        if (c >= 'A' && c <= 'Z')
        {
            c = static_cast<char>(c + ('a' - 'A'));
        }
    }
    if (extension == ".xlsx")
    {
        auto workbook = read_workbook(path);
        if (!sheet.empty())
        {
            const auto it = workbook.find(sheet);
            if (it == workbook.end())
            {
                throw std::runtime_error(utf8_string(path) + ": missing worksheet " + sheet);
            }
            return std::move(it->second);
        }
        if (workbook.size() != 1)
        {
            throw std::runtime_error(utf8_string(path) + ": select an explicit worksheet for a multi-sheet input");
        }
        return std::move(workbook.begin()->second);
    }
    if (extension != ".csv")
    {
        throw std::runtime_error(utf8_string(path) + ": expected .csv or .xlsx input");
    }
    if (!sheet.empty())
    {
        throw std::runtime_error(utf8_string(path) + ": a CSV file has no worksheets");
    }
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        throw std::runtime_error(utf8_string(path) + ": cannot open input");
    }
    const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    if (file.bad())
    {
        throw std::runtime_error(utf8_string(path) + ": input read failed");
    }
    return parse_csv(text, utf8_string(path));
}

} // namespace ark::entity_export
