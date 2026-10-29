// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <exception>

namespace stream_test {

// Only installed in the isolated, single-threaded allocation executables. No
// C++ allocation or logging is needed if an injected failure hits noexcept code.
class allocation_diagnostic final
{
public:
    allocation_diagnostic(const char* const& phase, const std::size_t& position, const std::size_t& count) noexcept
        : phase_(phase)
        , position_(position)
        , count_(count)
        , previous_context_(active_)
        , previous_handler_(std::set_terminate(terminate)) {
        active_ = this;
    }

    ~allocation_diagnostic() {
        active_ = previous_context_;
        std::set_terminate(previous_handler_);
    }

    allocation_diagnostic(const allocation_diagnostic&) = delete;
    allocation_diagnostic& operator=(const allocation_diagnostic&) = delete;

private:
    [[noreturn]] static void terminate() noexcept {
        if (const auto* context = active_)
        {
            std::fprintf(stderr, "stream allocation terminate: phase=%s fail_at=%zu allocation_count=%zu\n",
                context->phase_, context->position_, context->count_);
            std::fflush(stderr);
            active_ = context->previous_context_;
            // Preserve doctest's existing crash reporting and failure exit.
            if (context->previous_handler_)
            {
                context->previous_handler_();
            }
        }
        std::_Exit(EXIT_FAILURE);
    }

    const char* const& phase_;
    const std::size_t& position_;
    const std::size_t& count_;
    allocation_diagnostic* previous_context_;
    std::terminate_handler previous_handler_;
    inline static allocation_diagnostic* active_{};
};

} // namespace stream_test
