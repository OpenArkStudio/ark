// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <new>
#include <utility>

#if defined(_WIN32)
#include <malloc.h>
#endif

#include "redis/detail/redis_service.hpp"
#if defined(ARKNET_ENABLE_SSL)
#include "mysql/detail/mysql_service.hpp"
#endif

#if defined(ARK_DATABASE_EXPECT_ASIO_NEW)
#if ARKNET_USE_BOOST_ASIO
#if defined(BOOST_ASIO_HAS_STD_ALIGNED_ALLOC) || defined(BOOST_ASIO_HAS_BOOST_ALIGN) || defined(BOOST_ASIO_MSVC)
#error "This Asio allocation path bypasses global new injection"
#endif
#else
#if defined(ASIO_HAS_STD_ALIGNED_ALLOC) || defined(ASIO_HAS_BOOST_ALIGN) || defined(ASIO_MSVC)
#error "This Asio allocation path bypasses global new injection"
#endif
#endif
#endif

namespace {
// Only the constructing thread fails; the real native worker remains operational.
thread_local bool armed{};
thread_local long remaining{-1};
thread_local long allocations{};
const char* protocol{};
long budget{};

void before_allocation() {
    if (armed)
    {
        if (remaining == 0)
        {
            throw std::bad_alloc();
        }
        if (remaining > 0)
        {
            --remaining;
        }
        ++allocations;
    }
}

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

void* operator new(std::size_t size) {
    before_allocation();
    if (void* pointer = std::malloc(size ? size : 1))
    {
        return pointer;
    }
    throw std::bad_alloc();
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

void* operator new(std::size_t size, std::align_val_t alignment) {
    before_allocation();
    void* pointer{};
#if defined(_WIN32)
    pointer = _aligned_malloc(size ? size : 1, static_cast<std::size_t>(alignment));
#else
    if (posix_memalign(&pointer, static_cast<std::size_t>(alignment), size ? size : 1) != 0)
    {
        pointer = nullptr;
    }
#endif
    if (!pointer)
    {
        throw std::bad_alloc();
    }
    return pointer;
}

void* operator new[](std::size_t size, std::align_val_t alignment) {
    return ::operator new(size, alignment);
}

void operator delete(void* pointer, std::align_val_t) noexcept {
#if defined(_WIN32)
    _aligned_free(pointer);
#else
    std::free(pointer);
#endif
}

void operator delete[](void* pointer, std::align_val_t alignment) noexcept {
    ::operator delete(pointer, alignment);
}

void operator delete(void* pointer, std::size_t, std::align_val_t alignment) noexcept {
    ::operator delete(pointer, alignment);
}

void operator delete[](void* pointer, std::size_t, std::align_val_t alignment) noexcept {
    ::operator delete(pointer, alignment);
}

// Keep nothrow replacements in the same allocation family under sanitizers.
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    try
    { return ::operator new(size); }
    catch (...)
    { return nullptr; }
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    return ::operator new(size, std::nothrow);
}

void operator delete(void* pointer, const std::nothrow_t&) noexcept {
    std::free(pointer);
}

void operator delete[](void* pointer, const std::nothrow_t&) noexcept {
    std::free(pointer);
}

void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    try
    { return ::operator new(size, alignment); }
    catch (...)
    { return nullptr; }
}

void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    return ::operator new(size, alignment, std::nothrow);
}

void operator delete(void* pointer, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    ::operator delete(pointer, alignment);
}

void operator delete[](void* pointer, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    ::operator delete(pointer, alignment);
}

int main(int argc, char** argv) {
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
