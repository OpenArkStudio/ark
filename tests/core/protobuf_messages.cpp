// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <cstdint>
#include <memory>
#include <string>

#include "../network/doctest.h"
#include <google/protobuf/struct.pb.h>
#include <google/protobuf/util/message_differencer.h>

#include <ark/bus/msg_module.hpp>
#include <ark/net/net_msg.hpp>
#include <ark/proto/guest_room.pb.h>
#include <ark/proto/operational_logs.pb.h>

namespace {

bool has_same_json_fields(const std::string& left, const std::string& right) {
    google::protobuf::Struct left_fields;
    google::protobuf::Struct right_fields;
    return google::protobuf::util::JsonStringToMessage(left, &left_fields).ok() &&
           google::protobuf::util::JsonStringToMessage(right, &right_fields).ok() &&
           google::protobuf::util::MessageDifferencer::Equals(left_fields, right_fields);
}

void release_message(ark::net_msg* message) {
    ark::net_msg::release(message);
}

using message_owner = std::unique_ptr<ark::net_msg, decltype(&release_message)>;

message_owner make_message(const std::string& body, ark::guid_t actor_id) {
    auto header = std::make_unique<ark::ss_msg_header>();
    header->set_message_length(static_cast<std::uint32_t>(body.size()));
    header->set_actor_id(actor_id);
    message_owner message(ark::net_msg::alloc_msg(header.get()), release_message);
    header.release();
    message->copy_data(body.data(), static_cast<std::uint32_t>(body.size()));
    return message;
}

} // namespace

DOCTEST_TEST_CASE("Explicit protobuf parsing preserves payload and actor identity") {
    ark::protobuf::point_3d source;
    source.set_x(1.25);
    source.set_y(-2.5);
    source.set_z(3.75);
    auto message = make_message(source.SerializeAsString(), 77);
    ark::protobuf::point_3d body;
    ark::guid_t actor_id{};
    DOCTEST_REQUIRE(ark::msg_module::decode_protobuf(message.get(), body, actor_id));
    DOCTEST_REQUIRE(actor_id == 77);
    DOCTEST_REQUIRE(body.x() == source.x());
    DOCTEST_REQUIRE(body.y() == source.y());
    DOCTEST_REQUIRE(body.z() == source.z());
}

DOCTEST_TEST_CASE("Explicit protobuf parsing rejects truncated input before assigning actor identity") {
    auto message = make_message(std::string(1, static_cast<char>(0x80)), 77);
    ark::protobuf::point_3d body;
    ark::guid_t actor_id = 99;
    DOCTEST_REQUIRE_FALSE(ark::msg_module::decode_protobuf(message.get(), body, actor_id));
    DOCTEST_REQUIRE(actor_id == 99);
}

DOCTEST_TEST_CASE("Explicit raw-message parsing preserves binary content") {
    const std::string binary("a\0\r\nb", 5);
    auto message = make_message(binary, 123);
    std::string body;
    ark::guid_t actor_id{};
    DOCTEST_REQUIRE(ark::msg_module::read_message_body(message.get(), body, actor_id));
    DOCTEST_REQUIRE(body == binary);
    DOCTEST_REQUIRE(actor_id == 123);
}

