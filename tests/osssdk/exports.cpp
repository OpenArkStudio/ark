// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <cstdlib>
#include <iostream>
#include <string>

#include <ark/dynamic_library.hpp>

namespace {

void require(bool condition, const char* message) {
    if (!condition)
    {
        std::cerr << message << std::endl;
        std::_Exit(1);
    }
}

} // namespace

int main(int argc, char* argv[]) {
    require(argc == 3, "Expected plugin directory and library stem");
    ark::logger log;
    ark::logger::init(&log);
    log.set_log_func([](spdlog::level::level_enum, const std::string&) {});
    ark::dynamic_library library(argv[2]);
    require(library.load(argv[1]), "Cannot load actual OSS SDK shared library");
    require(library.get_symbol("DllEntryPlugin") != nullptr, "Missing explicit DllEntryPlugin export");
    require(library.get_symbol("DllExitPlugin") != nullptr, "Missing explicit DllExitPlugin export");
    require(library.unload(), "Cannot unload OSS SDK shared library");
    std::cout << argv[2] << ": actual DSO load, explicit entry/exit exports and unload passed\n";
}
