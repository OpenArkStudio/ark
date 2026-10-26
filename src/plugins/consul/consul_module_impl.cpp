#include <google/protobuf/util/json_util.h>

#include "../detail/plugin_configuration.hpp"
#include "ark/consul/consul_module_impl.hpp"
#include "ark/consul/consul_plugin.hpp"
#include "consul_factory.hpp"

namespace ark {

std::unique_ptr<consul_module> detail::make_consul_module() {
    return std::make_unique<consul_module_impl>();
}

namespace {

std::future<std::pair<bool, std::string>> failed_request() {
    std::promise<std::pair<bool, std::string>> promise;
    promise.set_value({false, {}});
    return promise.get_future();
}

} // namespace

static const std::string register_api("/v1/agent/service/register");
static const std::string deregister_api("/v1/agent/service/deregister/");
static const std::string kv_api("/v1/kv/");
static const std::string health_check_api("/v1/health/service/");

bool consul_module_impl::init() {
    http_client_module_ = find_module<http_client_module>();
    return http_client_module_ != nullptr && load_config();
}

// void AFCConsulModule::SetRegisterCenter(const std::string& center_ip, const uint16_t center_port)
// {
//     consul_ip_ = center_ip;
//     consul_port_ = center_port;
// }

bool consul_module_impl::load_config() {
    const auto config = plugin_config_detail::load_consul(consul_plugin::get_plugin_conf());
    consul_ip_ = config.host;
    consul_port_ = config.port;
    return true;
}

std::future<std::pair<bool, std::string>> consul_module_impl::register_service(const consul::service_data& service) {
    std::string body;
    if (!google::protobuf::util::MessageToJsonString(service, &body).ok())
    {
        return failed_request();
    }
    const std::string content_type = "application/json";
    return http_client_module_->async_request(
        arknet::http::verb::put, consul_ip_, consul_port_, register_api, nullptr, nullptr, &body, &content_type);
}

std::future<std::pair<bool, std::string>> consul_module_impl::deregister_service(const std::string& service_id) {
    if (service_id.empty())
    {
        return failed_request();
    }

    auto url = deregister_api + service_id;
    return http_client_module_->async_request(arknet::http::verb::put, consul_ip_, consul_port_, url);
}

std::future<std::pair<bool, std::string>> consul_module_impl::get_key_value(const std::string& key) {
    if (key.empty())
    {
        return failed_request();
    }

    std::string url = kv_api + key;
    return http_client_module_->async_request(arknet::http::verb::get, consul_ip_, consul_port_, url);
}

std::future<std::pair<bool, std::string>> consul_module_impl::set_key_value(
    const std::string& key, const std::string& value) {
    if (key.empty())
    {
        return failed_request();
    }

    return http_client_module_->async_request(
        arknet::http::verb::put, consul_ip_, consul_port_, kv_api + key, nullptr, nullptr, &value);
}

std::future<std::pair<bool, std::string>> consul_module_impl::del_key_value(const std::string& key) {
    if (key.empty())
    {
        return failed_request();
    }

    std::string url = kv_api + key;
    return http_client_module_->async_request(arknet::http::verb::delete_, consul_ip_, consul_port_, url);
}

std::future<std::pair<bool, std::string>> consul_module_impl::get_health_services(
    const std::string& service_name, const std::string& tag_filter) {
    if (service_name.empty())
    {
        return failed_request();
    }

    std::string url = health_check_api + service_name;
    std::map<std::string, std::string> params;
    params.insert(std::make_pair("tag", tag_filter));
    params.insert(std::make_pair("passing", "1"));

    return http_client_module_->async_request(arknet::http::verb::get, consul_ip_, consul_port_, url, &params);
}

//bool AFCConsulModule::GetHealthServices(
//    const std::string& service_name, const std::vector<std::string>& tag_filter_list, consul::service_set& services)
//{
//    for (auto tag : tag_filter_list)
//    {
//        auto check_future = HealthCheck(service_name, tag);
//    }
//
//    // TODO:
//    // when all
//
//    return true;
//}

} // namespace ark
