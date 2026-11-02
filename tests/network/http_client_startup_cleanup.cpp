// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <array>
#include <atomic>
#include <cfloat>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include <arknet/http/http_server.hpp>

#include "http_client_startup_access.hpp"

namespace {
thread_local bool fail_allocations{};
} // namespace

void* operator new(std::size_t size) {
    if (!fail_allocations)
    {
        if (auto* pointer = std::malloc(size ? size : 1))
        {
            return pointer;
        }
    }
    throw std::bad_alloc{};
}

void* operator new[](std::size_t size) {
    return ::operator new(size);
}

void operator delete(void* pointer) noexcept {
    std::free(pointer);
}

void operator delete[](void* pointer) noexcept {
    std::free(pointer);
}

void operator delete(void* pointer, std::size_t) noexcept {
    std::free(pointer);
}

void operator delete[](void* pointer, std::size_t) noexcept {
    std::free(pointer);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    try
    { return ::operator new(size); }
    catch (...)
    { return nullptr; }
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    try
    { return ::operator new[](size); }
    catch (...)
    { return nullptr; }
}

void operator delete(void* pointer, const std::nothrow_t&) noexcept {
    std::free(pointer);
}

void operator delete[](void* pointer, const std::nothrow_t&) noexcept {
    std::free(pointer);
}

namespace {
using namespace std::chrono_literals;
using access = ark::http_client_startup_test_access;

[[noreturn]] void exit_startup_probe(int status) noexcept {
    std::fflush(stderr);
#if defined(_WIN32)
    // Match the launcher's failure exit: retained DLLs must not receive process
    // detach callbacks, which can wait on native cleanup threads during exit.
    ::TerminateProcess(::GetCurrentProcess(), static_cast<UINT>(status));
    std::_Exit(3); // A failed termination request must never pass the controller.
#else
    std::_Exit(status);
#endif
}

struct failing_module : ark::module
{
    unsigned phase{};
    bool throws{};
    unsigned ordinary_cleanup{};

    bool step(unsigned current) const {
        if (phase != current)
        {
            return true;
        }
        if (throws)
        {
            throw std::runtime_error("controlled later HTTP startup failure");
        }
        return false;
    }

    bool init() override {
        return step(0);
    }

    bool post_init() override {
        return step(1);
    }

    bool check_config() override {
        return step(2);
    }

    bool pre_update() override {
        return step(3);
    }

    bool pre_shut() override {
        ++ordinary_cleanup;
        return true;
    }

    bool shut() override {
        ++ordinary_cleanup;
        return true;
    }
};

ark::startup_cleanup_result release_older(void* pointer) {
    ++*static_cast<unsigned*>(pointer);
    return ark::startup_cleanup_result::done;
}

template<typename predicate>
bool eventually(predicate&& ready) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!ready())
    {
        if (std::chrono::steady_clock::now() >= deadline)
        {
            return false;
        }
        std::this_thread::yield();
    }
    return true;
}

class http_peer
{
public:
    http_peer() {
        server_.bind_recv([this](auto& session, auto&) {
            std::lock_guard lock(mutex_);
            pending_.push_back(session);
            ++received;
        });
    }

    ~http_peer() {
        server_.stop();
    }

    bool start() {
        return server_.start("127.0.0.1", 0);
    }

    std::uint16_t port() const {
        return server_.listen_port();
    }

    void reply() {
        decltype(pending_) pending;
        {
            std::lock_guard lock(mutex_);
            pending.swap(pending_);
        }
        for (const auto& session : pending)
        {
            arknet::http_request::response_type response(arknet::http::status::ok, 11);
            response.body() = "accepted";
            response.prepare_payload();
            session->async_send(std::move(response));
        }
    }

    std::atomic<unsigned> received{};

private:
    arknet::http_server server_{arknet::detail::http_frame_size, arknet::detail::max_buffer_size, 1};
    std::mutex mutex_;
    std::vector<std::shared_ptr<arknet::http_session>> pending_;
};

} // namespace

