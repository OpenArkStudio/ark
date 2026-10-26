// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <memory>

#include <ark/module_base.hpp>
#include <ark/osssdk/oss_sdk_plugin.hpp>
#include "oss_sdk_factory.hpp"

namespace {

unsigned created{};
unsigned destroyed{};
unsigned updated{};

class entry_oss_sdk final : public ark::module_base<ark::oss_sdk_module>
{
public:
    entry_oss_sdk() {
        ++created;
    }

    ~entry_oss_sdk() override {
        ++destroyed;
    }

    bool init() override {
        return ark::oss_sdk_plugin::get_plugin_conf() == "conf/entry-probe.toml";
    }

    bool update() override {
        ++updated;
        return true;
    }
};

} // namespace

namespace ark::detail {

std::unique_ptr<oss_sdk_module> make_oss_sdk_module() {
    return std::make_unique<entry_oss_sdk>();
}

} // namespace ark::detail

ARK_EXPORT_FUNC unsigned oss_sdk_entry_events(unsigned event) {
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
