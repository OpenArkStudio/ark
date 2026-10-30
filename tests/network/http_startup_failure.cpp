// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <new>
#include <string>
#include <utility>

#include "doctest.h"

// Compile the real implementation here so the allocation probe can observe its
// private reservation set without adding a production injection interface.
#include "../../src/plugins/http/http_server_module_impl.cpp"

namespace {

const std::string* observed_endpoint{};
std::size_t failed_size{};
bool fail_next_allocation{};
bool fail_allocations{};

bool fail_after_reservation(std::size_t size) {
    if (observed_endpoint && ark::http_endpoints.contains(*observed_endpoint))
    {
        observed_endpoint = nullptr;
        failed_size = size;
        fail_allocations = true;
        return true;
    }
    return false;
}

std::uint16_t unused_http_port() {
    asio::io_context context;
    asio::ip::tcp::acceptor acceptor(context, {asio::ip::address_v4::loopback(), 0});
    return acceptor.local_endpoint().port();
}

} // namespace

void* operator new(std::size_t size) {
    if (fail_allocations || fail_next_allocation || fail_after_reservation(size))
    {
        fail_next_allocation = false;
        throw std::bad_alloc{};
    }
    if (auto* value = std::malloc(size ? size : 1))
    {
        return value;
    }
    throw std::bad_alloc{};
}

void operator delete(void* value) noexcept {
    std::free(value);
}

void operator delete(void* value, std::size_t) noexcept {
    std::free(value);
}

// Match the scalar nothrow allocation family when a native constructor unwinds.
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    try
    { return ::operator new(size); }
    catch (...)
    { return nullptr; }
}

void operator delete(void* value, const std::nothrow_t&) noexcept {
    ::operator delete(value);
}

TEST_CASE("HTTP allocation failure after reservation releases only its endpoint before worker construction") {
    const std::string ip = "127.0.0.1";
    const auto port = unused_http_port();
    const auto endpoint = ark::endpoint_key(ip, port);
    auto server = ark::detail::make_http_server_module();
    bool threw{};
    observed_endpoint = &endpoint;
    try
    { server->start_server(ip, port, 1); }
    catch (const std::bad_alloc&)
    { threw = true; }
    observed_endpoint = nullptr;
    fail_allocations = false;
    CHECK(threw);
    // make_unique's object allocation precedes the native constructor and its workers.
    CHECK(failed_size == sizeof(arknet::http_server));
    CHECK_FALSE(ark::http_endpoints.contains(endpoint));
    REQUIRE(server->start_server(ip, port, 1));

    auto duplicate = ark::detail::make_http_server_module();
    CHECK_FALSE(duplicate->start_server(ip, port, 1));
    CHECK(duplicate->pre_shut());
    CHECK(ark::http_endpoints.contains(endpoint));
    CHECK_FALSE(duplicate->start_server(ip, port, 1));
    CHECK(server->pre_shut());
    REQUIRE(duplicate->start_server(ip, port, 1));
    CHECK(duplicate->pre_shut());
    CHECK_FALSE(ark::http_endpoints.contains(endpoint));
}

TEST_CASE("HTTP reservation transfers its string storage without allocating") {
    auto endpoint = ark::endpoint_key(std::string(128, 'x'), 12345);
    const auto expected = endpoint;
    const auto* storage = endpoint.data();
    std::string committed;
    {
        ark::endpoint_reservation reservation(std::move(endpoint));
        REQUIRE(reservation.is_reserved());
        static_assert(noexcept(reservation.transfer_to(committed)));
        fail_next_allocation = true;
        reservation.transfer_to(committed);
        const auto allocation_unused = std::exchange(fail_next_allocation, false);
        CHECK(allocation_unused);
        CHECK(committed.data() == storage);
        CHECK(committed == expected);
    }
    CHECK(ark::http_endpoints.contains(committed));
    ark::release_endpoint(committed);
    CHECK_FALSE(ark::http_endpoints.contains(expected));
}

TEST_CASE("HTTP native listen rejection releases its reservation for reuse") {
    const std::string ip = "127.0.0.1";
    asio::io_context context;
    asio::ip::tcp::acceptor occupied(context, {asio::ip::address_v4::loopback(), 0});
    const auto port = occupied.local_endpoint().port();
    const auto endpoint = ark::endpoint_key(ip, port);
    auto server = ark::detail::make_http_server_module();
    CHECK_FALSE(server->start_server(ip, port, 1));
    CHECK_FALSE(ark::http_endpoints.contains(endpoint));
    occupied.close();
    REQUIRE(server->start_server(ip, port, 1));
    CHECK(server->pre_shut());
    CHECK_FALSE(ark::http_endpoints.contains(endpoint));
}
