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
public:
    http_server_module_impl();
    ~http_server_module_impl() override;

    bool init() override;
    bool ready_to_shut() const override;
    bool pre_shut() override;

    bool start_server() override;
    bool start_server(std::string const& ip, uint16_t port = 8080, uint8_t thread_num = 1) override;

    arknet::http_router& get_router() override {
        return router_;
    }

protected:
    bool load_config();
    std::string http_ip_{"0.0.0.0"};
    uint16_t http_port_{9090};
    uint8_t http_thread_num_{1};

private:
    struct callback_state;
    std::shared_ptr<callback_state> callbacks_;
    arknet::http_router router_;
    std::unique_ptr<arknet::http_server> server_;
    std::string listen_key_;
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
};

http_server_module_impl::http_server_module_impl()
    : callbacks_(std::make_shared<callback_state>()) {
}

http_server_module_impl::~http_server_module_impl() {
    pre_shut();
}

bool http_server_module_impl::init() {
    auto ret = load_config();
    ARK_ASSERT_RET_VAL(ret, false);

    return true;
}

bool http_server_module_impl::ready_to_shut() const {
    std::lock_guard lock(callbacks_->mutex);
    // Resource retirement starts only after managed frames have completed. Seal
    // native route dispatch before observing zero in-flight callback/send leases.
    callbacks_->closing = true;
    return callbacks_->pending == 0;
}

bool http_server_module_impl::pre_shut() {
    {
        std::lock_guard lock(callbacks_->mutex);
        callbacks_->closing = true;
    }
    if (server_)
    {
        server_->stop();
        server_.reset();
    }
    router_ = {};
    release_endpoint(listen_key_);

    return true;
}

bool http_server_module_impl::start_server() {
    return start_server(http_ip_, http_port_, http_thread_num_);
}

bool http_server_module_impl::start_server(
    std::string const& ip /* = "0.0.0.0"*/, uint16_t port /* = 8080 */, uint8_t thread_num /* = 1*/) {
    {
        std::lock_guard lock(callbacks_->mutex);
        if (callbacks_->closing)
        {
            if (server_ || callbacks_->pending != 0)
            {
                return false;
            }
            // An explicit restart after pre_shut uses the caller's new router.
            callbacks_->closing = false;
        }
    }
    if (server_ && server_->is_started())
    {
        return false;
    }
    endpoint_reservation reservation(port == 0 ? std::string{} : endpoint_key(ip, port));
    if (!reservation.is_reserved())
    {
        return false;
    }
    auto server = std::make_unique<arknet::http_server>(
        arknet::detail::http_frame_size, arknet::detail::max_buffer_size, std::max<unsigned>(thread_num, 1));
    server->bind_recv([this](std::shared_ptr<arknet::http_session>& session, arknet::http_route_request& request) {
        auto lease = std::make_shared<callback_state::request_lease>(callbacks_);
        if (!lease->acquired)
        {
            session->stop();
            return;
        }
        auto response = router_(request);
        response.set(arknet::http::field::server, "ARK");
        session->async_send(std::move(response), [lease](const arknet::error_code&, std::size_t) {});
    });
    if (!server->start(ip, port))
    {
        return false;
    }
    server_ = std::move(server);
    reservation.transfer_to(listen_key_);
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

std::unique_ptr<http_server_module> make_http_server_module() {
    return std::make_unique<http_server_module_impl>();
}

} // namespace detail

} // namespace ark
