// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "../network/doctest.h"

#include <ark/dynamic_library.hpp>
#include <ark/plugin_manager.hpp>

namespace ark::detail {

struct dynamic_library_test_access
{
    static bool succeeded(int result) {
        return dynamic_library::native_unload_succeeded(result);
    }

    static DYNLIB_HANDLE handle(const dynamic_library& library) {
        return library.lib_inst_;
    }

    template<typename close_type>
    static bool unload(dynamic_library& library, close_type close) {
        return library.unload_with(std::move(close));
    }
};

struct plugin_manager_test_access
{
    static void add(plugin_manager& manager, std::string name, dynamic_library* library) {
        manager.plugin_libs_.emplace(std::move(name), library);
    }

    static std::vector<std::pair<std::string, dynamic_library*>> libraries(const plugin_manager& manager) {
        return {manager.plugin_libs_.begin(), manager.plugin_libs_.end()};
    }

    template<typename unload_type>
    static bool stop(plugin_manager& manager, unload_type unload) {
        return manager.stop_with(0, std::move(unload));
    }

    static void release_retained(plugin_manager& manager) {
        for (auto& [name, library] : manager.plugin_libs_)
        {
            if (library)
            {
                CHECK(library->unload());
                delete library;
                library = nullptr;
            }
        }
    }
};

} // namespace ark::detail

namespace {

using library_access = ark::detail::dynamic_library_test_access;
using manager_access = ark::detail::plugin_manager_test_access;

constexpr const char* probe = "ark_oss_sdk_entry_probe";
#ifdef ARK_PLATFORM_WIN
constexpr int native_failure = 0;
#else
constexpr int native_failure = 1;
#endif

class quiet_logger
{
public:
    quiet_logger() {
        logger_.set_log_func([](spdlog::level::level_enum, const std::string&) {});
        ark::logger::set_instance(&logger_);
    }

    ~quiet_logger() {
        ark::logger::set_instance(previous_);
    }

private:
    ark::logger* previous_{ark::logger::instance()};
    ark::logger logger_;
};

struct retained_manager
{
    ark::plugin_manager manager;

    ~retained_manager() {
        // Failure injection never sends an invalid handle to the OS. Release
        // every real reference still retained after inspecting the failed state.
        manager_access::release_retained(manager);
    }

    void add(const std::string& name) {
        auto library = std::make_unique<ark::dynamic_library>(probe);
        REQUIRE(library->load(ARK_UNLOAD_PROBE_DIR));
        manager_access::add(manager, name, library.release());
    }
};

} // namespace

TEST_CASE("Native unload results follow the platform success convention") {
#ifdef ARK_PLATFORM_WIN
    CHECK_FALSE(library_access::succeeded(0));
    CHECK(library_access::succeeded(1));
    CHECK(library_access::succeeded(-1));
#else
    CHECK(library_access::succeeded(0));
    CHECK_FALSE(library_access::succeeded(1));
    CHECK_FALSE(library_access::succeeded(-1));
#endif
}

TEST_CASE("Empty and successfully unloaded libraries never close a handle twice") {
    quiet_logger logger;
    ark::dynamic_library library(probe);
    unsigned calls{};
    const auto close = [&](DYNLIB_HANDLE handle) {
        ++calls;
        return DYNLIB_UNLOAD(handle);
    };
    CHECK(library_access::unload(library, close));
    CHECK(library.unload());
    CHECK(calls == 0);
    REQUIRE(library.load(ARK_UNLOAD_PROBE_DIR));
    REQUIRE(library.get_symbol("DllEntryPlugin") != nullptr);
    REQUIRE(library_access::unload(library, close));
    CHECK(library_access::handle(library) == nullptr);
    CHECK(library_access::unload(library, close));
    CHECK(library.unload());
    CHECK(calls == 1);
}

TEST_CASE("A failed native unload retains the valid handle until a successful close") {
    quiet_logger logger;
    ark::dynamic_library library(probe);
    REQUIRE(library.load(ARK_UNLOAD_PROBE_DIR));
    const auto handle = library_access::handle(library);
    unsigned calls{};
    CHECK_FALSE(library_access::unload(library, [&](DYNLIB_HANDLE received) {
        ++calls;
        CHECK(received == handle);
        return native_failure;
    }));
    CHECK(calls == 1);
    CHECK(library_access::handle(library) == handle);
    CHECK(library.get_symbol("DllExitPlugin") != nullptr);
    REQUIRE(library.unload());
    CHECK(library_access::handle(library) == nullptr);
    CHECK(library.unload());
}

