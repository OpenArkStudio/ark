// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

#include <cstddef>
#include <memory>
#include <utility>

namespace ark::detail {

// Owner-thread activity outlives a facade that its own callback may destroy.
struct database_service_activity {
    std::size_t depth{};
    bool native_retired{};

    class guard {
    public:
        explicit guard(std::shared_ptr<database_service_activity> state) noexcept
            : state_(std::move(state)) {
            ++state_->depth;
        }

        guard(const guard&) = delete;
        guard& operator=(const guard&) = delete;

        ~guard() {
            --state_->depth;
        }

    private:
        std::shared_ptr<database_service_activity> state_;
    };
};

} // namespace ark::detail