DOCTEST_TEST_CASE("Snake-case protocol messages preserve existing binary payloads") {
    // Captured from the previous Point3D and ReqSwitchServer schemas before renaming.
    const std::string point_bytes("\x0d\x00\x00\xa0\x3f\x15\x00\x00\x20\xc0\x1d\x00\x00\x70\x40", 15);
    ark::protobuf::point_3d point;
    DOCTEST_REQUIRE(point.ParseFromString(point_bytes));
    DOCTEST_REQUIRE(point.x() == 1.25f);
    DOCTEST_REQUIRE(point.y() == -2.5f);
    DOCTEST_REQUIRE(point.z() == 3.75f);
    DOCTEST_REQUIRE(point.SerializeAsString() == point_bytes);

    const std::string switch_bytes("\x08\x0b\x10\x0c\x18\x0d\x20\x0e\x28\x0f\x30\x10\x38\x11", 14);
    ark::protobuf::req_switch_server request;
    DOCTEST_REQUIRE(request.ParseFromString(switch_bytes));
    DOCTEST_REQUIRE(request.self_id() == 11);
    DOCTEST_REQUIRE(request.self_server_id() == 12);
    DOCTEST_REQUIRE(request.target_server_id() == 13);
    DOCTEST_REQUIRE(request.gate_server_id() == 14);
    DOCTEST_REQUIRE(request.scene_id() == 15);
    DOCTEST_REQUIRE(request.client_id() == 16);
    DOCTEST_REQUIRE(request.group_id() == 17);
    DOCTEST_REQUIRE(request.SerializeAsString() == switch_bytes);

    const std::string acknowledgement_bytes("\x08\x0b\x10\x0c\x18\x0d\x20\x0e", 8);
    ark::protobuf::ack_switch_server acknowledgement;
    DOCTEST_REQUIRE(acknowledgement.ParseFromString(acknowledgement_bytes));
    DOCTEST_REQUIRE(acknowledgement.self_id() == 11);
    DOCTEST_REQUIRE(acknowledgement.self_server_id() == 12);
    DOCTEST_REQUIRE(acknowledgement.target_server_id() == 13);
    DOCTEST_REQUIRE(acknowledgement.gate_server_id() == 14);
    DOCTEST_REQUIRE(acknowledgement.SerializeAsString() == acknowledgement_bytes);

    const std::string mysql_bytes("\x30\x2a", 2); // Original nreqid tag 6.
    ark::protobuf::mysql_parameters parameters;
    DOCTEST_REQUIRE(parameters.ParseFromString(mysql_bytes));
    DOCTEST_REQUIRE(parameters.request_id() == 42);
    DOCTEST_REQUIRE(parameters.SerializeAsString() == mysql_bytes);

    const std::string log_bytes("\x0a\x03log", 5); // Original logid tag 1.
    osslog::login entry;
    DOCTEST_REQUIRE(entry.ParseFromString(log_bytes));
    DOCTEST_REQUIRE(entry.log_id() == "log");
    DOCTEST_REQUIRE(entry.SerializeAsString() == log_bytes);

    // Batch type names must remain distinct from the generated repeated-field accessors.
    const std::string batch_bytes("\x0a\x02\x08\x4d", 4);
    ark::protobuf::entity_data_node_batch node_batch;
    node_batch.add_multi_entity_data_node_list()->set_entity_id(77);
    DOCTEST_REQUIRE(node_batch.SerializeAsString() == batch_bytes);
    DOCTEST_REQUIRE(node_batch.multi_entity_data_node_list(0).entity_id() == 77);
    ark::protobuf::entity_data_table_batch table_batch;
    table_batch.add_multi_entity_data_table_list()->set_entity_id(77);
    DOCTEST_REQUIRE(table_batch.SerializeAsString() == batch_bytes);
    DOCTEST_REQUIRE(table_batch.multi_entity_data_table_list(0).entity_id() == 77);
}

DOCTEST_TEST_CASE("Consul JSON retains its external case-sensitive field names") {
    const std::string json = R"({"ID":"gate-1","Name":"gate","Address":"127.0.0.1","Port":80,)"
                             R"("Tags":["public"],"Meta":{"zone":"local"},)"
                             R"("Check":{"ID":"health-1","Name":"tcp","Note":"ready",)"
                             R"("Timeout":"1s","Interval":"5s","TCP":"127.0.0.1:80"}})";
    consul::service_data service;
    DOCTEST_REQUIRE(google::protobuf::util::JsonStringToMessage(json, &service).ok());
    DOCTEST_REQUIRE(service.id() == "gate-1");
    DOCTEST_REQUIRE(service.name() == "gate");
    DOCTEST_REQUIRE(service.address() == "127.0.0.1");
    DOCTEST_REQUIRE(service.port() == 80);
    DOCTEST_REQUIRE(service.tags(0) == "public");
    const auto metadata = service.meta().find("zone");
    DOCTEST_REQUIRE(metadata != service.meta().end());
    DOCTEST_REQUIRE(metadata->second == "local");
    DOCTEST_REQUIRE(service.check().id() == "health-1");
    DOCTEST_REQUIRE(service.check().tcp() == "127.0.0.1:80");
    std::string encoded;
    DOCTEST_REQUIRE(google::protobuf::util::MessageToJsonString(service, &encoded).ok());
    DOCTEST_REQUIRE(has_same_json_fields(encoded, json));
}

