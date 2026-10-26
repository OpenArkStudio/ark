// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <stdexcept>
#include <type_traits>
#include <typeinfo>

#include "../network/doctest.h"

#include <ark/kafka/kafka_module_impl.hpp>
#include <ark/kafka/kafka_plugin.hpp>
#include "kafka_factory.hpp"

#if __has_include(<ark/kafka/kafka_factory.hpp>)
#error "Kafka public mappings must exclude the private factory"
#endif

static_assert(std::is_base_of_v<ark::module_base<ark::kafka_module>, ark::kafka_module_impl>);
static_assert(std::is_base_of_v<ark::plugin_base<ark::kafka_plugin>, ark::kafka_plugin>);
static_assert(std::is_base_of_v<RdKafka::DeliveryReportCb, ark::kafka_module_impl>);
static_assert(std::is_base_of_v<RdKafka::EventCb, ark::kafka_module_impl>);
static_assert(!std::is_same_v<decltype(&ark::kafka_module_impl::update), decltype(&ark::module::update)>);
static_assert(std::is_same_v<decltype(ark::detail::make_kafka_module()), std::unique_ptr<ark::kafka_module>>);

TEST_CASE("Kafka real factory retains its public implementation and unpublished metadata") {
    auto candidate = ark::detail::make_kafka_module();
    REQUIRE(candidate != nullptr);
    const auto* instance = candidate.get();
    CHECK(typeid(*instance) == typeid(ark::kafka_module_impl));
    CHECK(candidate->get_plugin_manager() == nullptr);
    CHECK(candidate->get_name().empty());
}

TEST_CASE("Kafka assembly retains the interface key, enabled updates and explicit ownership") {
    ark::plugin_manager manager;
    ark::kafka_plugin owner;
    owner.set_plugin_manager(&manager);
    CHECK(owner.get_plugin_version() == 0);
    CHECK(owner.get_plugin_name() == typeid(ark::kafka_plugin).name());
    owner.set_plugin_conf("kafka-construction.toml");
    CHECK(ark::kafka_plugin::get_plugin_conf() == "kafka-construction.toml");
    owner.install();
    auto* instance = manager.find_module<ark::kafka_module>();
    REQUIRE(instance != nullptr);
    CHECK(typeid(*instance) == typeid(ark::kafka_module_impl));
    CHECK(instance->get_plugin_manager() == &manager);
    CHECK(instance->get_name() == typeid(ark::kafka_module).name());
    // Duplicate update insertion fails only when assembly already selected updates.
    CHECK_FALSE(manager.add_update_module(instance));
    CHECK(instance->init());
    CHECK(manager.update(1));
    // Empty parameters avoid creating a producer or connecting to any broker.
    CHECK_FALSE(instance->init_producer("", "construction"));
    CHECK_FALSE(instance->init_producer("unused", ""));
    CHECK_THROWS_AS(owner.install(), std::runtime_error);
    CHECK(manager.find_module<ark::kafka_module>() == instance);
    owner.uninstall();
    // Reinstallation proves the public registration key and ownership were released.
    CHECK_NOTHROW(owner.install());
    CHECK_NOTHROW(owner.uninstall());
    CHECK(manager.update(1));
    CHECK_THROWS_AS(owner.uninstall(), std::runtime_error);
}
