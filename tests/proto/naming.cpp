// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <string>

#include "../network/doctest.h"
#include <google/protobuf/descriptor.pb.h>

namespace {

bool lower_snake_case(const std::string& name) {
    static const std::regex pattern("[a-z][a-z0-9]*(_[a-z0-9]+)*");
    return std::regex_match(name, pattern);
}

void check_enum(const google::protobuf::EnumDescriptorProto& definition) {
    DOCTEST_INFO(definition.name());
    DOCTEST_CHECK(lower_snake_case(definition.name()));
    for (const auto& value : definition.value())
    {
        DOCTEST_INFO(value.name());
        DOCTEST_CHECK(lower_snake_case(value.name()));
    }
}

struct counts
{
    unsigned messages{};
    unsigned fields{};
    unsigned map_entries{};
    unsigned optional_oneofs{};
};

void check_message(const google::protobuf::DescriptorProto& definition, counts& checked) {
    // Map entry type names belong to protoc, not hand-written schema declarations.
    if (definition.options().map_entry())
    {
        ++checked.map_entries;
        return;
    }
    DOCTEST_INFO(definition.name());
    DOCTEST_CHECK(lower_snake_case(definition.name()));
    ++checked.messages;
    std::set<int> synthetic_oneofs;
    static const std::set<std::string> retired_fields{
        "selfid", "self_serverid", "target_serverid", "gate_serverid", "nreqid", "logid"};
    for (const auto& field : definition.field())
    {
        DOCTEST_INFO(field.name());
        DOCTEST_CHECK(lower_snake_case(field.name()));
        DOCTEST_CHECK_FALSE(retired_fields.contains(field.name()));
        ++checked.fields;
        if (field.proto3_optional())
        {
            synthetic_oneofs.insert(field.oneof_index());
        }
    }
    for (int index = 0; index < definition.oneof_decl_size(); ++index)
    {
        if (synthetic_oneofs.contains(index))
        {
            ++checked.optional_oneofs;
            continue;
        }
        DOCTEST_INFO(definition.oneof_decl(index).name());
        DOCTEST_CHECK(lower_snake_case(definition.oneof_decl(index).name()));
    }
    for (const auto& nested : definition.nested_type())
    {
        check_message(nested, checked);
    }
    for (const auto& enumeration : definition.enum_type())
    {
        check_enum(enumeration);
    }
}

google::protobuf::FileDescriptorSet read_descriptors() {
    std::ifstream input(ARK_PROTO_DESCRIPTOR_SET, std::ios::binary);
    DOCTEST_REQUIRE(input.good());
    google::protobuf::FileDescriptorSet descriptors;
    DOCTEST_REQUIRE(descriptors.ParseFromIstream(&input));
    return descriptors;
}

} // namespace

DOCTEST_TEST_CASE("Protoc descriptors enforce source snake-case names across every project schema") {
    const auto descriptors = read_descriptors();
    DOCTEST_REQUIRE(descriptors.file_size() == ARK_PROTO_SOURCE_COUNT);
    counts checked;
    std::set<std::string> packages;
    for (const auto& file : descriptors.file())
    {
        DOCTEST_INFO(file.name());
        const auto path = std::filesystem::path(file.name());
        DOCTEST_CHECK(path.extension() == ".proto");
        DOCTEST_CHECK(lower_snake_case(path.stem().string()));
        packages.insert(file.package());
        for (const auto& message : file.message_type())
        {
            check_message(message, checked);
        }
        for (const auto& enumeration : file.enum_type())
        {
            check_enum(enumeration);
        }
    }
    DOCTEST_CHECK(packages.contains("ark.protobuf.guest_room"));
    DOCTEST_CHECK(packages.contains("photon_oss"));
    DOCTEST_CHECK(checked.messages > 0);
    DOCTEST_CHECK(checked.fields > 0);
    DOCTEST_CHECK(checked.map_entries > 0);
    DOCTEST_CHECK(checked.optional_oneofs > 0);
}

DOCTEST_TEST_CASE("Renamed ID descriptors keep original field numbers types and JSON spelling") {
    using field_type = google::protobuf::FieldDescriptorProto;

    struct preserved_field
    {
        const char* package;
        const char* message;
        const char* name;
        const char* json_name;
        int number;
        field_type::Type type;
    };

    const preserved_field expected[]{
        {"ark.protobuf", "req_switch_server", "self_id", "selfid", 1, field_type::TYPE_INT64},
        {"ark.protobuf", "req_switch_server", "self_server_id", "selfServerid", 2, field_type::TYPE_INT64},
        {"ark.protobuf", "req_switch_server", "target_server_id", "targetServerid", 3, field_type::TYPE_INT64},
        {"ark.protobuf", "req_switch_server", "gate_server_id", "gateServerid", 4, field_type::TYPE_INT64},
        {"ark.protobuf", "ack_switch_server", "self_id", "selfid", 1, field_type::TYPE_INT64},
        {"ark.protobuf", "ack_switch_server", "self_server_id", "selfServerid", 2, field_type::TYPE_INT64},
        {"ark.protobuf", "ack_switch_server", "target_server_id", "targetServerid", 3, field_type::TYPE_INT64},
        {"ark.protobuf", "ack_switch_server", "gate_server_id", "gateServerid", 4, field_type::TYPE_INT64},
        {"ark.protobuf", "mysql_parameters", "request_id", "nreqid", 6, field_type::TYPE_INT64},
        {"osslog", "login", "log_id", "logid", 1, field_type::TYPE_STRING}};
    const auto descriptors = read_descriptors();
    for (const auto& preserved : expected)
    {
        DOCTEST_INFO(preserved.package << "." << preserved.message << "." << preserved.name);
        const field_type* found = nullptr;
        for (const auto& file : descriptors.file())
        {
            if (file.package() != preserved.package)
            {
                continue;
            }
            for (const auto& message : file.message_type())
            {
                if (message.name() != preserved.message)
                {
                    continue;
                }
                for (const auto& field : message.field())
                {
                    if (field.name() == preserved.name)
                    {
                        found = &field;
                    }
                }
            }
        }
        DOCTEST_REQUIRE(found != nullptr);
        DOCTEST_CHECK(found->number() == preserved.number);
        DOCTEST_CHECK(found->type() == preserved.type);
        DOCTEST_CHECK(found->label() == field_type::LABEL_OPTIONAL);
        DOCTEST_CHECK(found->json_name() == preserved.json_name);
    }
}