TEST_CASE("Failed startup retires the idle constructor-started Net HTTP runtime") {
    for (unsigned phase{}; phase < 4; ++phase)
    {
        for (bool throws : {false, true})
        {
            ark::plugin_manager manager;
            auto older = std::make_shared<unsigned>(0);
            REQUIRE(manager.prepare_startup_cleanup("older", "test", older, release_older));
            auto client = ark::detail::make_http_client_module(&manager);
            failing_module failure;
            failure.phase = phase;
            failure.throws = throws;
            REQUIRE(access::runtime(*client).started());
            REQUIRE(manager.add_module("http", client.get()));
            REQUIRE(manager.add_module("failure", &failure));
            if (throws)
            {
                CHECK_THROWS_AS(manager.start(0, {}, {}, {}), std::runtime_error);
            }
            else
            {
                CHECK_FALSE(manager.start(0, {}, {}, {}));
            }
            CHECK(manager.cleanup_failed_startup() == ark::startup_cleanup_result::done);
            CHECK(access::runtime(*client).stopped());
            CHECK(access::runtime(*client).get_thread_id(0) == std::thread::id{});
            CHECK(manager.cleanup_failed_startup() == ark::startup_cleanup_result::done);
            CHECK(*older == 1);
            CHECK(failure.ordinary_cleanup == 0);
            auto rejected = client->get("127.0.0.1", 1, "/after-startup-cleanup");
            REQUIRE(rejected.wait_for(0ms) == std::future_status::ready);
            CHECK(rejected.get().second == arknet::error_code(asio::error::operation_aborted).message());
            CHECK_THROWS_AS(manager.start(0, {}, {}, {}), std::logic_error);
        }
    }
}

TEST_CASE("Net HTTP constructor rejects failed enrollment before acquiring workers") {
    ark::plugin_manager manager;
    auto first = ark::detail::make_http_client_module(&manager);
    CHECK_THROWS_WITH_AS(ark::detail::make_http_client_module(&manager), "Net HTTP startup cleanup enrollment rejected",
        std::runtime_error);
    CHECK(access::runtime(*first).started());
    failing_module failure;
    REQUIRE(manager.add_module("failure", &failure));
    CHECK_FALSE(manager.start(0, {}, {}, {}));
    CHECK(manager.cleanup_failed_startup() == ark::startup_cleanup_result::done);
    CHECK(access::runtime(*first).stopped());
}

TEST_CASE("Net HTTP factory allocation failure acquires no worker") {
    ark::plugin_manager manager;
    auto older = std::make_shared<unsigned>(0);
    REQUIRE(manager.prepare_startup_cleanup("older", "test", older, release_older));
    bool rejected{};
    fail_allocations = true;
    try
    { auto client = ark::detail::make_http_client_module(&manager); }
    catch (const std::bad_alloc&)
    { rejected = true; }
    fail_allocations = false;
    CHECK(rejected);
    failing_module failure;
    REQUIRE(manager.add_module("failure", &failure));
    CHECK_FALSE(manager.start(0, {}, {}, {}));
    CHECK(manager.cleanup_failed_startup() == ark::startup_cleanup_result::done);
    CHECK(*older == 1);
}

TEST_CASE("Net HTTP successful startup disarms failure cleanup and preserves normal ownership") {
    ark::plugin_manager manager;
    auto client = ark::detail::make_http_client_module(&manager);
    REQUIRE(manager.add_module("http", client.get()));
    REQUIRE(manager.start(0, {}, {}, {}));
    CHECK(manager.cleanup_failed_startup() == ark::startup_cleanup_result::unsafe);
    CHECK(access::runtime(*client).started());
    CHECK(manager.stop(1));
    CHECK(access::runtime(*client).stopped());
}

