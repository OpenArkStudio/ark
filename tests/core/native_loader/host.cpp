// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "ark/dynamic_library.hpp"

namespace {

void require(bool condition, const char* message) {
    if (!condition)
    {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

std::string utf8_path(const std::filesystem::path& path) {
    const auto encoded = path.u8string();
    return {encoded.begin(), encoded.end()};
}

void verify_load(const std::string& directory) {
    ark::dynamic_library library("ark_loader_dependent");
    const auto loaded = library.load(directory);
#ifdef ARK_PLATFORM_WIN
    const auto error = GetLastError();
    if (!loaded)
    {
        std::cerr << "Native load failed: " << error << '\n';
    }
#endif
    require(loaded, "Cannot load plugin with a sibling-only dependency");
    const auto probe = reinterpret_cast<int (*)()>(library.get_symbol("ark_loader_dependency_probe"));
    require(probe && probe() == 42, "Sibling dependency or symbol dispatch failed");
    require(library.unload(), "Native library close failed");
    require(library.unload(), "Repeated native library close failed");
}

int run(const std::filesystem::path& executable, const std::filesystem::path& plugin_directory,
    const std::filesystem::path& dependency_path) {
    ark::logger log;
    ark::logger::init(&log);
    log.set_log_func([](spdlog::level::level_enum, const std::string&) {});
    const auto host = std::filesystem::canonical(executable).parent_path();
    const auto plugins = std::filesystem::canonical(plugin_directory);
    const auto dependency = std::filesystem::canonical(dependency_path);
    const auto working = std::filesystem::current_path();
    require(host != plugins && working != plugins, "Host, working and plugin directories must remain separate");
    require(dependency.parent_path() == plugins, "Dependency must be next to the plugin");
    require(!std::filesystem::exists(host / dependency.filename()) &&
                !std::filesystem::exists(working / dependency.filename()),
        "Dependency must not be copied next to the host or into its working directory");
    const auto absolute = utf8_path(plugins) + "/";
    const auto relative = utf8_path(std::filesystem::relative(plugins, working)) + "/./";
    verify_load(absolute);
    verify_load(relative);
#ifdef ARK_PLATFORM_WIN
    ark::dynamic_library missing("ark_loader_missing");
    require(!missing.load(absolute), "Missing native library unexpectedly loaded");
    require(GetLastError() != ERROR_SUCCESS, "Native loader failure lost GetLastError");
    ark::dynamic_library invalid("ark_loader_invalid");
    require(!invalid.load(std::string(1, static_cast<char>(0xff))), "Invalid UTF-8 path unexpectedly loaded");
    require(GetLastError() == ERROR_NO_UNICODE_TRANSLATION, "Path conversion failure lost its native diagnostic");
#endif
    std::cout << "Native loader: separate host/plugin/working directories; absolute and relative paths; "
                 "sibling dependency, symbol and repeated close passed; plugin directory="
              << utf8_path(plugins) << '\n';
    return 0;
}

} // namespace

#if defined(_WIN32) && !defined(__CYGWIN__)
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
#endif
    require(argc == 3, "Expected plugin directory and dependency path");
    return run(std::filesystem::path(argv[0]), std::filesystem::path(argv[1]), std::filesystem::path(argv[2]));
}
