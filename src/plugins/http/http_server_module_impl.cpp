/*
 * This source file is part of ARK
 * For the latest info, see https://github.com/ArkNX
 *
 * Copyright (c) 2013-2020 ArkNX authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"),
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <algorithm>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

#include <arknet/http/http_server.hpp>

#include "../detail/plugin_configuration.hpp"
#include "ark/http/http_server_module.hpp"
#include "ark/module_base.hpp"
#include "http_plugin.hpp"

namespace ark {

class http_server_module_impl final : public module_base<http_server_module>
{
    friend struct http_server_startup_test_access;

public:
    explicit http_server_module_impl(plugin_manager* manager);
    ~http_server_module_impl() override;

    bool init() override;
    bool ready_to_shut() const override;
    bool pre_shut() override;

    bool start_server() override;
    bool start_server(std::string const& ip, uint16_t port = 8080, uint8_t thread_num = 1) override;

    arknet::http_router& get_router() override;

protected:
    bool load_config();
    std::string http_ip_{"0.0.0.0"};
    uint16_t http_port_{9090};
    uint8_t http_thread_num_{1};

private:
    struct callback_state;
    struct state;
    struct startup_capsule;
    std::shared_ptr<state> state_;
    std::weak_ptr<startup_capsule> startup_owner_;
};

namespace {

std::mutex http_endpoint_mutex;
std::set<std::string> http_endpoints;

std::string endpoint_key(const std::string& ip, uint16_t port) {
    return ip + '\0' + std::to_string(port);
}

bool reserve_endpoint(const std::string& key) {
    std::lock_guard lock(http_endpoint_mutex);
    return http_endpoints.insert(key).second;
}

void release_endpoint(std::string& key) {
    if (key.empty())
    {
        return;
    }
    std::lock_guard lock(http_endpoint_mutex);
    http_endpoints.erase(key);
    key.clear();
}

class endpoint_reservation final
{
public:
    explicit endpoint_reservation(std::string key)
        : key_(std::move(key))
        , reserved_(key_.empty() || reserve_endpoint(key_)) {
    }

    ~endpoint_reservation() {
        if (reserved_)
        {
            release_endpoint(key_);
        }
    }

    endpoint_reservation(const endpoint_reservation&) = delete;
    endpoint_reservation& operator=(const endpoint_reservation&) = delete;

    bool is_reserved() const noexcept {
        return reserved_;
    }

    void transfer_to(std::string& key) noexcept {
        key_.swap(key);
        reserved_ = false;
    }

private:
    std::string key_;
    bool reserved_;
};

} // namespace

struct http_server_module_impl::callback_state
{
    struct request_lease
    {
        explicit request_lease(std::shared_ptr<callback_state> owner)
            : owner_(std::move(owner)) {
            std::lock_guard lock(owner_->mutex);
            if (!owner_->closing)
            {
                ++owner_->pending;
                acquired = true;
            }
        }

        ~request_lease() {
            if (acquired)
            {
                std::lock_guard lock(owner_->mutex);
                --owner_->pending;
            }
        }

        request_lease(const request_lease&) = delete;
        request_lease& operator=(const request_lease&) = delete;

        bool acquired = false;

    private:
        std::shared_ptr<callback_state> owner_;
    };

    mutable std::mutex mutex;
    bool closing = false;
    std::size_t pending = 0;
    bool exposed = false;
    bool sealed = false;
    arknet::http_router router;
};

struct http_server_module_impl::state
{
    std::shared_ptr<callback_state> callbacks = std::make_shared<callback_state>();

    struct native_owner
    {
        std::unique_ptr<arknet::http_server> server;
        std::string listen_key;
        std::unique_ptr<native_owner> next;
    };

    std::unique_ptr<native_owner> owners;
    native_owner* active{};

    startup_cleanup_result cleanup_failed_startup() {
        std::lock_guard lock(callbacks->mutex);
        // Even a stopped or never-started native endpoint can allocate in its
        // destructor. Its existence is not proof of safe retirement here.
        for (auto* owner = owners.get(); owner; owner = owner->next.get())
        {
            if (owner->server)
            {
                return startup_cleanup_result::unsafe;
            }
        }
        if (callbacks->exposed || callbacks->pending != 0)
        {
            return startup_cleanup_result::unsafe;
        }
        callbacks->sealed = true;
        callbacks->closing = true;
        return startup_cleanup_result::done;
    }
};

struct http_server_module_impl::startup_capsule
{
    std::shared_ptr<state> resource;
};

http_server_module_impl::http_server_module_impl(plugin_manager* manager)
    : state_(std::make_shared<state>()) {
    if (manager)
    {
        auto capsule = std::make_shared<startup_capsule>(startup_capsule{state_});
        if (!manager->prepare_startup_cleanup("HTTPServer", "ark_http_plugin", capsule, [](void* context) {
                return static_cast<startup_capsule*>(context)->resource->cleanup_failed_startup();
            }))
        {
            throw std::runtime_error("HTTP server startup ownership enrollment rejected");
        }
        startup_owner_ = capsule;
    }
}

http_server_module_impl::~http_server_module_impl() {
    // Failed registration/startup leaves the complete owner with the host.
    // Successful startup disarms enrollment and restores ordinary destruction.
    if (startup_owner_.expired())
    {
        pre_shut();
    }
}

arknet::http_router& http_server_module_impl::get_router() {
    std::lock_guard lock(state_->callbacks->mutex);
    // The mutable router API cannot observe subsequent handler registration.
    // Treat exposing it as possible retained business captures for this attempt.
    state_->callbacks->exposed = true;
    return state_->callbacks->router;
}

bool http_server_module_impl::init() {
    auto ret = load_config();
    ARK_ASSERT_RET_VAL(ret, false);

    return true;
}

bool http_server_module_impl::ready_to_shut() const {
    std::lock_guard lock(state_->callbacks->mutex);
    // Resource retirement starts only after managed frames have completed. Seal
    // native route dispatch before observing zero in-flight callback/send leases.
    state_->callbacks->closing = true;
    return state_->callbacks->pending == 0;
}

bool http_server_module_impl::pre_shut() {
    {
        std::lock_guard lock(state_->callbacks->mutex);
        state_->callbacks->closing = true;
    }
    for (auto* owner = state_->owners.get(); owner; owner = owner->next.get())
    {
        if (owner->server)
        {
            owner->server->stop();
            owner->server.reset();
        }
        release_endpoint(owner->listen_key);
    }
    state_->owners.reset();
    state_->active = nullptr;
    state_->callbacks->router = {};

    return true;
}

bool http_server_module_impl::start_server() {
    return start_server(http_ip_, http_port_, http_thread_num_);
}

bool http_server_module_impl::start_server(
    std::string const& ip /* = "0.0.0.0"*/, uint16_t port /* = 8080 */, uint8_t thread_num /* = 1*/) {
    {
        std::lock_guard lock(state_->callbacks->mutex);
        if (state_->callbacks->sealed)
        {
            return false;
        }
        if (state_->callbacks->closing)
        {
            if (state_->active || state_->callbacks->pending != 0)
            {
                return false;
            }
            // An explicit restart after pre_shut uses the caller's new router.
            state_->callbacks->closing = false;
        }
    }
    if (state_->active)
    {
        return false;
    }
    auto candidate = std::make_unique<state::native_owner>();
    endpoint_reservation reservation(port == 0 ? std::string{} : endpoint_key(ip, port));
    if (!reservation.is_reserved())
    {
        return false;
    }
    candidate->next = std::move(state_->owners);
    state_->owners = std::move(candidate);
    auto& owner = *state_->owners;
    owner.server = std::make_unique<arknet::http_server>(
        arknet::detail::http_frame_size, arknet::detail::max_buffer_size, std::max<unsigned>(thread_num, 1));
    // Commit ownership before binding or start can throw after workers exist.
    reservation.transfer_to(owner.listen_key);
    owner.server->bind_recv([callbacks = state_->callbacks](
                                std::shared_ptr<arknet::http_session>& session, arknet::http_route_request& request) {
        auto lease = std::make_shared<callback_state::request_lease>(callbacks);
        if (!lease->acquired)
        {
            session->stop();
            return;
        }
        auto response = callbacks->router(request);
        response.set(arknet::http::field::server, "ARK");
        session->async_send(std::move(response), [lease](const arknet::error_code&, std::size_t) {});
    });
    if (!owner.server->start(ip, port))
    {
        // Preserve the existing ordinary start-false stop and endpoint reuse.
        // If native stop throws, the complete attempt stays enrolled. Retain
        // even stopped native allocations rather than invoking their destructors
        // while an allocation failure may still be active.
        owner.server->stop();
        release_endpoint(owner.listen_key);
        if (startup_owner_.expired())
        {
            // Standalone use and completed startup retain the original ordinary
            // destruction path; failed attempts must not accumulate forever.
            owner.server.reset();
            auto retired = std::move(state_->owners);
            state_->owners = std::move(retired->next);
        }
        return false;
    }
    state_->active = &owner;
    return true;
}

bool http_server_module_impl::load_config() {
    const auto config = plugin_config_detail::load_http(http_plugin::get_plugin_conf());
    http_ip_ = config.host;
    http_port_ = config.port;
    http_thread_num_ = config.threads;
    return true;
}

namespace detail {

std::unique_ptr<http_server_module> make_http_server_module(plugin_manager* manager) {
    return std::make_unique<http_server_module_impl>(manager);
}

} // namespace detail

} // namespace ark