TEST_CASE("Net HTTP successful disarm restores destructor cancellation") {
    http_peer peer;
    REQUIRE(peer.start());
    ark::plugin_manager manager;
    auto client = ark::detail::make_http_client_module(&manager);
    REQUIRE(manager.start(0, {}, {}, {}));
    auto accepted = client->get("127.0.0.1", peer.port(), "/normal-destruction");
    REQUIRE(eventually([&] { return peer.received == 1; }));
    auto capsule = access::capsule(*client);
    client.reset();
    REQUIRE(accepted.wait_for(5s) == std::future_status::ready);
    CHECK_FALSE(accepted.get().first);
    CHECK(capsule.expired());
}

TEST_CASE("Pending startup HTTP work outlives its candidate and retains older resource owners") {
    http_peer peer;
    REQUIRE(peer.start());
    ark::plugin_manager manager;
    auto older = std::make_shared<unsigned>(0);
    REQUIRE(manager.prepare_startup_cleanup("older", "test", older, release_older));
    auto client = ark::detail::make_http_client_module(&manager);
    auto accepted = client->get("127.0.0.1", peer.port(), "/pending");
    REQUIRE(eventually([&] { return peer.received == 1; }));
    failing_module failure;
    REQUIRE(manager.add_module("failure", &failure));
    CHECK_FALSE(manager.start(0, {}, {}, {}));
    auto* runtime = &access::runtime(*client);
    auto retained = access::capsule(*client);
    client.reset();
    CHECK_FALSE(retained.expired());
    CHECK(accepted.wait_for(0ms) != std::future_status::ready);
    CHECK(manager.cleanup_failed_startup() == ark::startup_cleanup_result::unsafe);
    CHECK(runtime->started());
    CHECK(*older == 0);
    peer.reply();
    REQUIRE(accepted.wait_for(5s) == std::future_status::ready);
    CHECK(accepted.get().first);
    CHECK(manager.cleanup_failed_startup() == ark::startup_cleanup_result::unsafe);
    CHECK(runtime->started());
    CHECK(*older == 0);
    CHECK(failure.ordinary_cleanup == 0);
    // Test-owner teardown only: the failed manager never retries or invokes it.
    runtime->stop();
    CHECK(runtime->stopped());
}

TEST_CASE("Queued startup HTTP callbacks keep captures and forbid automatic delivery") {
    http_peer peer;
    REQUIRE(peer.start());
    ark::plugin_manager manager;
    auto older = std::make_shared<unsigned>(0);
    REQUIRE(manager.prepare_startup_cleanup("older", "test", older, release_older));
    auto client = ark::detail::make_http_client_module(&manager);
    auto captured = std::make_shared<int>(42);
    std::weak_ptr<int> retained = captured;
    unsigned delivered{};
    client->post([&, captured](auto result) { delivered += result.first && *captured == 42; }, "127.0.0.1", peer.port(),
        "/callback");
    captured.reset();
    REQUIRE(eventually([&] { return peer.received == 1; }));
    peer.reply();
    REQUIRE(eventually([&] { return access::queued(*client) == 1; }));
    failing_module failure;
    REQUIRE(manager.add_module("failure", &failure));
    CHECK_FALSE(manager.start(0, {}, {}, {}));
    CHECK(manager.cleanup_failed_startup() == ark::startup_cleanup_result::unsafe);
    CHECK(access::runtime(*client).started());
    CHECK_FALSE(retained.expired());
    CHECK(delivered == 0);
    CHECK(*older == 0);
    CHECK_FALSE(manager.update(1));
    CHECK(delivered == 0);
    CHECK(client->update()); // Explicit test-owner delivery after retention evidence.
    CHECK(delivered == 1);
    CHECK(retained.expired());
    CHECK(manager.cleanup_failed_startup() == ark::startup_cleanup_result::unsafe);
    CHECK(client->pre_shut());
}

