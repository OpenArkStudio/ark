// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <cstddef>
#include <exception>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "../network/doctest.h"

#include <ark/dynamic_library.hpp>
#include <ark/plugin_base.hpp>
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
    static void configure(plugin_manager& manager, const std::string& name) {
        manager.plugins_.emplace(name, "ownership.toml");
        manager.plugin_conf_dir_ = "conf/";
    }

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

enum class plugin_failure
{
    none,
    allocation,
    manager_metadata,
    configuration_metadata,
    install
};

struct plugin_events
{
    plugin_failure failure{};
    unsigned destroyed{};
    unsigned deallocated{};
    unsigned manager_assignments{};
    unsigned configuration_assignments{};
    unsigned installs{};
    unsigned uninstalls{};
    ark::plugin* retained{};
};

class ownership_plugin final : public ark::plugin_base<ownership_plugin>
{
public:
    inline static plugin_events* events{};

    static void* operator new(std::size_t size, const std::nothrow_t& tag) noexcept {
        return events->failure == plugin_failure::allocation ? nullptr : ::operator new(size, tag);
    }

    static void operator delete(void* value) noexcept {
        ++events->deallocated;
        ::operator delete(value);
    }

    static void operator delete(void* value, const std::nothrow_t&) noexcept {
        operator delete(value);
    }

    ~ownership_plugin() override {
        ++events->destroyed;
    }

    void set_plugin_manager(ark::plugin_manager* manager) override {
        ++events->manager_assignments;
        if (events->failure == plugin_failure::manager_metadata)
        {
            throw std::runtime_error("manager metadata failed");
        }
        plugin_base::set_plugin_manager(manager);
    }

    void set_plugin_conf(const std::string& configuration) override {
        ++events->configuration_assignments;
        if (events->failure == plugin_failure::configuration_metadata)
        {
            throw std::runtime_error("configuration metadata failed");
        }
        CHECK(configuration == "conf/ownership.toml");
        plugin_base::set_plugin_conf(configuration);
    }

    void install() override {
        ++events->installs;
        events->retained = this;
        if (events->failure == plugin_failure::install)
        {
            throw std::runtime_error("installation failed");
        }
    }

    void uninstall() override {
        ++events->uninstalls;
    }
};

unsigned allocation_entry_calls{};

void allocate_entry_plugin(ark::plugin_manager* manager, const std::string& name) {
    ++allocation_entry_calls;
    manager->register_plugin<ownership_plugin>(name);
}

constexpr const char* probe = "ark_oss_sdk_entry_probe";
#ifdef ARK_PLATFORM_WIN
constexpr int native_failure = 0;
#else
constexpr int native_failure = 1;
#endif

