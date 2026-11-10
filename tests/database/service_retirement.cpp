// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <atomic>
#include <chrono>
#include <exception>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <utility>

#include "../network/doctest.h"

#include "detail/database_awaitable.hpp"
#include "redis/detail/redis_awaitable.hpp"
#if defined(ARKNET_ENABLE_SSL)
#include "mysql/detail/mysql_awaitable.hpp"
#endif

namespace ark::detail {

struct database_service_test_access {
    template<class service_type>
    static auto activity(service_type& service) {
        return service.activity_;
    }

    template<class service_type>
    static auto pool(service_type& service) {
        return service.pool_;
    }

    template<class service_type, class callback_type>
    static auto reserve(service_type& service, callback_type callback) {
        return service.completions_->admit(1, std::move(callback));
    }

    template<class service_type, class ticket_type>
    static void complete(service_type& service, const ticket_type& ticket) {
        service.completions_->complete(ticket, {});
    }

    template<class service_type, class finalizer_type>
    static bool retire(service_type& service, finalizer_type finalizer) {
        return service.retire_native(std::move(finalizer));
    }

    template<class service_type>
    static auto next_id(service_type& service) {
        std::lock_guard lock(service.requests_->mutex);
        return service.requests_->next_id;
    }
};

} // namespace ark::detail

namespace {

using namespace std::chrono_literals;
using test_access = ark::detail::database_service_test_access;
using activity_type = ark::detail::database_service_activity;

#if defined(ARKNET_ENABLE_SSL)
using service_types =
    std::tuple<ark::detail::redis_service, ark::detail::tls_redis_service, ark::detail::mysql_service>;
#else
using service_types = std::tuple<ark::detail::redis_service>;
#endif

template<class service_type>
struct service_traits {
    using reply_type = arknet::redis::result;

    static auto options() {
        arknet::redis::pool_options value;
        value.connection.host.clear(); // Native validation fails without a socket or DNS operation.
        return value;
    }

    template<class callback_type>
    static auto reject(service_type& service, callback_type completion) {
        return service.start({"PING"}, std::move(completion), 0ms);
    }

    static auto request(service_type& service, ark::detail::database_awaitable<reply_type>& bridge) {
        return ark::detail::redis_command(service, bridge, {"PING"}, 1s);
    }

    static void check_reply(const reply_type& reply) {
        CHECK(reply.error == arknet::redis::client_errc::invalid_command);
    }
};

#if defined(ARKNET_ENABLE_SSL)
template<>
struct service_traits<ark::detail::mysql_service> {
    using reply_type = ark::detail::mysql_service_reply;

    static auto options() {
        arknet::mysql::pool_options value;
        value.connection.host.clear();
        return value;
    }

    template<class callback_type>
    static auto reject(ark::detail::mysql_service& service, callback_type completion) {
        return service.start_query("SELECT 1", std::move(completion), 0ms);
    }

    static auto request(ark::detail::mysql_service& service, ark::detail::database_awaitable<reply_type>& bridge) {
        return ark::detail::mysql_query(service, bridge, "SELECT 1", 1s);
    }

    static void check_reply(const reply_type& reply) {
        CHECK(reply.result.status.code == arknet::mysql::connection_errc::invalid_options);
    }
};
#endif

struct finalizer_failure : std::runtime_error {
    std::shared_ptr<int> origin;

    explicit finalizer_failure(std::shared_ptr<int> value)
        : std::runtime_error("injected native finalizer failure")
        , origin(std::move(value)) {
    }
};

struct callback_capture {
    std::weak_ptr<activity_type> activity;
    unsigned& destroyed;

    callback_capture(std::shared_ptr<activity_type> state, unsigned& count)
        : activity(std::move(state))
        , destroyed(count) {
    }

    ~callback_capture() {
        auto retained = activity.lock();
        CHECK(retained);
        if (retained) {
            CHECK(retained->depth > 0);
        }
        ++destroyed;
    }
};

struct worker_exit_probe {
    std::shared_ptr<std::atomic<bool>> exited;

    ~worker_exit_probe() {
        if (exited) {
            *exited = true;
        }
    }
};

void observe_worker_exit(std::shared_ptr<std::atomic<bool>> exited) {
    thread_local worker_exit_probe probe;
    probe.exited = std::move(exited);
}

struct native_observation {
    std::promise<void> completed;
    std::atomic<unsigned> callbacks{};
    std::atomic<unsigned> captures_destroyed{};
    std::shared_ptr<std::atomic<bool>> worker_exited = std::make_shared<std::atomic<bool>>(false);
};

struct native_capture {
    std::shared_ptr<native_observation> observation;

    explicit native_capture(std::shared_ptr<native_observation> state)
        : observation(std::move(state)) {
    }

    ~native_capture() {
        ++observation->captures_destroyed;
    }
};

} // namespace

