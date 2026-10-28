// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

#include <cstddef>
#include <new>

namespace node_copy_hooks {

struct allocation_tag
{
};

inline bool return_null{};
inline std::size_t allocations{};
inline std::size_t releases{};

template<typename T>
void release(T*& pointer) noexcept {
    if (pointer)
    {
        ++releases;
        delete pointer;
        pointer = nullptr;
    }
}

} // namespace node_copy_hooks

inline void* operator new(std::size_t size, node_copy_hooks::allocation_tag) noexcept {
    if (node_copy_hooks::return_null)
    {
        node_copy_hooks::return_null = false;
        return nullptr;
    }
    auto* pointer = ::operator new(size, std::nothrow);
    if (pointer)
    {
        ++node_copy_hooks::allocations;
    }
    return pointer;
}

inline void operator delete(void* pointer, node_copy_hooks::allocation_tag) noexcept {
    ++node_copy_hooks::releases;
    ::operator delete(pointer);
}

// Force-include this header in every TU of the separate hook executable.
#define ARK_NEW new (node_copy_hooks::allocation_tag{})
#define ARK_DELETE(pointer) node_copy_hooks::release(pointer)