TEST_CASE("Manager unload failure retains its wrapper and stops all later closes") {
    quiet_logger logger;
    ark::dynamic_library observer(probe);
    REQUIRE(observer.load(ARK_UNLOAD_PROBE_DIR));
    auto events = reinterpret_cast<unsigned (*)(unsigned)>(observer.get_symbol("oss_sdk_entry_events"));
    REQUIRE(events != nullptr);
    const auto destroyed = events(1);
    retained_manager owner;
    REQUIRE(owner.manager.start(0, ARK_UNLOAD_PROBE_DIR, "conf/", {{probe, "entry-probe.toml"}}));
    owner.add("second");
    owner.add("third");
    const auto order = manager_access::libraries(owner.manager);
    REQUIRE(order.size() == 3);
    const auto failed_handle = library_access::handle(*order[1].second);
    unsigned closes{};
    const auto unload = [&](ark::dynamic_library& library) {
        ++closes;
        return closes == 2 ? library_access::unload(library, [](DYNLIB_HANDLE) { return native_failure; })
                           : library.unload();
    };
    CHECK_FALSE(manager_access::stop(owner.manager, unload));
    CHECK(closes == 2);
    CHECK(owner.manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::failed);
    CHECK(owner.manager.get_shutdown_failure() == "unload: " + order[1].first);
    CHECK(events(1) == destroyed + 1);
    CHECK_FALSE(owner.manager.get_shutdown_exception());
    const auto retained = manager_access::libraries(owner.manager);
    REQUIRE(retained.size() == 3);
    CHECK(retained[0].second == nullptr); // Keep ARK_DELETE's existing map-entry behavior.
    CHECK(retained[1].second == order[1].second);
    CHECK(retained[2].second == order[2].second);
    CHECK(library_access::handle(*retained[1].second) == failed_handle);
    CHECK(retained[1].second->get_symbol("DllExitPlugin") != nullptr);
    CHECK_FALSE(owner.manager.stop(1));
    CHECK_FALSE(manager_access::stop(owner.manager, unload));
    CHECK(closes == 2);
    CHECK(owner.manager.get_shutdown_failure() == "unload: " + order[1].first);
    CHECK(events(1) == destroyed + 1);
    CHECK(observer.unload());
}

TEST_CASE("Manager native unload exceptions use the same retained failure state") {
    quiet_logger logger;
    retained_manager owner;
    owner.add("retained");
    unsigned closes{};
    CHECK_FALSE(manager_access::stop(owner.manager, [&](ark::dynamic_library&) -> bool {
        ++closes;
        throw std::runtime_error("controlled close exception");
    }));
    CHECK(owner.manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::failed);
    CHECK(owner.manager.get_shutdown_failure() == "unload: retained");
    REQUIRE(owner.manager.get_shutdown_exception());
    CHECK_THROWS_AS(std::rethrow_exception(owner.manager.get_shutdown_exception()), std::runtime_error);
    CHECK_FALSE(owner.manager.stop(1));
    CHECK(closes == 1);
    REQUIRE(manager_access::libraries(owner.manager).front().second != nullptr);
}

TEST_CASE("Normal plugin shutdown closes real DSO references once and remains stopped") {
    quiet_logger logger;
    ark::dynamic_library observer(probe);
    REQUIRE(observer.load(ARK_UNLOAD_PROBE_DIR));
    auto events = reinterpret_cast<unsigned (*)(unsigned)>(observer.get_symbol("oss_sdk_entry_events"));
    REQUIRE(events != nullptr);
    const auto created = events(0);
    const auto destroyed = events(1);
    ark::plugin_manager manager;
    REQUIRE(manager.start(0, ARK_UNLOAD_PROBE_DIR, "conf/", {{probe, "entry-probe.toml"}}));
    CHECK(events(0) == created + 1);
    CHECK(events(1) == destroyed);
    REQUIRE(manager.stop(1));
    CHECK(manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::stopped);
    CHECK(events(1) == destroyed + 1);
    REQUIRE(manager_access::libraries(manager).size() == 1);
    CHECK(manager_access::libraries(manager).front().second == nullptr);
    CHECK(manager.stop(2));
    CHECK(events(1) == destroyed + 1);
    CHECK(observer.unload());
    CHECK(observer.unload());
}
