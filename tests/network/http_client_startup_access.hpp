// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

// Observe the production resource owner without publishing a test API.
#include "../../src/plugins/net/http_client_module_impl.cpp"

namespace ark {
struct http_client_startup_test_access
{
    static arknet::io_pool& runtime(http_client_module& module) {
        return static_cast<http_client_module_impl&>(module).state_->runtime;
    }

    static std::size_t queued(http_client_module& module) {
        auto state = static_cast<http_client_module_impl&>(module).state_;
        std::lock_guard lock(state->mutex);
        return state->finished.size();
    }

    static std::weak_ptr<void> capsule(http_client_module& module) {
        return static_cast<http_client_module_impl&>(module).state_;
    }
};
} // namespace ark
