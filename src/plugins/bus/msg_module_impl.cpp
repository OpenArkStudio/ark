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

#include "ark/bus/bus_module.hpp"
#include "ark/bus/msg_module.hpp"
#include "ark/misc.hpp"
#include "ark/module_base.hpp"
#include "ark/net/net_service_manager_module.hpp"
#include "ark/plugin_manager.hpp"
#include "bus_plugin.hpp"

namespace ark {

class msg_module_impl final : public module_base<msg_module, bus_module, net_service_manager_module>
{
public:
    bool init() override;

    virtual bool send_def_cs_msg_by_app_type(
        const app_type app_type, const msg_id_t msg_id, const google::protobuf::Message& msg) override;
    virtual bool send_def_ss_msg_by_app_type(
        const app_type app_type, const msg_id_t msg_id, const google::protobuf::Message& msg) override;
    virtual bool send_msg_by_app_type(
        const app_type app_type, msg_header* header, const google::protobuf::Message& msg) override;
    bool send_msg_by_bus_id(bus_id_t bus_id, msg_id_t msg_id, const google::protobuf::Message& msg,
        const guid_t& guid = null_guid) override;
    bool send_to_client(conv_id_t session_id, msg_id_t msg_id, const google::protobuf::Message& msg,
        const guid_t& guid = null_guid) override;
    bool send_to_client(conv_id_t session_id, msg_id_t msg_id, const std::string& msg_data, bool compressed = false,
        const guid_t& guid = null_guid) override;
    bool send_to_intra(conv_id_t session_id, msg_id_t msg_id, const google::protobuf::Message& msg,
        const guid_t& guid = null_guid) override;

protected:
    bool send_msg(std::shared_ptr<net>& net_ptr, msg_header* header, const google::protobuf::Message& msg,
        const conv_id_t session_id = 0);
    bool send_msg(
        std::shared_ptr<net>& net_ptr, msg_header* header, const std::string& msg_data, const conv_id_t session_id = 0);

private:
    net_service_manager_module* net_service_manager_module_;
    bus_module* bus_module_;
};

bool msg_module_impl::init() {
    bus_module_ = find_module<bus_module>();
    net_service_manager_module_ = find_module<net_service_manager_module>();

    return true;
}

bool msg_module_impl::send_def_cs_msg_by_app_type(
    const app_type app_type, const msg_id_t msg_id, const google::protobuf::Message& msg) {
    s_msg_header head;
    head.set_message_id(msg_id);
    return send_msg_by_app_type(app_type, &head, msg);
}

bool msg_module_impl::send_def_ss_msg_by_app_type(
    const app_type app_type, const msg_id_t msg_id, const google::protobuf::Message& msg) {
    ss_msg_header head;
    head.set_message_id(msg_id);
    return send_msg_by_app_type(app_type, &head, msg);
}

bool msg_module_impl::send_msg_by_app_type(
    const app_type app_type, msg_header* header, const google::protobuf::Message& msg) {
    /// firstly find net client service
    auto client_service_ptr = net_service_manager_module_->get_client_service(app_type);
    if (client_service_ptr == nullptr)
    {
        ARK_LOG_ERROR("net client service not be found, src_bus={} app_type={} msg_id={} actor_id={}",
            bus_module_->get_self_bus_id(), (uint8_t)app_type, header->message_id(), header->actor_id());
        return false;
    }

    /// todo : if no client service then try net service

    auto busid = bus_module_->get_self_bus_id();
    auto connection_info_ptr = client_service_ptr->get_suitable_connect(misc::to_string(busid));
    if (connection_info_ptr == nullptr)
    {
        ARK_LOG_ERROR("connection info not be found, src_bus={} app_type={} msg_id={} actor_id={}", busid,
            (uint8_t)app_type, header->message_id(), header->actor_id());
        return false;
    }

    header->set_source_bus_id(busid);
    header->set_dest_bus_id(connection_info_ptr->server_bus_id);
    return send_msg(connection_info_ptr->net_client, header, msg);
}

bool msg_module_impl::send_msg_by_bus_id(
    bus_id_t bus_id, msg_id_t msg_id, const google::protobuf::Message& msg, const guid_t& guid /* = NULL_GUID*/) {
    auto src_bus = bus_module_->get_self_bus_id();
    auto net_ptr = net_service_manager_module_->get_net_connection_bus(src_bus, bus_id);

    ss_msg_header header;
    header.set_message_id(msg_id);
    header.set_actor_id(guid);
    header.set_source_bus_id(src_bus);
    header.set_dest_bus_id(bus_id);
    return send_msg(net_ptr, &header, msg);
}

bool msg_module_impl::send_to_client(
    conv_id_t session_id, msg_id_t msg_id, const google::protobuf::Message& msg, const guid_t& guid) {
    std::string msg_data;
    if (!msg.SerializeToString(&msg_data))
    {
        ARK_LOG_ERROR("msg serializing failed, src_bus={} target_bus={} msg_id={} actor_id={}", msg_id, guid);
        return false;
    }

    return send_to_client(session_id, msg_id, msg_data, false, guid);
}

bool msg_module_impl::send_to_client(conv_id_t session_id, msg_id_t msg_id, const std::string& msg_data,
    bool compressed /* = false*/, const guid_t& guid) {
    auto net_service_ptr = net_service_manager_module_->get_inter_net_server();
    if (net_service_ptr == nullptr)
    {
        ARK_LOG_ERROR("net service is null, src_bus={} session id ={} msg_id={} actor_id={}",
            bus_module_->get_self_bus_id(), session_id, msg_id, guid);
        return false;
    }

    s_msg_header header;
    header.set_message_id(msg_id);
    header.set_compressed(compressed);
    auto net_ptr = net_service_ptr->get_net();
    return send_msg(net_ptr, &header, msg_data, session_id);
}

bool msg_module_impl::send_to_intra(
    conv_id_t session_id, msg_id_t msg_id, const google::protobuf::Message& msg, const guid_t& guid) {
    auto net_service_ptr = net_service_manager_module_->get_intra_net_server();
    if (net_service_ptr == nullptr)
    {
        ARK_LOG_ERROR("net service is null, src_bus={} session id ={} msg_id={} actor_id={}",
            bus_module_->get_self_bus_id(), session_id, msg_id, guid);
        return false;
    }

    auto net_ptr = net_service_ptr->get_net();
    ss_msg_header header;
    header.set_message_id(msg_id);
    header.set_actor_id(guid);
    header.set_source_bus_id(bus_module_->get_self_bus_id());
    header.set_dest_bus_id(0);
    return send_msg(net_ptr, &header, msg, session_id);
}

bool msg_module_impl::send_msg(std::shared_ptr<net>& net_ptr, msg_header* header, const google::protobuf::Message& msg,
    const conv_id_t session_id) {
    std::string msg_data;
    if (!msg.SerializeToString(&msg_data))
    {
        ARK_LOG_ERROR("msg serializing failed, src_bus={} target_bus={} msg_id={} actor_id={}", header->source_bus_id(),
            header->dest_bus_id(), header->message_id(), header->actor_id());
        return false;
    }

    return send_msg(net_ptr, header, msg_data, session_id);
}

bool msg_module_impl::send_msg(
    std::shared_ptr<net>& net_ptr, msg_header* header, const std::string& msg_data, const conv_id_t session_id) {
    if (net_ptr == nullptr)
    {
        ARK_LOG_ERROR("net is invalid, src_bus={} target_bus={} msg_id={} actor_id={}", header->source_bus_id(),
            header->dest_bus_id(), header->message_id(), header->actor_id());
        return false;
    }

    auto conv_id = session_id;
    if (conv_id == 0)
    {
        conv_id = net_service_manager_module_->get_session_id(header->dest_bus_id());
    }

    ARK_LOG_DEBUG(
        "[msg][ack] msg id = {} session id = {} msg len = {}", header->message_id(), conv_id, msg_data.length());

    header->set_message_length(msg_data.size());
    return net_ptr->send_msg(header, msg_data.data(), conv_id);
}

namespace detail {

std::unique_ptr<msg_module> make_msg_module() {
    return std::make_unique<msg_module_impl>();
}

} // namespace detail

} // namespace ark