TEST_CASE_TEMPLATE_DEFINE(
    "Service retirement requires sealed admission and delivered completions", service_type, retirement_readiness) {
    service_type service(service_traits<service_type>::options());
    CHECK_FALSE(service.logically_drained());
    CHECK_FALSE(service.native_retired());
    arknet::set_last_error(asio::error::already_started);
    CHECK_FALSE(service.retire_native());
    CHECK(arknet::get_last_error() == asio::error::already_started);
    unsigned callbacks{};
    auto ticket = test_access::reserve(service, [&](auto) {
        ++callbacks;
        CHECK_FALSE(service.logically_drained());
        CHECK_FALSE(service.retire_native());
        CHECK_FALSE(service.native_retired());
    });
    REQUIRE(ticket);
    service.stop_accepting();
    CHECK_FALSE(service.logically_drained());
    CHECK_FALSE(service.retire_native());
    test_access::complete(service, ticket);
    CHECK_FALSE(service.logically_drained());
    CHECK_FALSE(service.retire_native());
    CHECK(service.update() == 1);
    CHECK(callbacks == 1);
    CHECK(service.logically_drained());
    CHECK(service.retire_native());
    CHECK(service.native_retired());
    CHECK(service.retire_native());
}

TEST_CASE_TEMPLATE_APPLY(retirement_readiness, service_types);

TEST_CASE_TEMPLATE_DEFINE("Inline refusal may destroy its facade before captures and activity retire", service_type,
    retirement_inline_destruction) {
    using traits = service_traits<service_type>;
    auto owner = std::make_unique<service_type>(traits::options());
    auto pool = test_access::pool(*owner);
    std::weak_ptr activity = test_access::activity(*owner);
    owner->stop_accepting();
    unsigned callbacks{};
    unsigned destroyed{};
    auto capture = std::make_shared<callback_capture>(test_access::activity(*owner), destroyed);
    auto completion = [&, capture = std::move(capture)](auto) {
        ++callbacks;
        CHECK_FALSE(owner->logically_drained());
        CHECK_FALSE(owner->retire_native());
        owner.reset();
        auto retained = activity.lock();
        CHECK(retained);
        if (retained) {
            CHECK(retained->depth > 0);
        }
    };
    SUBCASE("Normal return") {
        CHECK(traits::reject(*owner, std::move(completion)) == 0);
    }
    SUBCASE("Business callback throws after destruction") {
        CHECK_THROWS_AS(traits::reject(*owner,
                            [completion = std::move(completion)](auto result) mutable {
                                completion(std::move(result));
                                throw std::runtime_error("expected inline callback failure");
                            }),
            std::runtime_error);
    }
    CHECK_FALSE(owner);
    CHECK(callbacks == 1);
    CHECK(destroyed == 1);
    CHECK(activity.expired());
    CHECK(pool->close_and_wait());
}

TEST_CASE_TEMPLATE_APPLY(retirement_inline_destruction, service_types);

TEST_CASE_TEMPLATE_DEFINE("Dispatch may destroy its facade while its shared activity remains pinned", service_type,
    retirement_dispatch_destruction) {
    auto owner = std::make_unique<service_type>(service_traits<service_type>::options());
    auto pool = test_access::pool(*owner);
    std::weak_ptr activity = test_access::activity(*owner);
    unsigned destroyed{};
    auto capture = std::make_shared<callback_capture>(test_access::activity(*owner), destroyed);
    auto ticket = test_access::reserve(*owner, [&, capture = std::move(capture)](auto) {
        CHECK_FALSE(owner->logically_drained());
        CHECK_FALSE(owner->retire_native());
        owner.reset();
        auto retained = activity.lock();
        CHECK(retained);
        if (retained) {
            CHECK(retained->depth > 0);
        }
    });
    REQUIRE(ticket);
    owner->stop_accepting();
    test_access::complete(*owner, ticket);
    CHECK(owner->update() == 1);
    CHECK_FALSE(owner);
    CHECK(destroyed == 1);
    CHECK(activity.expired());
    CHECK(pool->close_and_wait());
}

TEST_CASE_TEMPLATE_APPLY(retirement_dispatch_destruction, service_types);

TEST_CASE_TEMPLATE_DEFINE("Dispatch exception unwinds service activity without closing the pool", service_type,
    retirement_dispatch_exception) {
    service_type service(service_traits<service_type>::options());
    unsigned destroyed{};
    auto capture = std::make_shared<callback_capture>(test_access::activity(service), destroyed);
    auto ticket = test_access::reserve(service, [&, capture = std::move(capture)](auto) {
        CHECK_FALSE(service.logically_drained());
        CHECK_FALSE(service.retire_native());
        throw std::runtime_error("expected dispatch failure");
    });
    REQUIRE(ticket);
    service.stop_accepting();
    test_access::complete(service, ticket);
    CHECK_THROWS_AS(service.update(), std::runtime_error);
    CHECK(destroyed == 1);
    CHECK(test_access::activity(service)->depth == 0);
    CHECK(service.logically_drained());
    CHECK_FALSE(service.native_retired());
    CHECK(service.retire_native());
}

TEST_CASE_TEMPLATE_APPLY(retirement_dispatch_exception, service_types);

