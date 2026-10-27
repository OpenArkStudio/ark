// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <algorithm>
#include <fstream>
#include <limits>
#include <string>
#include <tuple>

#include "../network/doctest.h"

#include <ark/kernel/entity_json.hpp>

namespace {
using namespace ark::entity_json;

std::string sample() {
    return R"({"format":"ark.entity-schema","version":1,"classes":[
{"name":"Player","fields":[],"tables":[],"containers":[]},
{"name":"GuestPlayer","description":"访客属性","fields":[
{"name":"id","index":99,"type":"uint32"},
{"name":"name","index":100,"type":"string","description":"显示名称","mask":["sync_view","sync_self"]},
{"name":"hp","index":104,"type":"uint32","mask":["save","real_time"]}],
"tables":[{"name":"inventory","index":200,"class":"Item","description":"道具表","mask":["save"]}],
"containers":[{"name":"bag","index":201,"class":"Item","description":"子实体","mask":["sync_self"]}]},
{"name":"Item","fields":[{"name":"amount","index":1,"type":"uint32"}],"tables":[],"containers":[]}],
"configs":[{"class":"GuestPlayer","rows":[{"id":1,"name":"Guest","hp":100}]}]})";
}

std::string replaced(std::string input, const std::string& old, const std::string& value) {
    const auto position = input.find(old);
    REQUIRE(position != std::string::npos);
    input.replace(position, old.size(), value);
    return input;
}

void invalid(const std::string& input, const std::string& expected_pointer) {
    try
    {
        static_cast<void>(parse(input, "resource/meta/example.json"));
        FAIL("invalid JSON definition was accepted");
    }
    catch (const error& error)
    {
        CHECK(error.source() == "resource/meta/example.json");
        CHECK(error.pointer() == expected_pointer);
        CHECK(std::string(error.what()).find(expected_pointer) != std::string::npos);
    }
}
} // namespace

TEST_CASE("Entity JSON preserves explicit fields masks tables containers descriptions and canonical export") {
    const auto document = parse(sample());
    REQUIRE(document.classes.size() == 3);
    const auto& guest = document.classes[1];
    CHECK(guest.name == "GuestPlayer");
    CHECK(guest.fields[0].index == 99);
    CHECK(guest.fields[1].index == 100);
    CHECK(guest.fields[1].mask.test(0));
    CHECK(guest.fields[1].mask.test(1));
    CHECK(guest.fields[2].mask.test(2));
    CHECK(guest.fields[2].mask.test(3));
    CHECK(guest.tables[0].class_name == "Item");
    CHECK(guest.containers[0].index == 201);
    CHECK(guest.description == "访客属性");
    CHECK(guest.fields[1].description == "显示名称");
    CHECK(guest.tables[0].description == "道具表");
    CHECK(guest.containers[0].description == "子实体");
    CHECK(std::get<std::uint64_t>(document.configs[0].rows[0].at("hp")) == 100);
    const auto exported = serialize(document);
    CHECK(serialize(parse(exported)) == exported);
    CHECK(exported.find("显示名称") != std::string::npos);
}

