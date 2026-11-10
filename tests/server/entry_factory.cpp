// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <memory>

#include <ark/module_base.hpp>
#include "login_module_factory.hpp"

namespace {

unsigned created{};
unsigned destroyed{};
unsigned updated{};

class entry_login final : public ark::module_base<ark::login_net_module> {
public:
    entry_login() {
        ++created;
    }

    ~entry_login() override {
        ++destroyed;
    }

    bool update() override {
        ++updated;
        return true;
    }
};

} // namespace

namespace ark::detail {

std::unique_ptr<login_net_module> make_login_net_module() {
    return std::make_unique<entry_login>();
}

} // namespace ark::detail

ARK_EXPORT_FUNC unsigned server_entry_events(unsigned event) {
    switch (event) {
    case 0:
        return created;
    case 1:
        return destroyed;
    default:
        return updated;
    }
}