TEST_CASE_TEMPLATE_DEFINE("Failed native finalization preserves the exact pool owner and exception", service_type,
    retirement_failure_retention) {
    service_type service(service_traits<service_type>::options());
    std::weak_ptr pool = test_access::pool(service);
    service.stop_accepting();
    unsigned called{};
    CHECK_FALSE(test_access::retire(service, [&](auto& retained) {
        ++called;
        CHECK(&retained == pool.lock().get());
        CHECK_FALSE(service.logically_drained());
        CHECK_FALSE(service.retire_native());
        arknet::set_last_error(asio::error::operation_not_supported);
        return false;
    }));
    CHECK(called == 1);
    CHECK(arknet::get_last_error() == asio::error::operation_not_supported);
    CHECK_FALSE(pool.expired());
    CHECK_FALSE(service.native_retired());
    CHECK(service.logically_drained());
    auto origin = std::make_shared<int>(42);
    auto failure = std::make_exception_ptr(finalizer_failure(origin));
    bool observed{};
    try {
        test_access::retire(service, [&](auto&) -> bool { std::rethrow_exception(failure); });
    } catch (const finalizer_failure& error) {
        observed = true;
        CHECK(error.origin == origin);
        CHECK(std::string(error.what()) == "injected native finalizer failure");
    } catch (...) {
        FAIL("Native finalization changed the exception type");
    }
    CHECK(observed);
    CHECK_FALSE(pool.expired());
    CHECK_FALSE(service.native_retired());
    CHECK(service.logically_drained());
    // The false/throw seam above does not impersonate a native pool success.
    // The real owned pool remains available for its actual permanent barrier.
    CHECK(service.retire_native());
    CHECK(service.native_retired());
}

TEST_CASE_TEMPLATE_APPLY(retirement_failure_retention, service_types);

TEST_CASE_TEMPLATE_DEFINE(
    "Actual owned pool retirement completes native captures and worker exit", service_type, retirement_native_barrier) {
    service_type service(service_traits<service_type>::options());
    auto pool = test_access::pool(service);
    auto observation = std::make_shared<native_observation>();
    auto ready = observation->completed.get_future();
    auto capture = std::make_shared<native_capture>(observation);
    // Negative acquire is posted to the real pool actor, but rejects before
    // connect. It needs no protocol endpoint, database, DNS or TLS handshake.
    pool->async_acquire(
        [observation, capture = std::move(capture)](auto result) {
            CHECK_FALSE(bool(result));
            observe_worker_exit(observation->worker_exited);
            ++observation->callbacks;
            observation->completed.set_value();
        },
        -1ms);
    CHECK(ready.wait_for(5s) == std::future_status::ready);
    CHECK_FALSE(observation->worker_exited->load());
    service.stop_accepting();
    CHECK(service.logically_drained());
    CHECK(service.retire_native());
    CHECK(service.native_retired());
    CHECK(observation->callbacks == 1);
    CHECK(observation->captures_destroyed == 1);
    CHECK(observation->worker_exited->load());
    unsigned refused{};
    CHECK(pool->async_acquire([&](auto result) {
        CHECK_FALSE(bool(result));
        ++refused;
    }) == 0);
    CHECK(refused == 1);
}

TEST_CASE_TEMPLATE_APPLY(retirement_native_barrier, service_types);

TEST_CASE_TEMPLATE_DEFINE("Accepted coroutine A then B survives root admission stop before service sealing",
    service_type, retirement_sequential_workflow) {
    using traits = service_traits<service_type>;
    service_type service(traits::options());
    auto runtime = std::make_shared<arknet::manual_runtime>();
    ark::task_group tasks(runtime, 1);
    ark::detail::database_awaitable<typename traits::reply_type> bridge(runtime, 1);
    unsigned replies{};
    std::exception_ptr error;
    auto sequence = [&]() -> ark::task<> {
        auto first = co_await traits::request(service, bridge);
        traits::check_reply(first);
        ++replies;
        auto second = co_await traits::request(service, bridge);
        traits::check_reply(second);
        ++replies;
    };
    REQUIRE(tasks.start(sequence(), [&](std::exception_ptr failure) { error = std::move(failure); }));
    tasks.poll();
    tasks.stop_accepting();
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (tasks.pending() && std::chrono::steady_clock::now() < deadline) {
        service.update();
        bridge.dispatch();
        tasks.poll();
        std::this_thread::yield();
    }
    CHECK(tasks.pending() == 0);
    CHECK_FALSE(error);
    CHECK(replies == 2);
    CHECK(test_access::next_id(service) == 3); // Both operations were admitted, not ID-zero refusals.
    CHECK(bridge.quiescent());
    CHECK_FALSE(service.logically_drained()); // Service admission has intentionally stayed open.
    service.stop_accepting();
    CHECK(service.logically_drained());
    CHECK(service.retire_native());
    CHECK(service.native_retired());
}

TEST_CASE_TEMPLATE_APPLY(retirement_sequential_workflow, service_types);
