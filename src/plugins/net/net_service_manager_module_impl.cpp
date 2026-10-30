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

#include <set>
#include <stdexcept>
#include <utility>

#include "ark/bus/bus_module.hpp"
#include "ark/container/map.hpp"
#include "ark/module_base.hpp"
#include "ark/net/net_client_service.hpp"
#include "ark/net/net_client_service_impl.hpp"
#include "ark/net/net_server_service.hpp"
#include "ark/net/net_server_service_impl.hpp"
#include "ark/net/net_service_manager_module.hpp"
#include "ark/plugin_manager.hpp"
#include "ark/utility/timer_module.hpp"
#include "net_plugin.hpp"

namespace ark {

class net_service_manager_module_impl final : public module_base<net_service_manager_module>
{
public:
    bool init() override;
    bool post_init() override;
    bool update() override;
    bool pre_shut() override;
    bool shut() override;

    bool create_server(
        const size_t silent_timeout, const msg_header& cs_req_header, const msg_header& ss_req_header) override;

    std::shared_ptr<net_server_service> get_inter_net_server() override;
    std::shared_ptr<net_server_service> get_intra_net_server() override;

    //int CreateClusterClients(AFHeaderLen head_len = AFHeaderLen::SS_HEAD_LENGTH) override;
    //int CreateClusterClient(AFHeaderLen head_len, bus_id_t bus_id, const std::string& url) override;

    //AFINetClientService* GetNetClientService(const ARK_APP_TYPE& app_type) override;
    //AFINetClientService* GetNetClientServiceByBusID(bus_id_t bus_id) override;

    bool add_net_connection_bus(bus_id_t target_bus_id, std::shared_ptr<net> net_ptr) override;
    bool remove_net_connection_bus(bus_id_t target_bus_id) override;
    std::shared_ptr<net> get_net_connection_bus(bus_id_t src_bus, bus_id_t target_bus) override;

    std::shared_ptr<net_client_service> get_client_service(const app_type app_type) override;

    conv_id_t get_session_id(bus_id_t target_bus_id) override;
    bool remove_session_id(conv_id_t session_id) override;

    bool create_client_service(const bus_addr& bus_addr, proto_type proto, const std::string& ip, uint16_t port,
        std::shared_ptr<const msg_header> head = std::shared_ptr<const msg_header>(new ss_msg_header)) override;

protected:
    //ananas::Future<std::pair<bool, std::string>> RegisterToConsul(bus_id_t bus_id);
    //int DeregisterFromConsul(bus_id_t bus_id);

    void health_check(uint64_t timer_id, const guid_t& entity_id);
    void start_configured_clients();

    bool check_connected_target_server(const bus_addr& bus_addr);
    std::shared_ptr<connection_data> get_target_client_connection(const bus_addr& bus_addr);

    void on_reg_server_call_back(const net_msg* msg, conv_id_t session_id);

private:
    // bus_id -> net_server_service - support multi-server in the same app
    using server_service = shared_object_map<bus_id_t, net_server_service>;
    server_service server_services_;

    server_service intra_services_;

    // target_app_type -> net_client_service
    // the NetClientService have target multi-client info.
    shared_object_map<app_type, net_client_service> client_services_;

    // All net relations, for finding AFINet
    // <self_bus, client_bus> -> AFINet
    shared_object_map<std::pair<bus_id_t, bus_id_t>, net> net_bus_relations_;

    //bus id <--> session id
    std::map<bus_id_t, conv_id_t> bus_session_list_;