class quiet_logger
{
public:
    explicit quiet_logger(bool throw_error = false) {
        logger_.set_log_func([throw_error](spdlog::level::level_enum level, const std::string&) {
            if (throw_error && level == spdlog::level::err)
            {
                throw std::runtime_error("load diagnostic failed");
            }
        });
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

TEST_CASE("Plugin candidates retain nothrow allocation and retire failed metadata once") {
    for (const auto failure : {plugin_failure::allocation, plugin_failure::manager_metadata,
             plugin_failure::configuration_metadata, plugin_failure::none})
    {
        CAPTURE(static_cast<int>(failure));
        plugin_events events{failure};
        ownership_plugin::events = &events;
        ark::plugin_manager manager;
        manager_access::configure(manager, "ownership-probe");
        if (failure == plugin_failure::manager_metadata)
        {
            CHECK_THROWS_WITH_AS(manager.register_plugin<ownership_plugin>("ownership-probe"),
                "manager metadata failed", std::runtime_error);
        }
        else if (failure == plugin_failure::configuration_metadata)
        {
            CHECK_THROWS_WITH_AS(manager.register_plugin<ownership_plugin>("ownership-probe"),
                "configuration metadata failed", std::runtime_error);
        }
        else
        {
            CHECK_NOTHROW(manager.register_plugin<ownership_plugin>("ownership-probe"));
        }
        CHECK(events.manager_assignments == (failure == plugin_failure::allocation ? 0 : 1));
        CHECK(events.configuration_assignments ==
              (failure == plugin_failure::allocation || failure == plugin_failure::manager_metadata ? 0 : 1));
        CHECK(events.installs == (failure == plugin_failure::none ? 1 : 0));
        CHECK(events.uninstalls == 0);
        CHECK(
            events.destroyed ==
            (failure == plugin_failure::manager_metadata || failure == plugin_failure::configuration_metadata ? 1 : 0));
        CHECK(events.deallocated == events.destroyed);
        CHECK(manager.get_startup_failure() ==
              (failure == plugin_failure::allocation ? "allocate_plugin: ownership-probe" : ""));
        if (failure == plugin_failure::none)
        {
            // A distinct library stem still unregisters through the RTTI key.
            CHECK_NOTHROW(manager.unregister_plugin<ownership_plugin>());
            CHECK(events.uninstalls == 1);
            CHECK(events.destroyed == 1);
            CHECK(events.deallocated == 1);
        }
    }
}

TEST_CASE("Plugin installation exceptions keep the existing explicit cleanup boundary") {
    plugin_events events{plugin_failure::install};
    ownership_plugin::events = &events;
    ark::plugin_manager manager;
    manager_access::configure(manager, "ownership-probe");
    CHECK_THROWS_WITH_AS(
        manager.register_plugin<ownership_plugin>("ownership-probe"), "installation failed", std::runtime_error);
    CHECK(events.installs == 1);
    CHECK(events.uninstalls == 0);
    CHECK(events.destroyed == 0);
    REQUIRE(events.retained != nullptr);
    // This fixture publishes no modules or asynchronous work. Production startup
    // failures retain those resources until process exit instead of rolling back.
    delete events.retained;
    CHECK(events.destroyed == 1);
    CHECK(events.deallocated == 1);
}

TEST_CASE("Failed library loading retires unpublished wrappers even when diagnostics throw") {
    for (const bool throw_error : {false, true})
    {
        quiet_logger logger(throw_error);
        ark::plugin_manager manager;
        const auto start = [&] {
            return manager.start(0, ARK_UNLOAD_PROBE_DIR, {}, {{"ark_missing_ownership_probe", ""}});
        };
        if (throw_error)
        {
            CHECK_THROWS_WITH_AS(start(), "load diagnostic failed", std::runtime_error);
        }
        else
        {
            CHECK_FALSE(start());
        }
        CHECK(manager_access::libraries(manager).empty());
        CHECK(manager.get_startup_failure() == "load_plugin: ark_missing_ownership_probe");
        CHECK(manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::running);
    }
}

TEST_CASE("A null plugin allocation fails its real entrypoint before later libraries and module startup") {
    quiet_logger logger;
    plugin_events events{plugin_failure::allocation};
    ownership_plugin::events = &events;
    allocation_entry_calls = 0;
    ark::dynamic_library first("ark_plugin_allocation_first");
    ark::dynamic_library second("ark_plugin_allocation_second");
    REQUIRE(first.load(ARK_ALLOCATION_PROBE_DIR));
    REQUIRE(second.load(ARK_ALLOCATION_PROBE_DIR));
    using set_entry = void (*)(void (*)(ark::plugin_manager*, const std::string&));
    for (auto* library : {&first, &second})
    {
        const auto set = reinterpret_cast<set_entry>(library->get_symbol("allocation_probe_set_entry"));
        REQUIRE(set != nullptr);
        set(allocate_entry_plugin);
    }

    struct startup_module final : ark::module
    {
        bool init() override {
            ++calls;
            return true;
        }

        unsigned calls{};
    } module;

    {
        retained_manager owner;
        REQUIRE(owner.manager.add_module("allocation-startup-observer", &module));
        CHECK_FALSE(owner.manager.start(0, ARK_ALLOCATION_PROBE_DIR, {},
            {{"ark_plugin_allocation_first", ""}, {"ark_plugin_allocation_second", ""}}));
        CHECK(allocation_entry_calls == 1);
        CHECK(module.calls == 0);
        CHECK(events.installs == 0);
        CHECK(events.uninstalls == 0);
        CHECK(events.destroyed == 0);
        const auto retained = manager_access::libraries(owner.manager);
        REQUIRE(retained.size() == 1);
        CHECK(owner.manager.get_startup_failure() == "allocate_plugin: " + retained.front().first);
        CHECK(retained.front().second->get_symbol("DllEntryPlugin") != nullptr);
        CHECK(owner.manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::running);
    }
    CHECK(first.unload());
    CHECK(second.unload());
}

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

namespace {

unsigned failing_exit_calls{};
bool unknown_exit_exception{};

void failing_exit(ark::plugin_manager*) {
    ++failing_exit_calls;
    if (unknown_exit_exception)
    {
        throw 7;
    }
    throw std::runtime_error("test plugin exit exception");
}

} // namespace

TEST_CASE("Plugin exit exceptions retain handles and never retry uninstall or native close") {
    for (const bool unknown : {false, true})
    {
        quiet_logger logger;
        constexpr const char* name = "ark_plugin_allocation_first";
        ark::dynamic_library observer(name);

        struct close_observer
        {
            ark::dynamic_library& library;

            ~close_observer() {
                CHECK(library.unload());
            }
        } closed{observer};

        REQUIRE(observer.load(ARK_ALLOCATION_PROBE_DIR));
        using entry = void (*)(ark::plugin_manager*, const std::string&);
        using exit = void (*)(ark::plugin_manager*);
        const auto set_entry = reinterpret_cast<void (*)(entry)>(observer.get_symbol("allocation_probe_set_entry"));
        const auto set_exit = reinterpret_cast<void (*)(exit)>(observer.get_symbol("allocation_probe_set_exit"));
        REQUIRE(set_entry != nullptr);
        REQUIRE(set_exit != nullptr);

        struct reset_callbacks
        {
            decltype(set_entry) reset_entry;
            decltype(set_exit) reset_exit;

            ~reset_callbacks() {
                reset_entry(nullptr);
                reset_exit(nullptr);
            }
        } reset{set_entry, set_exit};

        set_entry(+[](ark::plugin_manager*, const std::string&) {});
        set_exit(failing_exit);
        failing_exit_calls = 0;
        unknown_exit_exception = unknown;
        retained_manager owner;
        REQUIRE(owner.manager.start(0, ARK_ALLOCATION_PROBE_DIR, "", {{name, ""}}));
        const auto before = manager_access::libraries(owner.manager);
        REQUIRE(before.size() == 1);
        const auto handle = library_access::handle(*before.front().second);
        unsigned closes{};
        const auto close = [&](ark::dynamic_library& library) {
            ++closes;
            return library.unload();
        };
        CHECK_FALSE(manager_access::stop(owner.manager, close));
        CHECK(failing_exit_calls == 1);
        CHECK(closes == 0);
        CHECK(owner.manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::failed);
        CHECK(owner.manager.get_shutdown_failure() == std::string("uninstall: ") + name);
        const auto failure = owner.manager.get_shutdown_exception();
        REQUIRE(failure);
        bool observed{};
        try
        { std::rethrow_exception(failure); }
        catch (const std::runtime_error& error)
        {
            CHECK_FALSE(unknown);
            CHECK(std::string(error.what()) == "test plugin exit exception");
            observed = true;
        }
        catch (int value)
        {
            CHECK(unknown);
            CHECK(value == 7);
            observed = true;
        }
        catch (...)
        { FAIL("Plugin exit lost its exception type"); }
        CHECK(observed);
        const auto retained = manager_access::libraries(owner.manager);
        REQUIRE(retained.size() == 1);
        CHECK(retained.front().second == before.front().second);
        CHECK(library_access::handle(*retained.front().second) == handle);
        CHECK(retained.front().second->get_symbol("DllExitPlugin") != nullptr);
        CHECK_FALSE(owner.manager.stop(1));
        CHECK_FALSE(manager_access::stop(owner.manager, close));
        CHECK(failing_exit_calls == 1);
        CHECK(closes == 0);
        CHECK(owner.manager.get_shutdown_exception() == failure);
        CHECK(owner.manager.get_shutdown_failure() == std::string("uninstall: ") + name);
    }
}
