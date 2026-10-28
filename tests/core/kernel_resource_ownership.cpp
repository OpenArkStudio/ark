// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <memory>

#include "../network/doctest.h"

#include "ark/kernel/class_meta_module_impl.hpp"
#include "ark/kernel/config_module_impl.hpp"
#include "kernel_metadata_files.hpp"

TEST_CASE("Kernel XML configuration releases temporary ID nodes and preserves published records") {
    ark::logger logger;
    ark::logger::init(&logger);
    logger.set_log_func([](spdlog::level::level_enum, const std::string&) {});

    bool duplicate_id = false;
    SUBCASE("successful row") {
    }
#ifdef NDEBUG
    // The legacy duplicate-ID guard returns false only when assertions are disabled.
    SUBCASE("duplicate ID rejects the next row") {
        duplicate_id = true;
    }
#endif
    ark::test::metadata_files files(false, duplicate_id);
    ark::plugin_manager manager;
    ark::class_meta_module_impl metadata;
    ark::config_module_impl config;
    metadata.set_plugin_manager(&manager);
    config.set_plugin_manager(&manager);
    REQUIRE(manager.add_module(GET_CLASS_NAME(ark::class_meta_module), &metadata));
    REQUIRE(metadata.init());
    REQUIRE(config.init());
    std::weak_ptr<ark::node_meta> id_meta;
    {
        const auto schema = metadata.find_static_meta("attribute");
        REQUIRE(schema != nullptr);
        id_meta = schema->find_data_meta(schema->get_index("id"));
    }
    REQUIRE_FALSE(id_meta.expired());
    const auto schema_owners = id_meta.use_count();
    CHECK(config.post_init() == !duplicate_id);
    auto published = config.find_static_entity(42);
    REQUIRE(published != nullptr);
    CHECK(published->get_int32("level") == 7);
    // Only the published record's ID node may retain this field's metadata.
    CHECK(id_meta.use_count() == schema_owners + 1);
    CHECK(config.shut());
    CHECK(config.shut());
#ifdef NDEBUG
    // Querying a cleared manager retains its existing assertion in Debug.
    CHECK(config.find_static_entity(42) == nullptr);
#endif
    CHECK(id_meta.use_count() == schema_owners + 1);
    CHECK(metadata.shut());
    CHECK(metadata.shut());
    CHECK(published->get_int32("level") == 7);
    CHECK_FALSE(id_meta.expired());
    published.reset();
    CHECK(id_meta.expired());
    ark::logger::set_instance(nullptr);
}