    bus_module* bus_module_{};
    timer_module* timer_module_{};
    uint64_t health_timer_id_{};
};

bool net_service_manager_module_impl::init() {
    bus_module_ = find_module<bus_module>();
    timer_module_ = find_module<timer_module>();

    return true;
}

bool net_service_manager_module_impl::post_init() {
    start_configured_clients();
    // start health check timer
    health_timer_id_ = timer_module_->add_forever_timer(
        0, std::chrono::seconds(20), this, &net_service_manager_module_impl::health_check);

    return health_timer_id_ != 0;
}

void net_service_manager_module_impl::start_configured_clients() {
    const bus_addr self(bus_module_->get_self_bus_id());
    std::vector<app_type> types;
    bus_module_->get_target_bus_relations(types);
    std::set<bus_id_t> queued;
    for (const auto type : types)
    {
        std::vector<const proc_config*> processes;
        bus_module_->get_other_proc(type, processes);
        for (const auto* process : processes)
        {
            bus_addr target(process->bus_id);
            if (target.bus_id == self.bus_id || target.channel_id != self.channel_id ||
                target.zone_id != self.zone_id || !queued.insert(target.bus_id).second)
            {
                continue;
            }
            const auto& ep = process->intranet_ep;
            arknet::error_code address_error;
            const auto address = asio::ip::make_address(ep.get_ip(), address_error);
            if ((ep.proto() != proto_type::tcp && ep.proto() != proto_type::udp) || ep.get_ip().empty() ||
                ep.get_ip() == "*" || (!address_error && address.is_unspecified()) || ep.get_port() == 0 ||
                !ep.get_path().empty())
            {
                throw std::runtime_error(
                    "static Bus target " + target.to_string() +
                    ": intranet_endpoint requires a dialable TCP/UDP host and nonzero port without a path; "
                    "wildcard bind addresses cannot be advertised");
            }
            if (!create_client_service(target, ep.proto(), ep.get_ip(), ep.get_port()))
            {
                throw std::runtime_error("could not queue static Bus target " + target.to_string());
            }
        }
    }
}

void net_service_manager_module_impl::health_check(uint64_t timer_id, const guid_t& entity_id) {
    // TODO: health checking, now using etcd just need to watch.

    //    // check if I need to connect other servers from consul center.
    //    std::vector<ARK_APP_TYPE> target_list;
    //    m_pBusModule->GetTargetBusRelations(target_list);
    //
    //    if (target_list.empty())
    //    {
    //        return;
    //    }
    //
    //    auto reg_center = m_pBusModule->GetRegCenter();
    //    for (auto target : target_list)
    //    {
    //        const auto& name = m_pBusModule->GetAppName(target);
    //        if (name.empty())
    //        {
    //            continue;
    //        }
    //
    //        auto ret = m_pConsulModule->GetHealthServices(reg_center.service_name, name);
    //        ret.Then([=](const std::pair<bool, std::string>& response) {
    //            if (!response.first)
    //            {
    //                return;
    //            }
    //
    //            using json = nlohmann::json;
    //
    //            json resp_json = json::parse(response.second);
    //            if (!resp_json.is_array())
    //            {
    //                return;
    //            }
    //
    //            for (json::iterator iter = resp_json.begin(); iter != resp_json.end(); ++iter)
    //            {
    //                auto& health_check_json = iter.value();
    //                auto& service_json = health_check_json["Service"];
    //                if (!service_json.is_object())
    //                {
    //                    continue;
    //                }
    //
    //                consul::service_data service_msg;
    //                google::protobuf::util::JsonParseOptions option;
    //                option.ignore_unknown_fields = true;
    //                auto status = google::protobuf::util::JsonStringToMessage(service_json.dump(), &service_msg, option);
    //                if (!status.ok())
    //                {
    //                    continue;
    //                }
    //
    //                auto find_iter = service_msg.meta().find("busid");
    //                if (find_iter == service_msg.meta().end())
    //                {
    //                    ARK_LOG_ERROR("service={} do not have Meta=busid, please check it.", service_msg.id());
    //                    return;
    //                }
    //
    //                AFBusAddr bus_addr;
    //                bus_addr.FromString(find_iter->second);
    //                // Check if already connected target server
    //                if (CheckConnectedTargetServer(bus_addr))
    //                {
    //                    return;
    //                }
    //
    //                // TODO: NickYang
    //                // Will add a meta that represent the endpoint
    //                CreateClientService(bus_addr, proto_type::tcp, service_msg.address(), service_msg.port());
    //            }
    //        });
    //    }
}

bool net_service_manager_module_impl::update() {
    for (auto iter : server_services_)
    {
        auto server_data_ptr = iter.second;
        if (server_data_ptr != nullptr)
        {
            server_data_ptr->update();
        }
    }

    for (auto iter : intra_services_)
    {
        auto server_data_ptr = iter.second;
        if (server_data_ptr != nullptr)
        {
            server_data_ptr->update();
        }
    }

    for (auto iter : client_services_)
    {
        auto data_ptr = iter.second;
        if (data_ptr != nullptr)
        {
            data_ptr->update();
        }
    }

    return true;
}

bool net_service_manager_module_impl::pre_shut() {
    // All module objects are still alive in the explicit pre-shutdown phase.
    // A provider may already have cleared its wheel; a missing ID is retired.
    const auto timer_id = std::exchange(health_timer_id_, 0);
    if (timer_id != 0 && timer_module_)
    {
        timer_module_->remove_timer(timer_id);
    }
    return true;
}

bool net_service_manager_module_impl::shut() {
    pre_shut();
    for (auto iter : server_services_)
    {
        auto server_data_ptr = iter.second;

        //        // deregister from consul
        //        DeregisterFromConsul(iter.first);
    }

    for (auto iter : client_services_)
    {
        auto data_ptr = iter.second;
    }

    return true;
}

bool net_service_manager_module_impl::create_server(
    const size_t silent_timeout, const msg_header& cs_req_header, const msg_header& ss_req_header) {
    //    ananas::Promise<std::pair<bool, std::string>> promise;

    shared_ptr<const msg_header> cs_req_head(cs_req_header.create());
    shared_ptr<const msg_header> ss_req_head(ss_req_header.create());

    const proc_config& self_proc = bus_module_->get_self_proc();
    auto func = [this, &self_proc, silent_timeout](
                    const endpoint& ep, shared_ptr<const msg_header> header, server_service& server_service) {
        auto server_ptr = std::make_shared<net_server_service_impl>(get_plugin_manager());
        bool ret = server_ptr->start(
            header, self_proc.bus_id, ep, self_proc.thread_num, self_proc.max_connection, silent_timeout);
        if (ret)
        {
            auto server_interface_ptr = static_pointer_cast<net_server_service>(server_ptr);
            server_interface_ptr->reg_reg_server_call_back(
                this, &net_service_manager_module_impl::on_reg_server_call_back);
            server_service.insert(bus_module_->get_self_bus_id(), server_interface_ptr);

            ARK_LOG_INFO("Start net server successful, url = {}", ep.to_string());
            return true;
        }
        else
        {
            ARK_LOG_ERROR("Cannot start server net, url = {}", ep.to_string());
            return false;
        }
    };

    if (!self_proc.server_ep.get_ip().empty())
    {
        // create inter server
        auto result = func(self_proc.server_ep, cs_req_head, server_services_);
        if (!result)
        {
            ARK_LOG_ERROR("Cannot start inter server net, url = {}", self_proc.server_ep.to_string());
            //        promise.SetValue(std::make_pair(false, std::string()));
            //        return promise.GetFuture();
            return false;
        }
    }

    auto& intra_ep = self_proc.intranet_ep;
    if (intra_ep.get_port() == self_proc.server_ep.get_port())
    {
        ARK_LOG_ERROR("Cannot start intra server net, has the same port with server, url = {}", intra_ep.to_string());
        //            promise.SetValue(std::make_pair(false, std::string()));
        //            return promise.GetFuture();
        return false;
    }
    else
    {
        if (!intra_ep.get_ip().empty())
        {
            // create intra server
            auto result = func(intra_ep, ss_req_head, intra_services_);
            if (!result)
            {
                ARK_LOG_ERROR("Cannot start intra server net, url = {}", intra_ep.to_string());
                //            promise.SetValue(std::make_pair(false, std::string()));
                //            return promise.GetFuture();
                return false;
            }
        }
    }

    // Register self to service discovery center(consul)
    //return RegisterToConsul(m_pBusModule->GetSelfBusID());
    return true;
}

std::shared_ptr<net_server_service> net_service_manager_module_impl::get_inter_net_server() {
    return server_services_.find_value(bus_module_->get_self_bus_id());
}

std::shared_ptr<net_server_service> net_service_manager_module_impl::get_intra_net_server() {
    return intra_services_.find_value(bus_module_->get_self_bus_id());
}

bool net_service_manager_module_impl::add_net_connection_bus(bus_id_t target_bus_id, std::shared_ptr<net> net_ptr) {
    if (/*target_bus_id <= 0 || */ net_ptr == nullptr)
    {
        return false;
    }

    int self_bus_id = bus_module_->get_self_bus_id();
    return net_bus_relations_.insert(std::make_pair(self_bus_id, target_bus_id), net_ptr).second;
}

bool net_service_manager_module_impl::remove_net_connection_bus(bus_id_t target_bus_id) {
    //if (target_bus_id <= 0)
    //{
    //    return false;
    //}

    int self_bus_id = bus_module_->get_self_bus_id();
    net_bus_relations_.erase(std::make_pair(self_bus_id, target_bus_id));
    return true;
}

std::shared_ptr<net> net_service_manager_module_impl::get_net_connection_bus(bus_id_t src_bus, bus_id_t target_bus) {
    return net_bus_relations_.find_value(std::make_pair(src_bus, target_bus));
}

std::shared_ptr<net_client_service> net_service_manager_module_impl::get_client_service(const app_type app_type) {
    return client_services_.find_value(app_type);
}

//ananas::Future<std::pair<bool, std::string>> AFCNetServiceManagerModule::RegisterToConsul(bus_id_t bus_id)
//{
//    auto reg_center = m_pBusModule->GetRegCenter();
//    AFBusAddr bus(bus_id);
//    auto bus_name = bus.ToString();
//
//    const std::string app_whole_name = m_pBusModule->GetAppWholeName(bus.bus_id);
//    const std::string app_name = m_pBusModule->GetAppName(static_cast<ARK_APP_TYPE>(bus.app_type));
//
//    const AFProcConfig& self_proc = m_pBusModule->GetSelfProc();
//
//    consul::service_data service;
//    service.set_id(app_whole_name);
//    service.set_name(reg_center.service_name);
//    service.set_address(self_proc.intranet_ep.GetIP());
//    service.set_port(self_proc.intranet_ep.GetPort());
//
//    // multi-tag
//    service.add_tags(app_name);
//    service.add_tags(bus_name);
//
//    // multi-meta
//    service.mutable_meta()->insert({"busid", bus_name});
//
//    // health check
//    auto check = service.mutable_check();
//    check->set_id(app_whole_name + "_check");
//    check->set_name(app_whole_name);
//    check->set_tcp(self_proc.intranet_ep.GetIP() + ":" + std::to_string(self_proc.intranet_ep.GetPort()));
//    check->set_interval(reg_center.check_interval);
//    check->set_timeout(reg_center.check_timeout);
//
//    return m_pConsulModule->RegisterService(service);
//}
//
//int AFCNetServiceManagerModule::DeregisterFromConsul(bus_id_t bus_id)
//{
//    auto ret = m_pConsulModule->DeregisterService(m_pBusModule->GetAppWholeName(bus_id));
//    ret.Then([=](const std::pair<bool, std::string>& response) {
//        if (!response.first)
//        {
//            ARK_LOG_ERROR(
//                "DeregisterFromConsul failed, busid={}, error={}", AFBusAddr(bus_id).ToString(), response.second);
//        }
//    });
//
//    return 0;
//}

bool net_service_manager_module_impl::create_client_service(const bus_addr& bus_addr, proto_type proto,
    const std::string& ip, uint16_t port, std::shared_ptr<const msg_header> head) {
    app_type app_type = static_cast<ark::app_type>(bus_addr.app_type);
    auto client_ptr = client_services_.find_value(app_type);
    if (client_ptr == nullptr)
    {
        client_ptr = std::make_shared<net_client_service_impl>(get_plugin_manager());
        client_services_.insert(app_type, client_ptr);
    }

    endpoint target_ep;
    target_ep.set_protocol(proto);
    target_ep.set_ip(ip);
    target_ep.set_port(port);

    bool ret = client_ptr->start_client(head, bus_addr.bus_id, target_ep);
    if (!ret)
    {
        ARK_LOG_ERROR("start net client failed, self_bus_id={} target_url={}", bus_module_->get_self_bus_name(),
            target_ep.to_string());
        return ret;
    }

    ARK_LOG_INFO("start net client successful, self_bus_id={} target_url={}", bus_module_->get_self_bus_name(),
        target_ep.to_string());
    return true;
}

conv_id_t net_service_manager_module_impl::get_session_id(bus_id_t target_bus_id) {
    auto iter = bus_session_list_.find(target_bus_id);
    if (iter == bus_session_list_.end())
    {
        return 0;
    }

    return iter->second;
}

bool net_service_manager_module_impl::remove_session_id(conv_id_t session_id) {
    for (auto& iter : bus_session_list_)
    {
        if (iter.second == session_id)
        {
            bus_session_list_.erase(iter.first);
            break;
        }
    }

    return true;
}

bool net_service_manager_module_impl::check_connected_target_server(const bus_addr& bus_addr) {
    return (get_target_client_connection(bus_addr) != nullptr);
}

std::shared_ptr<connection_data> net_service_manager_module_impl::get_target_client_connection(
    const bus_addr& bus_addr) {
    auto client_service = client_services_.find_value(static_cast<ark::app_type>(bus_addr.app_type));
    if (client_service == nullptr)
    {
        return nullptr;
    }

    return client_service->get_connection_info(bus_addr.bus_id);
}

void net_service_manager_module_impl::on_reg_server_call_back(const net_msg* msg, conv_id_t session_id) {
    // msg->GetSrcBus() means client msg bus id
    auto iter = bus_session_list_.find(msg->get_src_bus());
    if (iter == bus_session_list_.end())
    {
        bus_session_list_.insert(std::make_pair(msg->get_src_bus(), session_id));
    }
    else
    {
        iter->second = session_id;
    }
}

namespace detail {

std::unique_ptr<net_service_manager_module> make_net_service_manager_module() {
    return std::make_unique<net_service_manager_module_impl>();
}

} // namespace detail

} // namespace ark