TEST_CASE("Entity JSON reports exact source and pointer for schema and row errors") {
    invalid(replaced(sample(), "\"version\":1", "\"version\":2"), "/version");
    invalid(replaced(sample(), "\"version\":1", "\"version\":1.0"), "/version");
    invalid(replaced(sample(), "\"index\":100", "\"index\":0"), "/classes/1/fields/1/index");
    invalid(replaced(sample(), "\"index\":100", "\"index\":99"), "/classes/1/fields/1/index");
    invalid(replaced(sample(), "\"name\":\"hp\"", "\"name\":\"name\""), "/classes/1/fields/2/name");
    invalid(replaced(sample(), "\"type\":\"string\"", "\"type\":\"vector3d\""), "/classes/1/fields/1/type");
    invalid(replaced(sample(), "\"type\":\"string\"", "\"type\":\"wstring\""), "/classes/1/fields/1/type");
    invalid(
        replaced(sample(), "\"sync_view\",\"sync_self\"", "\"sync_view\",\"sync_view\""), "/classes/1/fields/1/mask/1");
    invalid(replaced(sample(), "\"sync_view\",\"sync_self\"", "\"unknown\""), "/classes/1/fields/1/mask/0");
    invalid(replaced(sample(), "\"class\":\"Item\"", "\"class\":\"Missing\""), "/classes/1/tables/0/class");
    invalid(replaced(sample(), "\"hp\":100", "\"hp\":-1"), "/configs/0/rows/0/hp");
    invalid(replaced(sample(), "\"hp\":100", "\"hp\":4294967296"), "/configs/0/rows/0/hp");
    invalid(replaced(sample(), "\"hp\":100", "\"hp\":100.0"), "/configs/0/rows/0/hp");
    invalid(replaced(sample(), "\"hp\":100", "\"hp\":null"), "/configs/0/rows/0/hp");
    invalid(replaced(sample(), "\"hp\":100", "\"hp\":true"), "/configs/0/rows/0/hp");
    invalid(replaced(sample(), ",\"hp\":100", ""), "/configs/0/rows/0/hp");
    invalid(replaced(sample(), "\"hp\":100", "\"hp\":100,\"extra\":0"), "/configs/0/rows/0/extra");
    invalid(replaced(sample(), "\"id\":1,\"name\"", "\"id\":0,\"name\""), "/configs/0/rows/0/id");
    invalid(replaced(sample(), "\"name\":\"name\",\"index\":100",
                "\"name\":\"name\",\"index\":100,\"default\":\"unpromised\""),
        "/classes/1/fields/1/default");
}

TEST_CASE("Entity JSON rejects duplicate object keys including nested paths instead of overwriting") {
    invalid(replaced(sample(), "\"version\":1", "\"version\":1,\"version\":1"), "/version");
    invalid(replaced(sample(), "\"hp\":100", "\"hp\":100,\"hp\":90"), "/configs/0/rows/0/hp");
    invalid(replaced(sample(), "\"name\":\"name\",\"index\":100", "\"name\":\"name\",\"index\":100,\"index\":101"),
        "/classes/1/fields/1/index");
    invalid(replaced(sample(), "{\"id\":1,\"name\":\"Guest\",\"hp\":100}",
                "{\"id\":1,\"name\":\"Guest\",\"hp\":100},{\"id\":1,\"name\":\"Guest\",\"hp\":90}"),
        "/configs/0/rows/1/id");
}

TEST_CASE("Entity JSON typed scalars preserve full integer ranges and reject coercion") {
    for (const auto& [type, good, bad] : std::vector<std::tuple<std::string, std::string, std::string>>{
             {"bool", "true", "1"}, {"int32", "-2147483648", "2147483648"}, {"uint32", "4294967295", "-1"},
             {"int64", "-9223372036854775808", "9223372036854775808"}, {"uint64", "18446744073709551615", "-1"},
             {"float", "1.25", "1e99"}, {"double", "1.2345678901234567", "\"1.0\""}, {"string", "\"内容\"", "false"},
             {"guid", "\"9223372036854775807\"", "\"9223372036854775808\""}})
    {
        auto input = replaced(sample(), "\"name\":\"hp\",\"index\":104,\"type\":\"uint32\"",
            "\"name\":\"hp\",\"index\":104,\"type\":\"" + type + "\"");
        CHECK_NOTHROW(parse(replaced(input, "\"hp\":100", "\"hp\":" + good)));
        invalid(replaced(input, "\"hp\":100", "\"hp\":" + bad), "/configs/0/rows/0/hp");
    }
}

TEST_CASE("Entity JSON plain-model validation shares the canonical parser and cannot lose unsupported state") {
    auto document = parse(sample());
    document.configs[0].rows[0].at("hp") = std::string("100");
    CHECK_THROWS_AS(validate(document, "export.csv"), error);
    document = parse(sample());
    document.classes[1].fields[0].mask.set(15);
    CHECK_THROWS_AS(serialize(document), error);
    CHECK_THROWS_AS(parse("{", "bad.json"), error);
    CHECK_THROWS_AS(load("missing-entity-schema.json"), error);
}