TEST_CASE("Executing startup HTTP callbacks retain their active frame") {
    http_peer peer;
    REQUIRE(peer.start());
    ark::plugin_manager manager;
    auto client = ark::detail::make_http_client_module(&manager);
    failing_module failure;
    REQUIRE(manager.add_module("failure", &failure));
    auto result = ark::startup_cleanup_result::not_run;
    client->post(
        [&](auto) {
            CHECK_FALSE(manager.start(0, {}, {}, {}));
            result = manager.cleanup_failed_startup();
            CHECK(access::runtime(*client).started());
        },
        "127.0.0.1", peer.port(), "/active");
    REQUIRE(eventually([&] { return peer.received == 1; }));
    peer.reply();
    REQUIRE(eventually([&] { return access::queued(*client) == 1; }));
    CHECK(client->update());
    CHECK(result == ark::startup_cleanup_result::unsafe);
    CHECK(manager.cleanup_failed_startup() == ark::startup_cleanup_result::unsafe);
    CHECK(client->pre_shut());
}

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--allocation-failure")
    {
        ark::plugin_manager manager;
        auto older = std::make_shared<unsigned>(0);
        if (!manager.prepare_startup_cleanup("older", "test", older, release_older))
        {
            exit_startup_probe(2);
        }
        auto client = ark::detail::make_http_client_module(&manager);
        auto capsule = access::capsule(*client);
        auto* runtime = &access::runtime(*client);
        failing_module failure;
        manager.add_module("failure", &failure);
        if (manager.start(0, {}, {}, {}))
        {
            exit_startup_probe(2);
        }
        fail_allocations = true;
        client.reset(); // Candidate unwinding must leave the enrolled owner intact.
        if (capsule.expired() || !runtime->started())
        {
            exit_startup_probe(2);
        }
        const auto result = manager.cleanup_failed_startup();
        const auto repeated = manager.cleanup_failed_startup();
        if (result != ark::startup_cleanup_result::done || repeated != result || !runtime->stopped() || *older != 1 ||
            capsule.expired())
        {
            std::fprintf(stderr, "HTTP allocation probe: result=%d repeated=%d stopped=%d older=%u\n",
                static_cast<int>(result), static_cast<int>(repeated), runtime->stopped(), *older);
            exit_startup_probe(2);
        }
        std::fputs("HTTP startup allocation failure: idle runtime joined; candidate gone; exit 1\n", stderr);
        exit_startup_probe(1); // Failure cleanup never becomes same-process recovery.
    }
    if (argc == 3 && std::string_view(argv[1]) == "--install-failure")
    {
        ark::logger log;
        log.set_log_func([](spdlog::level::level_enum, const std::string& message) {
            std::fprintf(stderr, "%s\n", message.c_str());
        });
        ark::logger::init(&log);
        ark::plugin_manager manager;
        const std::string directory = std::string(argv[2]) + "/";
        constexpr auto name = "ark_http_startup_install_fixture";
        bool failed{};
        try
        { manager.start(0, directory, {}, {{name, ""}}); }
        catch (const std::runtime_error&)
        { failed = true; }
        ark::dynamic_library library(name);
        if (!failed || std::string_view(manager.get_startup_call().phase.data()) != "install" ||
            !library.load(directory))
        {
            exit_startup_probe(2);
        }
        const auto started = reinterpret_cast<bool (*)()>(library.get_symbol("http_fixture_runtime_started"));
        const auto stopped = reinterpret_cast<bool (*)()>(library.get_symbol("http_fixture_runtime_stopped"));
        if (!started || !stopped || !started() ||
            manager.cleanup_failed_startup() != ark::startup_cleanup_result::done || !stopped() ||
            manager.cleanup_failed_startup() != ark::startup_cleanup_result::done)
        {
            exit_startup_probe(2);
        }
        std::fputs("HTTP install failure: idle runtime joined once; retained DSO and owners; exit 1\n", stderr);
        exit_startup_probe(1);
    }
    doctest::Context context(argc, argv);
    return context.run();
}
