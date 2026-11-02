// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <new>
#include <utility>

#include "redis/detail/redis_service.hpp"
#if defined(ARKNET_ENABLE_SSL)
#include "mysql/detail/mysql_service.hpp"
#endif

#include "construction_allocation_probe.hpp"

namespace {
using namespace ark_database_allocation_probe;

template<class Service, class Options>
int probe(Options options) {
    // Options, argv and diagnostic state are prepared before arming. Pools stay
    // idle: construction starts a worker but never sends a database request.
    remaining = budget;
    armed = true;
    try
    {
        Service service(std::move(options));
        armed = false;
        if (!service.quiescent() || service.native_retired())
        {
            return 3;
        }
        service.stop_accepting();
        if (!service.logically_drained() || !service.retire_native() || !service.native_retired())
        {
            return 4;
        }
    }
    catch (const std::bad_alloc&)
    {
        const bool persistent = armed && remaining == 0;
        armed = false;
        if (!persistent || budget < 0)
        {
            return 5;
        }
        std::printf("caught %s budget=%ld allocations=%ld\n", protocol, budget, allocations);
        return 0;
    }
    catch (...)
    {
        armed = false;
        return 6;
    }
    std::printf("constructed %s allocations=%ld\n", protocol, allocations);
    return budget < 0 || allocations <= budget ? 0 : 7;
}
} // namespace

int main(int argc, char** argv) {
    using namespace ark_database_allocation_probe;
    if (argc != 3)
    {
        return 2;
    }
    protocol = argv[1];
    char* end{};
    budget = std::strtol(argv[2], &end, 10);
    if (end == argv[2] || *end || budget < -1)
    {
        return 2;
    }
    std::set_terminate([] {
        std::fprintf(
            stderr, "terminate %s budget=%ld allocations=%ld armed=%d\n", protocol, budget, allocations, armed);
        std::_Exit(86);
    });
    if (std::strcmp(protocol, "redis") == 0)
    {
        return probe<ark::detail::redis_service>(arknet::redis::pool_options{});
    }
#if defined(ARKNET_ENABLE_SSL)
    if (std::strcmp(protocol, "redis-tls") == 0)
    {
        return probe<ark::detail::tls_redis_service>(arknet::redis::pool_options{});
    }
    if (std::strcmp(protocol, "mysql") == 0)
    {
        return probe<ark::detail::mysql_service>(arknet::mysql::pool_options{});
    }
#endif
    return 2;
}
