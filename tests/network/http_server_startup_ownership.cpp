// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>

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

#include "http/http_plugin.hpp"
#include "http_server_startup_probe.hpp"

// Publish construction before the first native start allocation, without a
// production fault-injection API or changes to the inherited native runtime.
#define http_server startup_probe_http_server
#include "../../src/plugins/http/http_server_module_impl.cpp"
#undef http_server

namespace {
thread_local bool reject_allocations{};
thread_local bool reject_after_workers{};
bool failure_observed_workers{};

bool reject_new() {
    if (reject_allocations)
    {
        return true;
    }
    auto* native = arknet::startup_observed_server;
    if (reject_after_workers && native)
    {
        // Pool construction/start can allocate while holding its lifecycle
        // mutex. Observe lane identities instead of reentering started(). All
        // workers have initialized before start_iopool posts its first work.
        bool initialized = native->iopool().size() != 0;
        for (std::size_t index{}; index < native->iopool().size(); ++index)
        {
            initialized = initialized && native->iopool().get(index)->get_thread_id() != std::thread::id{};
        }
        if (initialized)
        {
            failure_observed_workers = true;
            reject_allocations = true;
            return true;
        }
    }
    return false;
}
} // namespace

void* operator new(std::size_t size) {
    if (!reject_new())
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

namespace ark {

struct http_server_startup_test_access
{
    static auto state(http_server_module& module) {
        return static_cast<http_server_module_impl&>(module).state_;
    }
};

} // namespace ark

namespace {

using access = ark::http_server_startup_test_access;

[[noreturn]] void exit_probe(int result) noexcept {
    std::fflush(stderr);
#if defined(_WIN32)
    ::TerminateProcess(::GetCurrentProcess(), static_cast<UINT>(result));
    std::_Exit(3);
#else
    std::_Exit(result);
#endif
}

struct failing_module final : ark::module
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
            throw std::runtime_error("controlled HTTP listener startup failure");
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

template<typename State>
void retire_test_owner(const State& state) {
    // Explicit fixture-owner teardown, after retention evidence. The failed
    // manager never retries cleanup or invokes ordinary stop on these owners.
    for (auto* owner = state->owners.get(); owner; owner = owner->next.get())
    {
        if (owner->server)
        {
            owner->server->stop();
            owner->server.reset();
        }
        ark::release_endpoint(owner->listen_key);
    }
    state->owners.reset();
    state->active = nullptr;
    state->callbacks->router = {};
}

std::string request(std::uint16_t port) {
    asio::io_context context;
    asio::ip::tcp::socket socket(context);
    socket.connect({asio::ip::make_address("127.0.0.1"), port});
    arknet::http::request<arknet::http::empty_body> input(arknet::http::verb::get, "/retained", 11);
    input.set(arknet::http::field::host, "127.0.0.1");
    input.keep_alive(false);
    arknet::http::write(socket, input);
    beast::flat_buffer buffer;
    arknet::http::response<arknet::http::string_body> output;
    arknet::http::read(socket, buffer, output);
    return output.body();
}

struct allocation_start_module final : ark::module
{
    ark::http_server_module* server{};
    std::string ip{"127.0.0.1"};

    bool init() override {
        reject_after_workers = true;
        return server->start_server(ip, 0, 2);
    }
};

} // namespace

TEST_CASE("HTTP empty startup ownership seals safely after each false or exception") {
    for (unsigned phase{}; phase < 4; ++phase)
    {
        for (bool throws : {false, true})
        {
            CAPTURE(phase);
            CAPTURE(throws);
            ark::plugin_manager manager;
            auto older = std::make_shared<unsigned>(0);
            REQUIRE(manager.prepare_startup_cleanup("older", "test", older, release_older));
            auto server = ark::detail::make_http_server_module(&manager);
            failing_module failure;
            failure.phase = phase;
            failure.throws = throws;
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
            CHECK(*older == 1);
            CHECK_FALSE(server->start_server("127.0.0.1", 0, 1));
            CHECK(manager.cleanup_failed_startup() == ark::startup_cleanup_result::done);
            CHECK(failure.ordinary_cleanup == 0);
        }
    }
}

TEST_CASE("HTTP native owners stay unsafe after every later false or exception") {
    for (unsigned phase{}; phase < 4; ++phase)
    {
        for (bool throws : {false, true})
        {
            CAPTURE(phase);
            CAPTURE(throws);
            ark::plugin_manager manager;
            auto older = std::make_shared<unsigned>(0);
            REQUIRE(manager.prepare_startup_cleanup("older", "test", older, release_older));
            auto server = ark::detail::make_http_server_module(&manager);
            REQUIRE(server->start_server("127.0.0.1", 0, 2));
            auto state = access::state(*server);
            failing_module failure;
            failure.phase = phase;
            failure.throws = throws;
            REQUIRE(manager.add_module("failure", &failure));
            if (throws)
            {
                CHECK_THROWS_AS(manager.start(0, {}, {}, {}), std::runtime_error);
            }
            else
            {
                CHECK_FALSE(manager.start(0, {}, {}, {}));
            }
            CHECK(manager.cleanup_failed_startup() == ark::startup_cleanup_result::unsafe);
            CHECK(state->active->server->is_started());
            CHECK(state->active->server->iopool().started());
            CHECK(*older == 0);
            retire_test_owner(state);
            CHECK(manager.cleanup_failed_startup() == ark::startup_cleanup_result::unsafe);
            CHECK(*older == 0);
            CHECK(failure.ordinary_cleanup == 0);
        }
    }
}

TEST_CASE("HTTP callbacks retain complete routing state after their module candidate is destroyed") {
    ark::plugin_manager manager;
    auto older = std::make_shared<unsigned>(0);
    REQUIRE(manager.prepare_startup_cleanup("older", "test", older, release_older));
    auto server = ark::detail::make_http_server_module(&manager);
    auto captured = std::make_shared<std::string>("retained route");
    std::weak_ptr<std::string> capture = captured;
    std::atomic<unsigned> calls{};
    server->get_router().add(arknet::http::verb::get, "/retained", [&, captured](auto&, auto& response) {
        ++calls;
        response.body() = *captured;
    });
    captured.reset();
    REQUIRE(server->start_server("127.0.0.1", 0, 2));
    auto state = access::state(*server);
    std::weak_ptr<typename decltype(state)::element_type> retained = state;
    const auto port = state->active->server->get_listen_port();
    failing_module failure;
    REQUIRE(manager.add_module("failure", &failure));
    REQUIRE_FALSE(manager.start(0, {}, {}, {}));
    state.reset();
    server.reset();
    REQUIRE_FALSE(retained.expired());
    state = retained.lock();
    CHECK(manager.cleanup_failed_startup() == ark::startup_cleanup_result::unsafe);
    CHECK_FALSE(capture.expired());
    CHECK(calls == 0);
    CHECK(*older == 0);
    CHECK_FALSE(manager.update(1));
    CHECK(calls == 0);
    // An explicit test request exercises native delivery after candidate loss;
    // cleanup itself does not dispatch, cancel, or seal live route callbacks.
    CHECK(request(port) == "retained route");
    CHECK(calls == 1);
    retire_test_owner(state);
    CHECK(capture.expired());
    CHECK(manager.cleanup_failed_startup() == ark::startup_cleanup_result::unsafe);
    CHECK(*older == 0);
}

TEST_CASE("HTTP exposed handlers retain older owners even without a native endpoint") {
    ark::plugin_manager manager;
    auto older = std::make_shared<unsigned>(0);
    REQUIRE(manager.prepare_startup_cleanup("older", "test", older, release_older));
    auto server = ark::detail::make_http_server_module(&manager);
    auto captured = std::make_shared<int>(42);
    std::weak_ptr<int> retained = captured;
    server->get_router().add(arknet::http::verb::get, "/retained", [captured](auto&, auto&) {});
    captured.reset();
    failing_module failure;
    REQUIRE(manager.add_module("failure", &failure));
    REQUIRE_FALSE(manager.start(0, {}, {}, {}));
    CHECK(manager.cleanup_failed_startup() == ark::startup_cleanup_result::unsafe);
    CHECK_FALSE(retained.expired());
    CHECK(*older == 0);
    server->pre_shut(); // Explicit fixture teardown after retention evidence.
    CHECK(retained.expired());
    CHECK(manager.cleanup_failed_startup() == ark::startup_cleanup_result::unsafe);
    CHECK(*older == 0);
}

TEST_CASE("HTTP successful startup disarms enrollment and restores ordinary destructor ownership") {
    ark::plugin_manager manager;
    auto server = ark::detail::make_http_server_module(&manager);
    REQUIRE(server->start_server("127.0.0.1", 0, 1));
    std::weak_ptr<void> retained = access::state(*server);
    REQUIRE(manager.start(0, {}, {}, {}));
    CHECK(manager.cleanup_failed_startup() == ark::startup_cleanup_result::unsafe);
    server.reset();
    CHECK(retained.expired());
}

TEST_CASE("HTTP full startup registry rejects ownership before native acquisition") {
    ark::plugin_manager manager;
    auto older = std::make_shared<unsigned>(0);
    for (std::size_t index{}; index < ark::startup_cleanup_capacity; ++index)
    {
        REQUIRE(manager.prepare_startup_cleanup("owner-" + std::to_string(index), "test", older, release_older));
    }
    const auto constructed = arknet::startup_native_constructions;
    CHECK_THROWS_AS(ark::detail::make_http_server_module(&manager), std::runtime_error);
    CHECK(arknet::startup_native_constructions == constructed);
    failing_module failure;
    REQUIRE(manager.add_module("failure", &failure));
    REQUIRE_FALSE(manager.start(0, {}, {}, {}));
    CHECK(manager.cleanup_failed_startup() == ark::startup_cleanup_result::done);
    CHECK(*older == ark::startup_cleanup_capacity);
}

TEST_CASE("HTTP managed native listen rejection preserves ordinary same-endpoint fallback") {
    asio::io_context context;
    asio::ip::tcp::acceptor occupied(context, {asio::ip::make_address("127.0.0.1"), 0});
#if defined(_WIN32)
    // Do not let the native server's SO_REUSEADDR override the occupied port.
    occupied.close();
    occupied.open(asio::ip::tcp::v4());
    const int exclusive = 1;
    REQUIRE(::setsockopt(occupied.native_handle(), SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                reinterpret_cast<const char*>(&exclusive), sizeof(exclusive)) == 0);
    occupied.bind({asio::ip::make_address("127.0.0.1"), 0});
    occupied.listen();
#endif
    const auto port = occupied.local_endpoint().port();
    ark::plugin_manager manager;
    auto server = ark::detail::make_http_server_module(&manager);
    CHECK_FALSE(server->start_server("127.0.0.1", port, 1));
    CHECK_FALSE(ark::http_endpoints.contains(ark::endpoint_key("127.0.0.1", port)));
    occupied.close();
    REQUIRE(server->start_server("127.0.0.1", port, 1));
    auto state = access::state(*server);
    REQUIRE(state->owners->next);
    REQUIRE(state->owners->next->server);
    CHECK(state->owners->next->server->iopool().stopped());
    REQUIRE(manager.start(0, {}, {}, {}));
    CHECK(server->pre_shut());
}

TEST_CASE("HTTP ordinary listen failures do not accumulate native attempt owners") {
    for (bool managed : {false, true})
    {
        CAPTURE(managed);
        asio::io_context context;
        asio::ip::tcp::acceptor occupied(context);
        occupied.open(asio::ip::tcp::v4());
#if defined(_WIN32)
        const int exclusive = 1;
        REQUIRE(::setsockopt(occupied.native_handle(), SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                    reinterpret_cast<const char*>(&exclusive), sizeof(exclusive)) == 0);
#endif
        occupied.bind({asio::ip::make_address("127.0.0.1"), 0});
        occupied.listen();
        const auto port = occupied.local_endpoint().port();
        ark::plugin_manager manager;
        auto server = ark::detail::make_http_server_module(managed ? &manager : nullptr);
        if (managed)
        {
            REQUIRE(manager.start(0, {}, {}, {}));
        }
        auto state = access::state(*server);
        for (unsigned attempt{}; attempt < 4; ++attempt)
        {
            CHECK_FALSE(server->start_server("127.0.0.1", port, 1));
            CHECK(state->owners == nullptr);
        }
        occupied.close();
        REQUIRE(server->start_server("127.0.0.1", port, 1));
        CHECK(state->owners->next == nullptr);
        CHECK(server->pre_shut());
    }
}

int main(int argc, char** argv) {
    if (argc == 3 && std::string_view(argv[1]) == "--install-failure")
    {
        ark::logger log;
        log.set_log_func([](spdlog::level::level_enum, const std::string& message) {
            std::fprintf(stderr, "%s\n", message.c_str());
        });
        ark::logger::init(&log);
        ark::plugin_manager manager;
        auto older = std::make_shared<unsigned>(0);
        manager.prepare_startup_cleanup("older", "test", older, release_older);
        const std::string directory = std::string(argv[2]) + "/";
        constexpr auto name = "ark_http_server_startup_install_fixture";
        bool threw{};
        try
        { manager.start(0, directory, {}, {{name, ""}}); }
        catch (const std::runtime_error& exception)
        {
            threw = true;
            std::fprintf(stderr, "HTTP ownership install exception: %s\n", exception.what());
        }
        ark::dynamic_library library(name);
        if (!threw || std::string_view(manager.get_startup_call().phase.data()) != "install" ||
            !library.load(directory))
        {
            std::fprintf(stderr, "HTTP ownership install probe: threw=%d phase=%s\n", threw,
                manager.get_startup_call().phase.data());
            exit_probe(2);
        }
        const auto retained = reinterpret_cast<bool (*)()>(library.get_symbol("http_ownership_fixture_retained"));
        if (!retained || !retained() || manager.cleanup_failed_startup() != ark::startup_cleanup_result::unsafe ||
            manager.cleanup_failed_startup() != ark::startup_cleanup_result::unsafe || !retained() || *older != 0)
        {
            std::fprintf(stderr, "HTTP ownership install probe: symbol=%d retained=%d older=%u\n", retained != nullptr,
                retained ? retained() : false, *older);
            exit_probe(2);
        }
        std::fputs(
            "HTTP ownership install failure: candidate gone; native, captures and DSO retained; exit 1\n", stderr);
        exit_probe(1);
    }
    if (argc == 2 && std::string_view(argv[1]) == "--allocation-failure")
    {
        std::set_terminate([] {
            std::fputs("forbidden native destruction before HTTP failure runner\n", stderr);
            exit_probe(2);
        });
        ark::plugin_manager manager;
        auto older = std::make_shared<unsigned>(0);
        manager.prepare_startup_cleanup("older", "test", older, release_older);
        auto server = ark::detail::make_http_server_module(&manager);
        auto state = access::state(*server);
        allocation_start_module startup;
        startup.server = server.get();
        manager.add_module("allocation-start", &startup);
        // Prepare owning arguments before the hook arms allocation rejection.
        const std::string directory;
        const std::string configuration;
        const std::unordered_map<std::string, std::string> plugins;
        bool threw{};
        try
        { manager.start(0, directory, configuration, plugins); }
        catch (const std::bad_alloc&)
        { threw = true; }
        if (!threw || !failure_observed_workers || !reject_allocations || !state->owners || !state->owners->server ||
            !state->owners->server->iopool().started())
        {
            exit_probe(2);
        }
        std::weak_ptr<typename decltype(state)::element_type> retained = state;
        state.reset();
        server.reset();
        state = retained.lock();
        if (!state || manager.cleanup_failed_startup() != ark::startup_cleanup_result::unsafe ||
            manager.cleanup_failed_startup() != ark::startup_cleanup_result::unsafe || *older != 0 ||
            !state->owners->server->iopool().started() ||
            state->owners->server->iopool().get(0)->get_thread_id() == std::thread::id{})
        {
            exit_probe(2);
        }
        std::fputs("HTTP ownership failure runner reached: native workers and older owners retained; exit 1\n", stderr);
        exit_probe(1);
    }
    doctest::Context context(argc, argv);
    return context.run();
}
