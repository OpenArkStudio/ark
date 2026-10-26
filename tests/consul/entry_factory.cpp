// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <memory>

#include <ark/consul/consul_plugin.hpp>
#include <ark/module_base.hpp>
#include "consul_factory.hpp"

namespace {

unsigned created{};
unsigned destroyed{};
unsigned updated{};

class entry_consul final : public ark::module_base<ark::consul_module>
{
public:
    entry_consul() {
        ++created;
    }

    ~entry_consul() override {
        ++destroyed;
    }

    bool init() override {
        return ark::consul_plugin::get_plugin_conf() == "conf/entry-probe.toml";
    }

    bool update() override {
        ++updated;
        return true;
    }

    std::future<std::pair<bool, std::string>> register_service(const ::consul::service_data&) override {
        return {};
    }

    std::future<std::pair<bool, std::string>> deregister_service(const std::string&) override {
        return {};
    }

    std::future<std::pair<bool, std::string>> get_health_services(const std::string&, const std::string&) override {
        return {};
    }

    std::future<std::pair<bool, std::string>> get_key_value(const std::string&) override {
        return {};
    }

    std::future<std::pair<bool, std::string>> set_key_value(const std::string&, const std::string&) override {
        return {};
    }

    std::future<std::pair<bool, std::string>> del_key_value(const std::string&) override {
        return {};
    }
};

} // namespace

namespace ark::detail {

std::unique_ptr<consul_module> make_consul_module() {
    return std::make_unique<entry_consul>();
}

} // namespace ark::detail

ARK_EXPORT_FUNC unsigned consul_entry_events(unsigned event) {
    switch (event)
    {
    case 0:
        return created;
    case 1:
        return destroyed;
    default:
        return updated;
    }
}
