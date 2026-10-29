// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

#include <atomic>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace ark::detail {

// The counter belongs to one loaded implementation copy, not a process-wide or
// durable identity service. Never wrap or recycle an allocated incarnation.
inline std::uint64_t next_incarnation(std::atomic<std::uint64_t>& counter) {
    auto previous = counter.load(std::memory_order_relaxed);
    for (;;)
    {
        if (previous == std::numeric_limits<std::uint64_t>::max())
        {
            throw std::overflow_error("Entity incarnation exhausted; start a new implementation lifetime");
        }
        if (counter.compare_exchange_weak(previous, previous + 1, std::memory_order_relaxed))
        {
            return previous + 1;
        }
    }
}

} // namespace ark::detail
