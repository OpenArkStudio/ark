// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <cstdlib>
#include <memory>
#include <new>
#include <string>

#include "../network/doctest.h"

#include <ark/kernel/data_list_impl.hpp>
#include <ark/kernel/node_impl.hpp>
#include <ark/kernel/node_manager.hpp>

namespace {

std::size_t fail_remaining{};
bool failure_consumed{};

std::shared_ptr<ark::node_meta> field(const std::shared_ptr<ark::class_meta>& schema, ark::data_type type) {
    auto result = schema->create_data_meta("value", 1);
    result->set_type(type);
    return result;
}

void fail_second_allocation() {
    failure_consumed = false;
    fail_remaining = 2; // Allocate the node first, then fail its string or map allocation.
}

} // namespace

void* operator new(std::size_t size) {
    if (fail_remaining && --fail_remaining == 0)
    {
        failure_consumed = true;
        throw std::bad_alloc{};
    }
    if (auto* pointer = std::malloc(size ? size : 1))
    {
        return pointer;
    }
    throw std::bad_alloc{};
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    try
    { return ::operator new(size); }
    catch (...)
    { return nullptr; }
}

void operator delete(void* pointer) noexcept {
    std::free(pointer);
}

void operator delete(void* pointer, std::size_t) noexcept {
    ::operator delete(pointer);
}

void operator delete(void* pointer, const std::nothrow_t&) noexcept {
    ::operator delete(pointer);
}

TEST_CASE("Node clone string allocation failure destroys the unpublished candidate") {
    const auto schema = std::make_shared<ark::class_meta>("Copy");
    auto meta = field(schema, ark::data_type::dt_string);
    ark::node_string source(meta);
    const std::string text(1024, 's');
    source.set_string(text);
    ark::node_manager manager(schema);
    const auto references = meta.use_count();
#if defined(ARK_NODE_COPY_TEST_HOOKS)
    const auto releases = node_copy_hooks::releases;
#endif
    fail_second_allocation();
    bool threw{};
    try
    { manager.create_data(&source); }
    catch (const std::bad_alloc&)
    { threw = true; }
    CHECK(threw);
    CHECK(failure_consumed);
    CHECK(manager.is_empty());
    CHECK(meta.use_count() == references);
    CHECK(source.get_string() == text);
#if defined(ARK_NODE_COPY_TEST_HOOKS)
    CHECK(node_copy_hooks::releases == releases + 1);
#endif
}

TEST_CASE("Node clone map allocation failure destroys the copied unpublished candidate") {
    const auto schema = std::make_shared<ark::class_meta>("Copy");
    auto meta = field(schema, ark::data_type::dt_int32);
    ark::node_int32 source(meta);
    source.set_int32(42);
    ark::node_manager manager(schema);
    const auto references = meta.use_count();
#if defined(ARK_NODE_COPY_TEST_HOOKS)
    const auto releases = node_copy_hooks::releases;
#endif
    fail_second_allocation();
    bool threw{};
    try
    { manager.create_data(&source); }
    catch (const std::bad_alloc&)
    { threw = true; }
    CHECK(threw);
    CHECK(failure_consumed);
    CHECK(manager.is_empty());
    CHECK(meta.use_count() == references);
    CHECK(source.get_int32() == 42);
#if defined(ARK_NODE_COPY_TEST_HOOKS)
    CHECK(node_copy_hooks::releases == releases + 1);
#endif
}

TEST_CASE("Node creation and lazy creation release candidates if map insertion throws") {
    for (bool lazy : {false, true})
    {
        const auto schema = std::make_shared<ark::class_meta>("Create");
        auto meta = field(schema, ark::data_type::dt_int32);
        ark::node_manager manager(schema);
        const auto references = meta.use_count();
        fail_second_allocation();
        bool threw{};
        try
        {
            if (lazy)
            {
                manager.create_data(std::string("value"));
            }
            else
            {
                manager.create_data(meta);
            }
        }
        catch (const std::bad_alloc&)
        { threw = true; }
        CHECK(threw);
        CHECK(failure_consumed);
        CHECK(manager.is_empty());
        CHECK(meta.use_count() == references);
    }
}

TEST_CASE("Node clone owns an independent string and rejects duplicates without replacing it") {
    const auto schema = std::make_shared<ark::class_meta>("Copy");
    auto meta = field(schema, ark::data_type::dt_string);
    ark::node_string source(meta);
    const std::string text(1024, 's');
    source.set_string(text);
    const auto references = meta.use_count();
    {
        ark::node_manager manager(schema);
        REQUIRE(manager.create_data(&source));
        const auto* copied = manager.get_node(1);
        REQUIRE(copied != nullptr);
        CHECK(copied != &source);
        CHECK(copied->get_string() == text);
        CHECK(copied->get_string().data() != source.get_string().data());
        source.set_string("changed source");
        CHECK(copied->get_string() == text);
#if defined(ARK_NODE_COPY_TEST_HOOKS)
        const auto releases = node_copy_hooks::releases;
#endif
        CHECK_FALSE(manager.create_data(&source));
        CHECK(manager.get_node(1) == copied);
        CHECK(manager.get_string(1) == text);
        CHECK(meta.use_count() == references + 1);
#if defined(ARK_NODE_COPY_TEST_HOOKS)
        CHECK(node_copy_hooks::releases == releases + 1);
#endif
        ark::data_list_impl args;
        args.add_string("");
        args.add_u_int(1);
        manager.init_data(args);
        CHECK(manager.get_string(1).empty());
        CHECK(source.get_string() == "changed source");
    }
    CHECK(meta.use_count() == references);
}

#if defined(ARK_NODE_COPY_TEST_HOOKS)
#ifndef NDEBUG
#error The nullable custom allocation regression requires NDEBUG in every target TU.
#endif
TEST_CASE("Node clone null custom allocation preserves the source and false result") {
    const auto schema = std::make_shared<ark::class_meta>("Copy");
    auto meta = field(schema, ark::data_type::dt_int32);
    ark::node_int32 source(meta);
    source.set_int32(42);
    ark::node_manager manager(schema);
    const auto references = meta.use_count();
    const auto allocations = node_copy_hooks::allocations;
    const auto releases = node_copy_hooks::releases;
    node_copy_hooks::return_null = true;
    CHECK_FALSE(manager.create_data(&source));
    CHECK_FALSE(node_copy_hooks::return_null);
    CHECK(manager.is_empty());
    CHECK(source.get_int32() == 42);
    CHECK(meta.use_count() == references);
    CHECK(node_copy_hooks::allocations == allocations);
    CHECK(node_copy_hooks::releases == releases);
}
#endif
