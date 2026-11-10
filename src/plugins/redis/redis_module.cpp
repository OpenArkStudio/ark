// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>
#include <variant>

#include "ark/module_base.hpp"
#include "redis/detail/redis_awaitable.hpp"
#include "redis/redis_plugin.hpp"

namespace ark {

class redis_module_impl final : public module_base<redis_module> {
public:
    redis_module_impl();
    ~redis_module_impl() override;

    bool init() override;
    bool update() override;
    bool ready_to_shut() const override;
    bool pre_shut() override;
    bool shut() override;

    bool configure(std::string connection, redis_options options) override;
    task<arknet::redis::result> command(std::string connection, arknet::redis::command command,
        std::chrono::milliseconds timeout = std::chrono::seconds(5)) override;
    task<arknet::redis::result> pipeline(std::string connection, std::vector<arknet::redis::command> commands,
        std::chrono::milliseconds timeout = std::chrono::seconds(5)) override;

private:
    struct state;
    std::shared_ptr<state> state_;
    void ensure_state();
};

struct redis_module_impl::state {
    using service_type = std::variant<std::unique_ptr<detail::redis_service>
#if defined(ARKNET_ENABLE_SSL)
        ,
        std::unique_ptr<detail::tls_redis_service>
#endif
        >;

    struct connection {
        detail::database_awaitable<arknet::redis::result> bridge;
        service_type service;

        connection(std::shared_ptr<arknet::manual_runtime> runtime, redis_options options)
            : bridge(std::move(runtime), options.max_operations) {
            if (!options.max_input_bytes) {
                throw std::invalid_argument("Redis input byte limit must be positive");
            }
            if (options.tls) {
#if defined(ARKNET_ENABLE_SSL)
                service = std::make_unique<detail::tls_redis_service>(
                    std::move(options.pool), options.max_operations, options.max_input_bytes);
#else
                throw std::invalid_argument("Redis TLS requires ARKNET_ENABLE_SSL");
#endif
            } else {
                service = std::make_unique<detail::redis_service>(
                    std::move(options.pool), options.max_operations, options.max_input_bytes);
            }
        }

        template<class function_type>
        decltype(auto) visit(function_type&& function) const {
            return std::visit([&](auto& service) -> decltype(auto) { return function(*service); }, service);
        }
    };

    explicit state(std::shared_ptr<arknet::manual_runtime> execution)
        : runtime(std::move(execution)) {
    }

    void check_owner() const {
        if (owner != std::this_thread::get_id()) {
            throw std::logic_error("Redis module requires its owner thread");
        }
    }

    // The non-coroutine public factory has already copied/moved every input.
    // Pin the connection even while this lazy coroutine has not been started.
    static task<arknet::redis::result> await_pinned(
        std::shared_ptr<connection> pinned, task<arknet::redis::result> operation) {
        co_return co_await std::move(operation);
    }

    static task<arknet::redis::result> missing(std::shared_ptr<state> pinned) {
        const auto executor = co_await asio::this_coro::executor;
        if (!detail::is_database_executor(executor, *pinned->runtime)) {
            throw std::logic_error("Redis operation requires its manager task executor");
        }
        pinned->check_owner();
        co_return arknet::redis::result{
            pinned->accepting ? arknet::redis::client_errc::not_connected : arknet::redis::client_errc::closed, {}, {},
            false};
    }

    std::shared_ptr<arknet::manual_runtime> runtime;
    const std::thread::id owner{std::this_thread::get_id()};
    std::unordered_map<std::string, std::shared_ptr<connection>> connections;
    bool accepting{true};
};

redis_module_impl::redis_module_impl() = default;
redis_module_impl::~redis_module_impl() = default;

void redis_module_impl::ensure_state() {
    if (!state_) {
        auto* manager = get_plugin_manager();
        if (!manager) {
            throw std::logic_error("Redis module requires an installed plugin manager");
        }
        state_ = std::make_shared<state>(manager->get_task_runtime());
    }
    state_->check_owner();
}

bool redis_module_impl::init() {
    ensure_state();
    return true;
}

bool redis_module_impl::configure(std::string name, redis_options options) {
    ensure_state();
    if (!state_->accepting || state_->connections.contains(name) ||
        get_plugin_manager()->get_shutdown_state() != plugin_manager::shutdown_state::running) {
        return false;
    }
    if (name.empty()) {
        throw std::invalid_argument("Redis connection name must not be empty");
    }
    // Allocate the destination node and buckets before starting native IO.
    auto [entry, inserted] = state_->connections.try_emplace(std::move(name), nullptr);
    if (!inserted) {
        return false;
    }
    try {
        auto connection = std::make_shared<state::connection>(state_->runtime, std::move(options));
        entry->second = std::move(connection);
    } catch (...) {
        // This unpublished entry is still empty; preserve same-name retry.
        state_->connections.erase(entry);
        throw;
    }
    return true;
}

task<arknet::redis::result> redis_module_impl::command(
    std::string name, arknet::redis::command command, std::chrono::milliseconds timeout) {
    ensure_state();
    const auto found = state_->connections.find(name);
    if (found == state_->connections.end()) {
        return state::missing(state_);
    }
    auto pinned = found->second;
    auto operation = pinned->visit(
        [&](auto& service) { return detail::redis_command(service, pinned->bridge, std::move(command), timeout); });
    return state::await_pinned(std::move(pinned), std::move(operation));
}

task<arknet::redis::result> redis_module_impl::pipeline(
    std::string name, std::vector<arknet::redis::command> commands, std::chrono::milliseconds timeout) {
    ensure_state();
    const auto found = state_->connections.find(name);
    if (found == state_->connections.end()) {
        return state::missing(state_);
    }
    auto pinned = found->second;
    auto operation = pinned->visit(
        [&](auto& service) { return detail::redis_pipeline(service, pinned->bridge, std::move(commands), timeout); });
    return state::await_pinned(std::move(pinned), std::move(operation));
}

bool redis_module_impl::update() {
    if (state_) {
        state_->check_owner();
        for (const auto& [name, connection] : state_->connections) {
            connection->visit([](auto& service) { service.update(); });
            connection->bridge.dispatch();
        }
    }
    return true;
}

bool redis_module_impl::ready_to_shut() const {
    if (state_) {
        state_->check_owner();
        for (const auto& [name, connection] : state_->connections) {
            if (!connection->bridge.quiescent() ||
                !connection->visit([](const auto& service) { return service.quiescent(); })) {
                return false;
            }
        }
    }
    return true;
}

bool redis_module_impl::pre_shut() {
    if (!ready_to_shut()) {
        return false;
    }
    if (state_) {
        state_->accepting = false;
        for (const auto& [name, connection] : state_->connections) {
            connection->bridge.stop_accepting();
            connection->visit([](auto& service) { service.stop_accepting(); });
        }
        for (const auto& [name, connection] : state_->connections) {
            if (!connection->visit([](auto& service) { return service.retire_native(); })) {
                return false;
            }
        }
    }
    return true;
}

bool redis_module_impl::shut() {
    if (state_) {
        state_->check_owner();
        if (state_->accepting) {
            return false;
        }
        for (const auto& [name, connection] : state_->connections) {
            if (!connection->visit([](const auto& service) { return service.native_retired(); })) {
                return false;
            }
        }
        // Keep sealed connection state pinned by unstarted factory results.
        state_->connections.clear();
    }
    return true;
}

namespace detail {

std::unique_ptr<redis_module> make_redis_module() {
    return std::make_unique<redis_module_impl>();
}

} // namespace detail

} // namespace ark
