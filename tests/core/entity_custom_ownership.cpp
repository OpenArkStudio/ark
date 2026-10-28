// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <array>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <new>
#include <string>

#include "../network/doctest.h"

#include <ark/kernel/data_impl.hpp>
#include <ark/kernel/data_list_impl.hpp>
#include <ark/kernel/entity_impl.hpp>

namespace {

std::array<void*, 16> live{};
unsigned allocations{}, releases{}, hook_deletes{}, injected_failures{};
bool observe{}, return_null{}, fail_after_candidate{};
bool fail_next_heap{};

void record_release(void* value) noexcept {
    for (auto& entry : live)
    {
        if (value != nullptr && entry == value)
        {
            entry = nullptr;
            ++releases;
            break;
        }
    }
}

void reset() {
    for (auto* entry : live)
    {
        REQUIRE(entry == nullptr);
    }
    allocations = releases = hook_deletes = injected_failures = 0;
    observe = return_null = fail_after_candidate = fail_next_heap = false;
}

void release_unpublished() {
    for (auto* entry : live)
    {
        // Recover a baseline leak after recording the failing ownership checks.
        delete static_cast<ark::data*>(entry);
    }
}

using add_function = bool (*)(ark::entity&, const std::string&);
constexpr std::array<add_function, 7> adders{
    [](ark::entity& object, const std::string& name) { return object.add_custom_bool(name, true); },
    [](ark::entity& object, const std::string& name) { return object.add_custom_int32(name, -17); },
    [](ark::entity& object, const std::string& name) { return object.add_custom_u_int32(name, 4000000000U); },
    [](ark::entity& object, const std::string& name) { return object.add_custom_int64(name, 4567890123LL); },
    [](ark::entity& object, const std::string& name) { return object.add_custom_float(name, 1.25F); },
    [](ark::entity& object, const std::string& name) { return object.add_custom_double(name, 2.5); },
    [](ark::entity& object, const std::string& name) { return object.add_custom_string(name, "str"); }};

std::shared_ptr<ark::class_meta> schema() {
    return std::make_shared<ark::class_meta>("CustomOwnership");
}

} // namespace

void* operator new(std::size_t size) {
    if (fail_next_heap)
    {
        fail_next_heap = false;
        ++injected_failures;
        throw std::bad_alloc{};
    }
    if (auto* value = std::malloc(size ? size : 1))
    {
        return value;
    }
    throw std::bad_alloc{};
}

void* operator new[](std::size_t size) {
    return ::operator new(size);
}

void operator delete(void* value) noexcept {
    record_release(value);
    std::free(value);
}

void operator delete[](void* value) noexcept {
    ::operator delete(value);
}

void operator delete(void* value, std::size_t) noexcept {
    ::operator delete(value);
}

void operator delete[](void* value, std::size_t) noexcept {
    ::operator delete(value);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    if (observe && return_null)
    {
        return_null = false;
        ++injected_failures;
        return nullptr;
    }
    void* value{};
    try
    { value = ::operator new(size); }
    catch (...)
    { return nullptr; }
    if (observe)
    {
        for (auto& entry : live)
        {
            if (entry == nullptr)
            {
                entry = value;
                ++allocations;
                fail_next_heap = fail_after_candidate;
                return value;
            }
        }
        std::abort();
    }
    return value;
}

void operator delete(void* value, const std::nothrow_t&) noexcept {
    ::operator delete(value);
}

void entity_custom_test::record_delete_hook(const void* value) noexcept {
    for (const auto* entry : live)
    {
        if (entry == value)
        {
            ++hook_deletes;
            return;
        }
    }
}

TEST_CASE("Entity custom scalar insertion exceptions release unpublished candidates through the delete hook") {
    for (std::size_t index = 0; index < adders.size(); ++index)
    {
        CAPTURE(index);
        reset();
        ark::entity_impl object(schema(), 1, 0, 1, 0, ark::data_list_impl{});
        const std::string name = "custom";
        observe = fail_after_candidate = true;
        CHECK_THROWS_AS(adders[index](object, name), std::bad_alloc);
        observe = fail_after_candidate = false;
        CHECK_FALSE(fail_next_heap);
        CHECK(injected_failures == 1);
        CHECK(allocations == 1);
        CHECK(releases == 1);
        CHECK(hook_deletes == 1);
        CHECK_FALSE(object.find_custom_data(name));
        release_unpublished();
        CHECK(adders[index](object, name));
        CHECK(object.remove_custom_data(name));
    }
}

TEST_CASE("Entity custom string value allocation exceptions release the unpublished candidate") {
    reset();
    ark::entity_impl object(schema(), 1, 0, 1, 0, ark::data_list_impl{});
    const std::string name = "string";
    const std::string value(1024, 'x');
    observe = fail_after_candidate = true;
    CHECK_THROWS_AS(object.add_custom_string(name, value), std::bad_alloc);
    observe = fail_after_candidate = false;
    CHECK(injected_failures == 1);
    CHECK(allocations == 1);
    CHECK(releases == 1);
    CHECK(hook_deletes == 1);
    CHECK_FALSE(object.find_custom_data(name));
    release_unpublished();
    REQUIRE(object.add_custom_string(name, value));
    CHECK(std::string(object.get_custom_string(name)) == value);
}

TEST_CASE("Entity custom candidates preserve null duplicate and successful owning map behavior") {
    for (const auto add : adders)
    {
        reset();
        {
            ark::entity_impl object(schema(), 1, 0, 1, 0, ark::data_list_impl{});
            const std::string name = "custom";
            observe = return_null = true;
            CHECK_FALSE(add(object, name));
            CHECK_FALSE(object.find_custom_data(name));
            CHECK(injected_failures == 1);
            CHECK(allocations == 0);
            CHECK(releases == 0);
            REQUIRE(add(object, name));
            CHECK(allocations == 1);
            CHECK(releases == 0);
            CHECK_FALSE(add(object, name));
            observe = false;
            CHECK(allocations == 1);
            CHECK(hook_deletes == 0);
            CHECK(object.find_custom_data(name));
        }
        CHECK(releases == 1);
        // Successful insertion retains the owning map's existing delete path.
        CHECK(hook_deletes == 0);
    }
}
