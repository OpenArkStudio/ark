// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <utility>

#include "redis/redis_plugin.hpp"
#if defined(ARKNET_ENABLE_SSL)
#include "mysql/mysql_plugin.hpp"
#endif

#include "construction_allocation_probe.hpp"

namespace {
using namespace ark_database_allocation_probe;

template<class Module, class Options>
bool configure(Module& module, const std::string& name, const Options& options) {
    return module.configure(name, options);
}

template<class Module, class Options>
int probe(std::unique_ptr<Module> module, Options options, bool populated) {
    ark::plugin_manager manager;
    module->set_plugin_manager(&manager);
    if (!module->init()) {
        return 3;
    }
    if (populated) {
        // Keep an existing live connection across the failed insertion/retry.
        if (!configure(*module, "existing", options)) {
            return 3;
        }
    }
    // Own every argument before arming; passing caller lvalues would inject
    // into their copies before configure is entered, rather than its ownership.
    std::string name(160, 'n');
    auto attempted_options = options;
    bool caught{};
    remaining = budget;
    armed = true;
    try {
        if (!module->configure(std::move(name), std::move(attempted_options))) {
            armed = false;
            return 4;
        }
        armed = false;
    } catch (const std::bad_alloc&) {
        const bool persistent = armed && remaining == 0;
        armed = false;
        if (!persistent || budget < 0) {
            return 5;
        }
        caught = true;
    } catch (...) {
        armed = false;
        return 6;
    }
    name.assign(160, 'n');
    if (caught && !configure(*module, name, options)) {
        return 7; // A failed configure must release its name for retry.
    }
    if (!configure(*module, "healthy-fallback", options)) {
        return 8;
    }
    // Duplicate rejection must preserve the installed connection, without
    // attempting even the first allocation under persistent failure.
    auto duplicate_options = options;
    remaining = 0;
    armed = true;
    const bool duplicate = module->configure(std::move(name), std::move(duplicate_options));
    armed = false;
    if (duplicate || !module->ready_to_shut() || !module->pre_shut() || !module->shut()) {
        return 9;
    }
    if (caught) {
        std::printf("caught %s budget=%ld allocations=%ld\n", protocol, budget, allocations);
    } else {
        std::printf("constructed %s allocations=%ld\n", protocol, allocations);
    }
    return budget < 0 || allocations <= budget ? 0 : 10;
}
} // namespace

int main(int argc, char** argv) {
    using namespace ark_database_allocation_probe;
    if (argc != 3 && argc != 4) {
        return 2;
    }
    protocol = argv[1];
    char* end{};
    budget = std::strtol(argv[2], &end, 10);
    if (end == argv[2] || *end || budget < -1 || (argc == 4 && std::strcmp(argv[3], "populated") != 0)) {
        return 2;
    }
    std::set_terminate([] {
        std::fprintf(
            stderr, "terminate %s budget=%ld allocations=%ld armed=%d\n", protocol, budget, allocations, armed);
        std::_Exit(86);
    });
    if (std::strcmp(protocol, "redis") == 0 || std::strcmp(protocol, "redis-tls") == 0) {
        ark::redis_options options;
        options.tls = std::strcmp(protocol, "redis-tls") == 0;
        return probe(ark::detail::make_redis_module(), std::move(options), argc == 4);
    }
#if defined(ARKNET_ENABLE_SSL)
    if (std::strcmp(protocol, "mysql") == 0) {
        return probe(ark::detail::make_mysql_module(), ark::mysql_options{}, argc == 4);
    }
#endif
    return 2;
}
