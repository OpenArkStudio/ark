/*
 * This source file is part of ARK
 * For the latest info, see https://github.com/ArkNX
 *
 * Copyright (c) 2013-2020 ArkNX authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
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

#include <chrono>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

#include <arknet/base/io_pool.hpp>
#include <arknet/http/request.hpp>

#include "ark/module_base.hpp"
#include "http_client_factory.hpp"

namespace ark {

class http_client_module_impl final : public module_base<http_client_module>
{
public:
    http_client_module_impl();
    ~http_client_module_impl() override;

    bool update() override;
    bool stop_accepting() override;
    bool ready_to_shut() const override;
    bool pre_shut() override;

    std::future<std::pair<bool, std::string>> async_request(arknet::http::verb method, const std::string& host,
        const uint16_t port, const std::string& url, const std::map<std::string, std::string>* url_params = nullptr,
        const std::vector<std::string>* cookies = nullptr, const std::string* http_body = nullptr,
        const std::string* content_type = nullptr, int io_timeout = 20, int connect_timeout = 5) override {
        return async_request(method, host, std::to_string(port), url, url_params, cookies, http_body, content_type,
            io_timeout, connect_timeout);
    }

    std::future<std::pair<bool, std::string>> get(const std::string& host, const uint16_t port, const std::string& url,
        const std::map<std::string, std::string>* url_params = nullptr,
        const std::vector<std::string>* cookies = nullptr, int io_timeout = 20, int connect_timeout = 5) override {
        return async_request(arknet::http::verb::get, host, std::to_string(port), url, url_params, cookies, nullptr,
            nullptr, io_timeout, connect_timeout);
    }

    std::future<std::pair<bool, std::string>> post(const std::string& host, const uint16_t port, const std::string& url,
        const std::map<std::string, std::string>* url_params = nullptr, const std::string* http_body = nullptr,
        const std::string* content_type = nullptr, const std::vector<std::string>* cookies = nullptr,
        int io_timeout = 20, int connect_timeout = 5) override {
        return async_request(arknet::http::verb::post, host, std::to_string(port), url, url_params, cookies, http_body,
            content_type, io_timeout, connect_timeout);
    }

    void post(std::function<void(std::pair<bool, std::string>)> cb, const std::string& host, const uint16_t port,
        const std::string& url, const std::map<std::string, std::string>* url_params = nullptr,
        const std::string* http_body = nullptr, const std::string* content_type = nullptr,
        const std::vector<std::string>* cookies = nullptr, int io_timeout = 20, int connect_timeout = 5) override {
        async_request(std::move(cb), arknet::http::verb::post, host, std::to_string(port), url, url_params, cookies,
            http_body, content_type, io_timeout, connect_timeout);
    }

    void post(std::function<void(std::pair<bool, std::string>)> cb, const std::string& host, const std::string& service,
        const std::string& url, const std::map<std::string, std::string>* url_params = nullptr,
        const std::string* http_body = nullptr, const std::string* content_type = nullptr,
        const std::vector<std::string>* cookies = nullptr, int io_timeout = 20, int connect_timeout = 5) override {
        async_request(std::move(cb), arknet::http::verb::post, host, service, url, url_params, cookies, http_body,
            content_type, io_timeout, connect_timeout);
    }

private:
    std::future<std::pair<bool, std::string>> async_request(arknet::http::verb method, const std::string& host,
        const std::string& service, const std::string& target,
        const std::map<std::string, std::string>* params = nullptr, const std::vector<std::string>* cookies = nullptr,
        const std::string* http_body = nullptr, const std::string* content_type = nullptr, int io_timeout = 20,
        int connect_timeout = 5);

    void async_request(std::function<void(std::pair<bool, std::string>)> cb, arknet::http::verb method,
        const std::string& host, const std::string& service, const std::string& target,
        const std::map<std::string, std::string>* params = nullptr, const std::vector<std::string>* cookies = nullptr,
        const std::string* http_body = nullptr, const std::string* content_type = nullptr, int io_timeout = 20,
        int connect_timeout = 5);

private:
    bool is_continuation() const;

    struct state;
    std::shared_ptr<state> state_;
    const std::thread::id owner_ = std::this_thread::get_id();
};

namespace {

using http_result = std::pair<bool, std::string>;
using http_callback = std::function<void(http_result)>;

std::string encode_parameter(const std::string& text) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string encoded;
    for (unsigned char character : text)
    {
        if ((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') || character == '-' || character == '_' || character == '.' ||
            character == '~')
        {
            encoded += static_cast<char>(character);
        }
        else
        {
            encoded += '%';
            encoded += hex[character >> 4];
            encoded += hex[character & 15];
        }
    }
    return encoded;
}

arknet::http_request::request_type make_request(arknet::http::verb method, const std::string& target,
    const std::map<std::string, std::string>* params, const std::vector<std::string>* cookies, const std::string* body,
    const std::string* content_type) {
    std::string url = target.empty() ? "/" : target;
    if (params)
    {
        for (const auto& [key, value] : *params)
        {
            if (url.back() != '?' && url.back() != '&')
            {
                url += url.find('?') == std::string::npos ? '?' : '&';
            }
            url += encode_parameter(key) + "=" + encode_parameter(value);
        }
    }
    arknet::http_request::request_type request(method, url, 11);
    request.set(arknet::http::field::user_agent, "ARK");
    if (cookies)
    {
        std::string cookie;
        for (const auto& value : *cookies)
        {
            if (!cookie.empty())
            {
                cookie += "; ";
            }
            cookie += value;
        }
        request.set(arknet::http::field::cookie, cookie);
    }
    if (body)
    {
        request.body() = *body;
    }
    if (content_type)
    {
        request.set(arknet::http::field::content_type, *content_type);
    }
    return request;
}

http_result make_result(arknet::error_code error, arknet::http_request::response_type response) {
    if (error)
    {
        return {false, error.message()};
    }
    if (response.result() != arknet::http::status::ok)
    {
        return {false, std::string(response.reason())};
    }
    return {true, std::move(response.body())};
}

} // namespace

struct http_client_module_impl::state : std::enable_shared_from_this<state>
{
    struct callback
    {
        http_callback function;
        http_result result;
        bool accepted;
    };

    static thread_local const state* callback_context;
    static thread_local bool callback_accepted;

    arknet::io_pool runtime{1};
    std::mutex mutex;
    std::unordered_map<std::uint64_t, std::shared_ptr<arknet::http_request>> requests;
    std::deque<callback> finished;
    std::uint64_t next_request = 0;
    bool accepting = true;
    bool stopped = false;
    std::size_t active_callbacks = 0;

    void submit(arknet::http::verb method, const std::string& host, const std::string& service,
        const std::string& target, const std::map<std::string, std::string>* params,
        const std::vector<std::string>* cookies, const std::string* body, const std::string* content_type,
        int io_timeout, int connect_timeout, bool continuation, std::function<void(http_result, bool)> completion) {
        auto message = make_request(method, target, params, cookies, body, content_type);
        arknet::http_request_options options;
        options.io_timeout = std::chrono::seconds(io_timeout);
        options.connect_timeout = std::chrono::seconds(connect_timeout);
        std::unique_lock<std::mutex> lock(mutex);
        if (stopped || (!accepting && !continuation))
        {
            lock.unlock();
            completion(make_result(asio::error::operation_aborted, {}), false);
            return;
        }
        auto operation = std::make_shared<arknet::http_request>(runtime.get_context(0));
        const auto id = ++next_request;
        requests.emplace(id, operation);
        operation->async_request(host, service, std::move(message), options,
            [self = shared_from_this(), id, completion = std::move(completion)](
                arknet::error_code error, arknet::http_request::response_type response) mutable {
                // Keep the request visible until its result is published. In particular,
                // a queued callback must become visible before readiness can succeed.
                completion(make_result(error, std::move(response)), true);
                std::lock_guard<std::mutex> completed_lock(self->mutex);
                self->requests.erase(id);
            });
    }

    void queue_type(http_callback callback, http_result result, bool accepted) {
        std::lock_guard<std::mutex> lock(mutex);
        finished.push_back({std::move(callback), std::move(result), accepted});
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            accepting = false;
            stopped = true;
            for (const auto& [id, operation] : requests)
            {
                operation->cancel();
            }
        }
        runtime.stop();
    }
};

thread_local const http_client_module_impl::state* http_client_module_impl::state::callback_context = nullptr;
thread_local bool http_client_module_impl::state::callback_accepted = false;

http_client_module_impl::http_client_module_impl()
    : state_(std::make_shared<state>()) {
    if (!state_->runtime.start())
    {
        throw arknet::system_error(arknet::get_last_error());
    }
}

http_client_module_impl::~http_client_module_impl() {
    pre_shut();
}

bool http_client_module_impl::pre_shut() {
    auto state = state_;
    state->stop();
    return true;
}

bool http_client_module_impl::stop_accepting() {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->accepting = false;
    return true;
}

bool http_client_module_impl::ready_to_shut() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->requests.empty() && state_->finished.empty() && state_->active_callbacks == 0;
}

bool http_client_module_impl::is_continuation() const {
    if (state::callback_context)
    {
        return state::callback_context == state_.get() && state::callback_accepted;
    }
    auto* manager = get_plugin_manager();
    return manager && std::this_thread::get_id() == owner_ &&
           manager->get_task_runtime()->get_executor().running_in_this_thread();
}

bool http_client_module_impl::update() {
    auto shared = state_;
    std::size_t remaining;
    {
        std::lock_guard<std::mutex> lock(shared->mutex);
        remaining = shared->finished.size();
    }
    while (remaining--)
    {
        state::callback callback;
        {
            std::lock_guard<std::mutex> lock(shared->mutex);
            if (shared->finished.empty())
            {
                break;
            }
            callback = std::move(shared->finished.front());
            shared->finished.pop_front();
            ++shared->active_callbacks;
        }

        struct delivery_guard
        {
            state& shared;
            state::callback& callback;
            const state* previous_context = state::callback_context;
            bool previous_accepted = state::callback_accepted;

            ~delivery_guard() {
                callback.function = {};
                state::callback_context = previous_context;
                state::callback_accepted = previous_accepted;
                std::lock_guard<std::mutex> lock(shared.mutex);
                --shared.active_callbacks;
            }
        } delivery{*shared, callback};

        state::callback_context = shared.get();
        state::callback_accepted = callback.accepted;
        if (callback.function)
        {
            callback.function(std::move(callback.result));
        }
    }
    return true;
}

std::future<http_result> http_client_module_impl::async_request(arknet::http::verb method, const std::string& host,
    const std::string& service, const std::string& target, const std::map<std::string, std::string>* params,
    const std::vector<std::string>* cookies, const std::string* body, const std::string* content_type, int io_timeout,
    int connect_timeout) {
    auto promise = std::make_shared<std::promise<http_result>>();
    auto future = promise->get_future();
    state_->submit(method, host, service, target, params, cookies, body, content_type, io_timeout, connect_timeout,
        is_continuation(),
        [promise = std::move(promise)](http_result result, bool) { promise->set_value(std::move(result)); });
    return future;
}

void http_client_module_impl::async_request(http_callback callback, arknet::http::verb method, const std::string& host,
    const std::string& service, const std::string& target, const std::map<std::string, std::string>* params,
    const std::vector<std::string>* cookies, const std::string* body, const std::string* content_type, int io_timeout,
    int connect_timeout) {
    state_->submit(method, host, service, target, params, cookies, body, content_type, io_timeout, connect_timeout,
        is_continuation(), [state = state_, callback = std::move(callback)](http_result result, bool accepted) mutable {
            state->queue_type(std::move(callback), std::move(result), accepted);
        });
}

namespace detail {

std::unique_ptr<http_client_module> make_http_client_module() {
    return std::make_unique<http_client_module_impl>();
}

} // namespace detail

} // namespace ark
