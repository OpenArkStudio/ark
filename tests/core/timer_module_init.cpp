// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <cstddef>
#include <new>

#include "../network/doctest.h"

#include <ark/plugin_manager.hpp>
#include <ark/time/timer.hpp>
#include "utility/utility_plugin.hpp"

#ifndef NDEBUG
#error This regression must exercise the production module with assertions disabled.
#endif

namespace {

std::size_t nothrow_calls{};
std::size_t fail_remaining{};

class receiver
{
public:
    void on_timer(std::uint64_t, const ark::guid_t&) {
    }
};

} // namespace

// Keep default ARK_NEW definitions in both this TU and the production module.
// Successful allocations delegate to ordinary new and use its matching delete.
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    ++nothrow_calls;
    if (fail_remaining && --fail_remaining == 0)
    {
        return nullptr;
    }
    try
    { return ::operator new(size); }
    catch (...)
    { return nullptr; }
}

void operator delete(void* value, const std::nothrow_t&) noexcept {
    ::operator delete(value);
}

TEST_CASE("Production timer module rejects null allocations without continuing initialization") {
    for (const std::size_t ordinal : {1u, 18u, ark::free_timer_size + 1, ark::free_timer_size + ark::tv_num})
    {
        ark::plugin_manager manager;
        auto timer = ark::detail::make_timer_module();
        timer->set_plugin_manager(&manager);
        const auto initial_calls = nothrow_calls;
        fail_remaining = ordinal;
        const auto initialized = timer->init();
        CHECK_FALSE(initialized);
        CHECK(fail_remaining == 0);
        CHECK(nothrow_calls - initial_calls == ordinal);
        // Failed startup proceeds only to destruction, without later lifecycle calls.
    }
}

TEST_CASE("Production timer module initializes normally and closes through its public interface") {
    ark::plugin_manager manager;
    auto timer = ark::detail::make_timer_module();
    timer->set_plugin_manager(&manager);
    REQUIRE(timer->init());
    receiver target;
    const auto id =
        timer->add_forever_timer(ark::null_guid, std::chrono::milliseconds(10), &target, &receiver::on_timer);
    REQUIRE(id != 0);
    CHECK(timer->remove_timer(id));
    CHECK_FALSE(timer->remove_timer(id));
    CHECK(timer->update());
    CHECK(timer->pre_shut());
    CHECK(timer->pre_shut());
}