DOCTEST_TEST_CASE("Renamed protocol fields retain existing JSON property names") {
    const std::string json = R"({"selfid":"11","selfServerid":"12","targetServerid":"13",)"
                             R"("gateServerid":"14","SceneID":"15","clientId":"16","groupID":"17"})";
    ark::protobuf::req_switch_server request;
    DOCTEST_REQUIRE(google::protobuf::util::JsonStringToMessage(json, &request).ok());
    DOCTEST_REQUIRE(request.scene_id() == 15);
    DOCTEST_REQUIRE(request.group_id() == 17);
    std::string encoded;
    DOCTEST_REQUIRE(google::protobuf::util::MessageToJsonString(request, &encoded).ok());
    DOCTEST_REQUIRE(has_same_json_fields(encoded, json));

    ark::protobuf::ack_switch_server acknowledgement;
    const std::string acknowledgement_json =
        R"({"selfid":"11","selfServerid":"12","targetServerid":"13","gateServerid":"14"})";
    DOCTEST_REQUIRE(google::protobuf::util::JsonStringToMessage(acknowledgement_json, &acknowledgement).ok());
    DOCTEST_REQUIRE(acknowledgement.self_id() == 11);
    DOCTEST_REQUIRE(acknowledgement.self_server_id() == 12);
    DOCTEST_REQUIRE(acknowledgement.target_server_id() == 13);
    DOCTEST_REQUIRE(acknowledgement.gate_server_id() == 14);
    encoded.clear();
    DOCTEST_REQUIRE(google::protobuf::util::MessageToJsonString(acknowledgement, &encoded).ok());
    DOCTEST_REQUIRE(has_same_json_fields(encoded, acknowledgement_json));

    ark::protobuf::mysql_parameters parameters;
    DOCTEST_REQUIRE(google::protobuf::util::JsonStringToMessage(R"({"nreqid":"42"})", &parameters).ok());
    DOCTEST_REQUIRE(parameters.request_id() == 42);
    encoded.clear();
    DOCTEST_REQUIRE(google::protobuf::util::MessageToJsonString(parameters, &encoded).ok());
    DOCTEST_REQUIRE(has_same_json_fields(encoded, R"({"nreqid":"42"})"));

    osslog::login entry;
    DOCTEST_REQUIRE(google::protobuf::util::JsonStringToMessage(R"({"logid":"log"})", &entry).ok());
    DOCTEST_REQUIRE(entry.log_id() == "log");
    encoded.clear();
    DOCTEST_REQUIRE(google::protobuf::util::MessageToJsonString(entry, &encoded).ok());
    DOCTEST_REQUIRE(has_same_json_fields(encoded, R"({"logid":"log"})"));

    ark::protobuf::mysql_server_info mysql;
    mysql.set_str_db_name("ark");
    mysql.set_n_server_id(7);
    encoded.clear();
    DOCTEST_REQUIRE(google::protobuf::util::MessageToJsonString(mysql, &encoded).ok());
    DOCTEST_REQUIRE(has_same_json_fields(encoded, R"({"strDBName":"ark","nServerID":"7"})"));
}

DOCTEST_TEST_CASE("Protocol descriptors and textual enum values use the new public names") {
    DOCTEST_REQUIRE(ark::protobuf::point_3d::descriptor()->full_name() == "ark.protobuf.point_3d");
    ark::protobuf::msg_ss_server_report report;
    report.set_logic_status(ark::protobuf::e_st_busy);
    std::string json;
    DOCTEST_REQUIRE(google::protobuf::util::MessageToJsonString(report, &json).ok());
    DOCTEST_REQUIRE(has_same_json_fields(json, R"({"logicStatus":"e_st_busy"})"));
    DOCTEST_REQUIRE(static_cast<int>(ark::protobuf::e_st_busy) == 2);
}

DOCTEST_TEST_CASE("Guest-room snake-case enum symbols preserve numeric wire values") {
    namespace wire = ark::protobuf::guest_room;
    for (const auto kind : {wire::action_unspecified, wire::action_damage, wire::action_skill, wire::action_rest})
    {
        const auto numeric = static_cast<int>(kind);
        wire::action_request request;
        const auto original =
            numeric == 0 ? std::string("\x08\x01", 2) : std::string("\x08\x01\x10", 3) + static_cast<char>(numeric);
        DOCTEST_REQUIRE(request.ParseFromString(original));
        DOCTEST_REQUIRE(request.sequence() == 1);
        DOCTEST_REQUIRE(request.kind() == kind);
        DOCTEST_REQUIRE(request.SerializeAsString() == original);
    }
    wire::action_request action;
    action.set_sequence(1);
    action.set_kind(wire::action_damage);
    std::string encoded;
    DOCTEST_REQUIRE(google::protobuf::util::MessageToJsonString(action, &encoded).ok());
    DOCTEST_REQUIRE(has_same_json_fields(encoded, R"({"sequence":1,"kind":"action_damage"})"));
}
