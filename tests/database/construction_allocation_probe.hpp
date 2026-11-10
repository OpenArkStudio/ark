// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

// Include only in the executable driver translation unit; module sources use
// these replacement allocation functions through the executable linker.

#include <cstddef>
#include <cstdlib>
#include <new>

#if defined(_WIN32)
#include <malloc.h>
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

namespace ark_database_allocation_probe {
// Only the constructing thread fails; the real native worker remains operational.
thread_local bool armed{};
thread_local long remaining{-1};
thread_local long allocations{};
const char* protocol{};
long budget{};

void before_allocation() {
    if (armed) {
        if (remaining == 0) {
            throw std::bad_alloc();
        }
        if (remaining > 0) {
            --remaining;
        }
        ++allocations;
    }
}

} // namespace ark_database_allocation_probe

void* operator new(std::size_t size) {
    ark_database_allocation_probe::before_allocation();
    if (void* pointer = std::malloc(size ? size : 1)) {
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
    ark_database_allocation_probe::before_allocation();
    void* pointer{};
#if defined(_WIN32)
    pointer = _aligned_malloc(size ? size : 1, static_cast<std::size_t>(alignment));
#else
    if (posix_memalign(&pointer, static_cast<std::size_t>(alignment), size ? size : 1) != 0) {
        pointer = nullptr;
    }
#endif
    if (!pointer) {
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
    try {
        return ::operator new(size);
    } catch (...) {
        return nullptr;
    }
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
    try {
        return ::operator new(size, alignment);
    } catch (...) {
        return nullptr;
    }
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
