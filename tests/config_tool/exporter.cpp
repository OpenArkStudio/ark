// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <random>

#include "../network/doctest.h"
#include <OpenXLSX.hpp>

#include "exporter.hpp"

namespace {

struct temporary_directory
{
    std::filesystem::path path;

    temporary_directory() {
        std::random_device random;
        path = std::filesystem::temp_directory_path() / ("ark-config-tool-" + std::to_string(random()));
        REQUIRE(std::filesystem::create_directory(path));
    }

    ~temporary_directory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

std::string read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

ark::entity_json::document example() {
    const std::filesystem::path root = ARK_CONFIG_TOOL_FIXTURE_DIR;
    auto result = ark::entity_export::read_fields(ark::entity_export::read_table(root / "fields.csv"));
    ark::entity_export::add_config(result, "GuestPlayer", ark::entity_export::read_table(root / "GuestPlayer.csv"));
    return result;
}

} // namespace

TEST_CASE("CSV retains BOM, empty values, quoted delimiters and multiline strings") {
    using ark::entity_export::parse_csv;
    const auto table = parse_csv("\xef\xbb\xbf"
                                 "a,b,c\r\n1,\"two,three\",\"x\"\"y\r\nz\"\r\n2,,",
        "input.csv");
    REQUIRE(table.rows.size() == 3);
    CHECK(table.rows[0][0].text == "a");
    CHECK(table.rows[1][1].text == "two,three");
    CHECK(table.rows[1][2].text == "x\"y\r\nz");
    REQUIRE(table.rows[2].size() == 3);
    CHECK(table.rows[2][2].text.empty());
    CHECK_THROWS(parse_csv("a\n\"unterminated", "bad.csv"));
    CHECK_THROWS(parse_csv("a\n\"closed\"junk", "bad.csv"));
    CHECK_THROWS(parse_csv("a\nx\"y", "bad.csv"));
}

TEST_CASE("Field indices, masks and profile values survive shared-model roundtrip") {
    const auto original = example();
    const auto copy = ark::entity_json::parse(ark::entity_json::serialize(original));
    REQUIRE(copy.classes.size() == 2);
    REQUIRE(copy.configs.size() == 1);
    CHECK(copy.classes[1].fields[5].name == "hp");
    CHECK(copy.classes[1].fields[5].index == 104);
    CHECK(copy.classes[1].fields[5].mask.test(0));
    CHECK(copy.classes[1].fields[5].mask.test(1));
    CHECK(std::get<std::uint64_t>(copy.configs[0].rows[0].at("hp")) == 100);
    CHECK(ark::entity_export::cpp_bindings(copy).find("hp = 104u;") != std::string::npos);
    CHECK(ark::entity_export::csharp_bindings(copy).find("Hp = 104u;") != std::string::npos);
}

TEST_CASE("Invalid tables fail before an existing export can be changed") {
    using namespace ark::entity_export;
    CHECK_THROWS(read_fields(parse_csv("class,name,type\nGuest,hp,uint32\n", "missing-index.csv")));
    CHECK_THROWS(
        read_fields(parse_csv("class,index,name,type\nGuest,1,hp,uint32\nGuest,1,mp,uint32\n", "duplicate.csv")));
    CHECK_THROWS(read_fields(parse_csv("class,index,name,type,mask\nGuest,1,hp,uint32,mystery\n", "mask.csv")));
    auto document = example();
    CHECK_THROWS(add_config(document, "GuestPlayer", parse_csv("id,hp\n1,100\n", "missing-fields.csv")));
    CHECK_THROWS(add_config(document, "Missing", parse_csv("id\n1\n", "unknown-class.csv")));
    auto wide = read_fields(parse_csv("class,index,name,type\nWide,99,id,uint32\nWide,100,value,uint64\n", "wide.csv"));
    add_config(wide, "Wide", parse_csv("id,value\n1,18446744073709551615\n", "wide-data.csv"));
    CHECK(std::get<std::uint64_t>(wide.configs[0].rows[0].at("value")) == std::numeric_limits<std::uint64_t>::max());
    CHECK_THROWS(add_config(wide, "Wide", parse_csv("id,value\n2,18446744073709551616\n", "overflow.csv")));
    REQUIRE(wide.configs.size() == 1);
    CHECK_THROWS(add_config(wide, "Wide", parse_csv("id,value\n1,4\n", "duplicate-id.csv")));
    CHECK(wide.configs.size() == 1);
}

TEST_CASE("Explicit inheritance flattens with stable indices and detects cycles or conflicts") {
    using namespace ark::entity_export;
    const auto document = read_fields(parse_csv("class,index,name,type,reference\nBase,100,hp,uint32,\n"
                                                "Guest,,,class,Base\nGuest,101,mp,uint32,\n",
        "inherit.csv"));
    REQUIRE(document.classes[1].fields.size() == 2);
    CHECK(document.classes[1].fields[1].name == "hp");
    CHECK(document.classes[1].fields[1].index == 100);
    CHECK_THROWS(read_fields(parse_csv("class,index,name,type,reference\nA,,,class,B\nB,,,class,A\n", "cycle.csv")));
    CHECK_THROWS(read_fields(parse_csv("class,index,name,type,reference\nBase,100,hp,uint32,\n"
                                       "Guest,,,class,Base\nGuest,100,mp,uint32,\n",
        "conflict.csv")));
}

TEST_CASE("Real XLSX worksheets export through the same schema and typed-row validation") {
    using namespace ark::entity_export;
    temporary_directory temp;
    const auto file = temp.path / utf8_path("属性 source.xlsx");
    OpenXLSX::XLDocument workbook;
    workbook.create(utf8_string(file), OpenXLSX::XLDoNotOverwrite);
    workbook.workbook().worksheet(1).setName("fields");
    workbook.workbook().addWorksheet("GuestPlayer");
    for (const auto& [name, source] :
        std::map<std::string, std::string>{{"fields", "fields.csv"}, {"GuestPlayer", "GuestPlayer.csv"}})
    {
        const auto table = read_table(std::filesystem::path(ARK_CONFIG_TOOL_FIXTURE_DIR) / source);
        auto sheet = workbook.workbook().worksheet(name);
        for (std::size_t row = 0; row < table.rows.size(); ++row)
        {
            for (std::size_t column = 0; column < table.rows[row].size(); ++column)
            {
                sheet.cell(static_cast<std::uint32_t>(row + 1), static_cast<std::uint16_t>(column + 1)).value() =
                    table.rows[row][column].text;
            }
        }
    }
    workbook.save();
    workbook.close();
    const auto tables = read_workbook(file);
    auto output = read_fields(tables.at("fields"));
    add_config(output, "GuestPlayer", tables.at("GuestPlayer"));
    CHECK(ark::entity_json::serialize(output) == ark::entity_json::serialize(example()));
    CHECK_THROWS(read_table(file));
    CHECK(read_table(file, "fields").rows.size() == 13);
}

TEST_CASE("Atomic bundle replacement preserves unrelated directories and old valid exports") {
    using namespace ark::entity_export;
    temporary_directory temp;
    const auto directory = temp.path / "bundle with spaces";
    const auto document = example();
    export_bundle(document, directory);
    const auto original = read(directory / "meta/entity_schema.json");
    CHECK_THROWS(export_bundle(document, directory));
    CHECK(read(directory / "meta/entity_schema.json") == original);
    auto invalid = document;
    invalid.classes[1].fields[1].index = invalid.classes[1].fields[0].index;
    CHECK_THROWS(export_bundle(invalid, directory, true));
    CHECK(read(directory / "meta/entity_schema.json") == original);
    export_bundle(document, directory, true, false);
    CHECK_FALSE(std::filesystem::exists(directory / "code"));
    CHECK(read(directory / "meta/entity_schema.json") == original);
    const auto unrelated = temp.path / "unrelated";
    std::filesystem::create_directory(unrelated);
    std::ofstream(unrelated / "keep.txt") << "keep";
    CHECK_THROWS(export_bundle(document, unrelated, true));
    CHECK(read(unrelated / "keep.txt") == "keep");
    auto collision = document;
    collision.classes[1].fields[1].name = "fieldName";
    collision.classes[1].fields[2].name = "field_name";
    collision.configs.clear();
    CHECK_THROWS(cpp_bindings(collision));
}

TEST_CASE("XLSX scalar types retain exact text integers and reject unsafe numeric or string coercion") {
    using namespace ark::entity_export;
    temporary_directory temp;
    const auto file = temp.path / "typed.xlsx";
    auto fields = read_fields(
        parse_csv("class,index,name,type\nWide,99,id,uint32\nWide,100,value,uint64\n"
                  "Wide,101,flag,bool\nWide,102,ratio,double\nWide,103,label,string\nWide,104,field_name,string\n",
            "types.csv"));
    OpenXLSX::XLDocument workbook;
    workbook.create(file.string(), OpenXLSX::XLDoNotOverwrite);
    auto sheet = workbook.workbook().worksheet(1);
    const std::vector<std::string> names{"id", "value", "flag", "ratio", "label", "field_name"};
    for (std::size_t i = 0; i < names.size(); ++i)
    {
        sheet.cell(1, static_cast<std::uint16_t>(i + 1)).value() = names[i];
    }
    sheet.cell("A2").value() = 1;
    sheet.cell("B2").value() = "18446744073709551615";
    sheet.cell("C2").value() = true;
    sheet.cell("D2").value() = 0.125;
    sheet.cell("E2").value() = "中文, quoted";
    sheet.cell("F2").value() = "literal column name";
    workbook.save();
    auto output = fields;
    add_config(output, "Wide", read_table(file));
    const auto& row = output.configs[0].rows[0];
    CHECK(std::get<std::uint64_t>(row.at("value")) == std::numeric_limits<std::uint64_t>::max());
    CHECK(std::get<bool>(row.at("flag")));
    CHECK(std::get<double>(row.at("ratio")) == 0.125);
    CHECK(std::get<std::string>(row.at("label")) == "中文, quoted");
    CHECK(std::get<std::string>(row.at("field_name")) == "literal column name");
    sheet.cell("B2").value() = std::numeric_limits<std::int64_t>::max();
    workbook.save();
    CHECK_THROWS(add_config(fields, "Wide", read_table(file)));
    sheet.cell("B2").value() = "10";
    sheet.cell("E2").value() = 12;
    workbook.save();
    CHECK_THROWS(add_config(fields, "Wide", read_table(file)));
    workbook.close();
}

TEST_CASE("Formula cells and invalid scalar cells are explicit export errors") {
    using namespace ark::entity_export;
    temporary_directory temp;
    const auto file = temp.path / "formula.xlsx";
    OpenXLSX::XLDocument workbook;
    workbook.create(file.string(), OpenXLSX::XLDoNotOverwrite);
    workbook.workbook().worksheet(1).cell("A1").formula() = "1+1";
    workbook.save();
    workbook.close();
    CHECK_THROWS(read_table(file));
    auto document =
        read_fields(parse_csv("class,index,name,type\nRow,99,id,uint32\nRow,100,value,double\n", "number.csv"));
    CHECK_THROWS(add_config(document, "Row", parse_csv("id,value\n1,NaN\n", "nan.csv")));
    CHECK_THROWS(add_config(document, "Row", parse_csv("id,value\n1,Infinity\n", "inf.csv")));
    CHECK_THROWS(add_config(document, "Row", parse_csv("id,value\n1,1.25junk\n", "junk.csv")));
}

TEST_CASE("Generated names are legal, collision checked and optional for Unicode schemas") {
    using namespace ark::entity_export;
    auto document = read_fields(parse_csv("class,index,name,type\nClass,100,operator,uint32\n", "keywords.csv"));
    CHECK(cpp_bindings(document).find("struct class_fields") != std::string::npos);
    CHECK(cpp_bindings(document).find("operator_ = 100u;") != std::string::npos);
    document.classes[0].name = "角色";
    CHECK_THROWS(cpp_bindings(document));
    temporary_directory temp;
    export_bundle(document, temp.path / "unicode", false, false);
    CHECK(ark::entity_json::load(utf8_string(temp.path / "unicode/meta/entity_schema.json")).classes[0].name == "角色");
}
