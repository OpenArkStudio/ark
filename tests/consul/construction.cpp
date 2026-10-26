// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <stdexcept>
#include <type_traits>
#include <typeinfo>

#include "../network/doctest.h"

#include <ark/consul/consul_module_impl.hpp>
#include <ark/consul/consul_plugin.hpp>
#include "consul_factory.hpp"

#if __has_include(<ark/consul/consul_factory.hpp>)
#error "Consul public mappings must exclude the private factory"
#endif

static_assert(std::is_base_of_v<ark::module_base<ark::consul_module>, ark::consul_module_impl>);
static_assert(std::is_base_of_v<ark::plugin_base<ark::consul_plugin>, ark::consul_plugin>);
static_assert(std::is_same_v<decltype(&ark::consul_module_impl::update), decltype(&ark::module::update)>);

TEST_CASE("Consul real factory retains its public implementation and unpublished metadata") {
    auto candidate = ark::detail::make_consul_module();
    REQUIRE(candidate != nullptr);
    CHECK(typeid(*candidate) == typeid(ark::consul_module_impl));
    CHECK(candidate->get_plugin_manager() == nullptr);
    CHECK(candidate->get_name().empty());
}

TEST_CASE("Consul assembly retains the interface key, disabled updates and explicit ownership") {
    ark::plugin_manager manager;
    ark::consul_plugin owner;
    owner.set_plugin_manager(&manager);
    CHECK(owner.get_plugin_version() == 0);
    CHECK(owner.get_plugin_name() == typeid(ark::consul_plugin).name());
    owner.set_plugin_conf("consul-construction.toml");
    CHECK(ark::consul_plugin::get_plugin_conf() == "consul-construction.toml");
    owner.install();
    auto* instance = manager.find_module<ark::consul_module>();
    REQUIRE(instance != nullptr);
    CHECK(typeid(*instance) == typeid(ark::consul_module_impl));
    CHECK(instance->get_plugin_manager() == &manager);
    CHECK(instance->get_name() == typeid(ark::consul_module).name());
    // Insertion succeeds only when assembly left the module out of the update set.
    CHECK(manager.add_update_module(instance));
    manager.remove_update_module(instance->get_name());
    CHECK_THROWS_AS(owner.install(), std::runtime_error);
    CHECK(manager.find_module<ark::consul_module>() == instance);
    owner.uninstall();
    // Reinstallation proves the public registration key and ownership were released.
    CHECK_NOTHROW(owner.install());
    CHECK_NOTHROW(owner.uninstall());
    CHECK(manager.update(1));
    CHECK_THROWS_AS(owner.uninstall(), std::runtime_error);
}
