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

#pragma once

#include <string>

#include "ark/app.hpp"
#include "ark/define.hpp"
#include "ark/math/vector3d.hpp"
#include "ark/module.hpp"
#include "ark/net/net_msg.hpp"
#include "ark/proto.hpp"

namespace ark {

class msg_module : public module {
public:
    virtual bool send_def_cs_msg_by_app_type(
        const app_type app_type, const msg_id_t msg_id, const google::protobuf::Message& msg) = 0;
    virtual bool send_def_ss_msg_by_app_type(
        const app_type app_type, const msg_id_t msg_id, const google::protobuf::Message& msg) = 0;
    virtual bool send_msg_by_app_type(
        const app_type app_type, msg_header* header, const google::protobuf::Message& msg) = 0;
    virtual bool send_msg_by_bus_id(
        bus_id_t bus_id, msg_id_t msg_id, const google::protobuf::Message& msg, const guid_t& guid = null_guid) = 0;
    virtual bool send_to_client(conv_id_t session_id, msg_id_t msg_id, const google::protobuf::Message& msg,
        const guid_t& guid = null_guid) = 0;
    virtual bool send_to_client(conv_id_t session_id, msg_id_t msg_id, const std::string& msg_data,
        bool compressed = false, const guid_t& guid = null_guid) = 0;
    virtual bool send_to_intra(conv_id_t session_id, msg_id_t msg_id, const google::protobuf::Message& msg,
        const guid_t& guid = null_guid) = 0;

    static bool read_message_body(const net_msg* msg, std::string& message_body, guid_t& actor_id) {
        message_body.assign(msg->get_msg_data(), msg->get_msg_length());
        actor_id = msg->get_actor_id();
        return true;
    }

    static bool decode_protobuf(const net_msg* msg, google::protobuf::Message& protobuf_msg, guid_t& actor_id) {
        if (!protobuf_msg.ParseFromString(std::string(msg->get_msg_data(), msg->get_msg_length()))) {
            return false;
        }

        actor_id = msg->get_actor_id();
        return true;
    }

    static vector3d protobuf_to_vec(ark::protobuf::point_3d point) {
        vector3d vector;
        vector.x = point.x();
        vector.y = point.y();
        vector.z = point.z();
        return vector;
    }

    static ark::protobuf::point_3d vec_to_protobuf(vector3d vector) {
        ark::protobuf::point_3d point;
        point.set_x(vector.x);
        point.set_y(vector.y);
        point.set_z(vector.z);
        return point;
    }
};

} // namespace ark
