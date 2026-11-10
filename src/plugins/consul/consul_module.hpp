#pragma once

#include <future>
#include <string>
#include <utility>

#include "ark/module.hpp"
#include "ark/proto/consul.pb.h"

namespace ark {

class consul_module : public module {
public:
    // Set register center url(You should call this function first)
    // virtual void SetRegisterCenter(const std::string& center_ip, const uint16_t center_port) = 0;

    // Register a service to register center
    virtual std::future<std::pair<bool, std::string>> register_service(const consul::service_data& service) = 0;

    // Unregister service form register center
    virtual std::future<std::pair<bool, std::string>> deregister_service(const std::string& service_id) = 0;

    // Get all health service with tag filter
    virtual std::future<std::pair<bool, std::string>> get_health_services(
        const std::string& service_name, const std::string& tag_filter) = 0;

    //virtual bool GetHealthServices(const std::string& service_name, const std::vector<std::string>& tag_filter_list,
    //    consul::service_set& services) = 0;

    // Get/Set value
    virtual std::future<std::pair<bool, std::string>> get_key_value(const std::string& key) = 0;
    virtual std::future<std::pair<bool, std::string>> set_key_value(
        const std::string& key, const std::string& value) = 0;
    virtual std::future<std::pair<bool, std::string>> del_key_value(const std::string& key) = 0;
};

} // namespace ark
