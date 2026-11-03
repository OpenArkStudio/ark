// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <type_traits>

#ifdef ARK_DATABASE_TEST_DLADDR
#include <dlfcn.h>
#endif

#include <ark/mysql/mysql_module.hpp>
#include <ark/plugin_manager.hpp>
#include <ark/redis/redis_module.hpp>
#include <ark/time/date_time.hpp>

static_assert(std::is_abstract_v<ark::mysql_module> && std::is_abstract_v<ark::redis_module>);

#if __has_include(<ark/mysql/mysql_module_impl.hpp>) || __has_include(<ark/redis/redis_module_impl.hpp>) || \
    __has_include(<ark/mysql/mysql_plugin.hpp>) || __has_include(<ark/redis/redis_plugin.hpp>) || \
    __has_include(<mysql/mysql_plugin.hpp>) || __has_include(<redis/redis_plugin.hpp>)
#error "Database public header mapping must exclude implementation and assembly headers"
#endif

namespace {

void require(bool condition, const std::string& reason) {
    if (!condition)
    {
        std::cerr << reason << std::endl;
        // A failed acceptance must not unwind through owners still in use.
        std::_Exit(1);
    }
}

void require_implementation(const ark::module* module, const std::filesystem::path& expected_dso) {
    require(module, "installed database interface RTTI mismatch");
#ifdef ARK_DATABASE_TEST_DLADDR
    Dl_info library{};
    void* const* vtable{};
    // Native UNIX C++ ABIs store the primary vptr first. Copy its representation
    // without accessing the object through an incompatible pointer type.
    std::memcpy(&vtable, static_cast<const void*>(module), sizeof(vtable));
    require(::dladdr(vtable[0], &library) != 0 && library.dli_fname, "database implementation DSO address unavailable");
    require(
        std::filesystem::canonical(library.dli_fname) == expected_dso, "database implementation came from another DSO");
    std::cout << "Database implementation DSO: " << library.dli_fname << '\n';
#else
    std::cout << "Database plugin requested through ark loader (native address probe unavailable): " << expected_dso
              << '\n';
#endif
}

} // namespace

int main(int argc, char** argv) {
    require(argc == 2, "expected installed plugin directory");
    const auto plugin_directory = std::filesystem::canonical(argv[1]);
#ifdef _DEBUG
    constexpr auto mysql_filename = "ark_mysql_plugin_d" ARK_DATABASE_TEST_LIBRARY_SUFFIX;
    constexpr auto redis_filename = "ark_redis_plugin_d" ARK_DATABASE_TEST_LIBRARY_SUFFIX;
#else
    constexpr auto mysql_filename = "ark_mysql_plugin" ARK_DATABASE_TEST_LIBRARY_SUFFIX;
    constexpr auto redis_filename = "ark_redis_plugin" ARK_DATABASE_TEST_LIBRARY_SUFFIX;
#endif
    require(std::filesystem::is_regular_file(plugin_directory / mysql_filename) &&
                std::filesystem::is_regular_file(plugin_directory / redis_filename),
        "installed database plugins are missing; match CMAKE_BUILD_TYPE to the producer configuration");
    const auto mysql_dso = std::filesystem::canonical(plugin_directory / mysql_filename);
    const auto redis_dso = std::filesystem::canonical(plugin_directory / redis_filename);
    ark::logger log;
    ark::logger::init(&log);
    log.set_log_func([](spdlog::level::level_enum, const std::string& line) { std::cout << line << std::endl; });
    ark::plugin_manager manager;
    require(manager.start(ark::date_time::get_now_time(), plugin_directory.string() + "/", "",
                {{"ark_mysql_plugin", ""}, {"ark_redis_plugin", ""}}),
        "installed database plugin startup failed: " + manager.get_startup_failure());
    require_implementation(manager.find_module<ark::mysql_module>(), mysql_dso);
    require_implementation(manager.find_module<ark::redis_module>(), redis_dso);
    // No configure/query/command: this covers only the unconfigured initial
    // lifecycle and requires no database endpoint, credentials or network IO.
    require(manager.pending_tasks() == 0 && manager.update(ark::date_time::get_now_time()),
        "unconfigured database lifecycle failed");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!manager.stop_until(ark::date_time::get_now_time(), deadline))
    {
        require(manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::stopping &&
                    manager.get_shutdown_failure().empty() && !manager.get_shutdown_exception() &&
                    std::chrono::steady_clock::now() < deadline,
            "database shutdown failed or reached deadline");
        require(manager.update(ark::date_time::get_now_time()), "database shutdown owner update failed");
        std::this_thread::yield();
    }
    require(manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::stopped &&
                manager.get_shutdown_failure().empty() && !manager.get_shutdown_exception() &&
                manager.pending_tasks() == 0 && !manager.try_find_module<ark::mysql_module>() &&
                !manager.try_find_module<ark::redis_module>(),
        "database stop/unload did not complete");
    require(manager.stop(ark::date_time::get_now_time()), "database repeated stop failed");
    std::cout << "Database state=stopped; modules removed; plugin unload succeeded; repeated stop passed" << std::endl;
}
