// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <memory>

#include <ark/etcd_client/etcd_client_plugin.hpp>
#include <ark/module_base.hpp>
#include "etcd_client_factory.hpp"

namespace {

unsigned created{};
unsigned destroyed{};
unsigned updated{};

class entry_etcd_client final : public ark::module_base<ark::etcd_client_module>
{
public:
    entry_etcd_client() {
        ++created;
    }

    ~entry_etcd_client() override {
        ++destroyed;
    }

    bool init() override {
        return ark::etcd_client_plugin::get_plugin_conf() == "conf/entry-probe.toml";
    }

    bool update() override {
        ++updated;
        return true;
    }

    std::shared_ptr<ark::etcd_client> create_etcd_client(std::vector<std::string>, int, int) override {
        return {};
    }
};

} // namespace

namespace ark::detail {

std::unique_ptr<etcd_client_module> make_etcd_client_module() {
    return std::make_unique<entry_etcd_client>();
}

} // namespace ark::detail

ARK_EXPORT_FUNC unsigned etcd_client_entry_events(unsigned event) {
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
