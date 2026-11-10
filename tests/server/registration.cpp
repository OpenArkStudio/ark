// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <typeinfo>

#include <ark/module_base.hpp>
#include "login/include/login_plugin.hpp"
#include "login/interface/login_net_module.hpp"
#include "login_module_factory.hpp"

namespace {

unsigned created{};
unsigned destroyed{};
unsigned updated{};

class counted_login final : public ark::module_base<ark::login_net_module> {
public:
    counted_login() {
        ++created;
    }

    ~counted_login() override {
        ++destroyed;
    }

    bool update() override {
        ++updated;
        return true;
    }
};

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << std::endl;
        std::_Exit(1);
    }
}

bool rejects(ark::login_plugin& owner) {
    try {
        owner.install();
    } catch (const std::runtime_error&) {
        return true;
    }
    return false;
}

} // namespace

namespace ark::detail {

std::unique_ptr<login_net_module> make_login_net_module() {
    return std::make_unique<counted_login>();
}

} // namespace ark::detail

int main() {
    ark::logger log;
    ark::logger::init(&log);
    log.set_log_func([](spdlog::level::level_enum, const std::string&) {});
    ark::plugin_manager manager;
    ark::login_plugin owner;
    ark::login_plugin competitor;
    owner.set_plugin_manager(&manager);
    competitor.set_plugin_manager(&manager);
    owner.install();
    auto* original = manager.find_module<ark::login_net_module>();
    require(original && created == 1 && destroyed == 0, "Initial factory ownership failed");
    require(rejects(owner) && created == 2 && destroyed == 1, "Duplicate candidate was not destroyed exactly once");
    require(rejects(competitor) && created == 3 && destroyed == 2, "Manager-collision candidate leaked");
    require(manager.find_module<ark::login_net_module>() == original, "Rejected install changed the original module");
    require(manager.update(1) && updated == 0, "Actual Login assembly enabled update for its substitute factory");
    owner.uninstall();
    require(destroyed == 3, "Owned module cleanup failed");
    competitor.install();
    require(created == 4 && destroyed == 3, "Failed install retained stale per-plugin bookkeeping");
    competitor.uninstall();
    require(
        destroyed == 4 && manager.update(2) && updated == 0, "Reinstalled substitute cleanup or update mode failed");
    std::cout << "server registration: rejected candidates, original ownership, retry and disabled updates passed\n";
}
