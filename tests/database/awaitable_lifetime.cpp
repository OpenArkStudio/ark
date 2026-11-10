// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "../network/doctest.h"

#include "detail/database_awaitable.hpp"
#include "detail/database_completion_queue.hpp"

namespace ark::detail {

struct database_awaitable_test_access {
    template<class result_type>
    static bool fail_post(database_awaitable<result_type>& bridge) {
        return bridge.state_->schedule(64, [](auto, auto) { throw std::bad_alloc(); });
    }
};

} // namespace ark::detail

namespace {

struct response {
    std::string data;
    bool cancelled{};
    bool outcome_unknown{};
};

using bridge_type = ark::detail::database_awaitable<response>;
using callback_type = std::function<void(std::exception_ptr, response)>;
using ark::detail::module_task;
using ark::detail::module_task_group;

module_task<> collect_one(bridge_type& bridge, callback_type& native, unsigned& received, response& result,
    std::shared_ptr<arknet::manual_runtime> runtime) {
    result = co_await bridge.wait(
        [&](callback_type completion) {
            native = std::move(completion);
            return std::uint64_t{11};
        },
        [](std::uint64_t) {});
    CHECK(runtime->get_executor().running_in_this_thread());
    ++received;
}

} // namespace

TEST_CASE("Awaitable results stay reserved until a bounded executor drain resumes their tasks") {
    auto runtime = std::make_shared<arknet::manual_runtime>();
    module_task_group group(runtime, 2);
    bridge_type bridge(runtime, 2);
    callback_type first;
    callback_type second;
    response first_result;
    response second_result;
    unsigned received{};
    unsigned completed{};
    auto done = [&](std::exception_ptr error) {
        CHECK_FALSE(error);
        ++completed;
    };
    group.start(collect_one(bridge, first, received, first_result, runtime), done);
    group.start(collect_one(bridge, second, received, second_result, runtime), done);
    group.poll();
    REQUIRE(first);
    REQUIRE(second);
    CHECK(bridge.pending() == 2);
    std::thread native([&] {
        first({}, {std::string("first\0binary", 12), false, true});
        second({}, {"second", false, false});
        first({}, {"duplicate", false, false});
    });
    native.join();
    group.poll();
    CHECK(received == 0);
    CHECK(bridge.pending() == 2);
    CHECK(bridge.dispatch(1));
    CHECK_FALSE(bridge.dispatch(1));
    group.poll();
    CHECK(received == 1);
    CHECK(bridge.pending() == 1);
    CHECK(first_result.data == std::string("first\0binary", 12));
    CHECK(first_result.outcome_unknown);
    CHECK(bridge.dispatch(1));
    group.poll();
    CHECK(received == 2);
    CHECK(completed == 2);
    CHECK(second_result.data == "second");
    CHECK(bridge.pending() == 0);
    first({}, {"late duplicate", false, false});
    CHECK_FALSE(bridge.dispatch());
    group.close();
    CHECK(group.closed());
}

TEST_CASE("A failed owner post preserves the owning result and allows exactly one retry delivery") {
    auto runtime = std::make_shared<arknet::manual_runtime>();
    module_task_group group(runtime);
    bridge_type bridge(runtime, 1);
    callback_type native;
    response result;
    unsigned received{};
    group.start(collect_one(bridge, native, received, result, runtime));
    group.poll();
    REQUIRE(native);
    native({}, {"retained after failed post", false, false});
    CHECK_THROWS_AS(ark::detail::database_awaitable_test_access::fail_post(bridge), std::bad_alloc);
    CHECK(bridge.pending() == 1);
    CHECK_FALSE(bridge.quiescent());
    CHECK(received == 0);
    CHECK(bridge.dispatch());
    CHECK_FALSE(bridge.dispatch());
    group.poll();
    CHECK(received == 1);
    CHECK(result.data == "retained after failed post");
    CHECK(bridge.pending() == 0);
    CHECK(bridge.quiescent());
    CHECK(group.pending() == 0);
    group.close();
}

TEST_CASE("A bridge with no reserved result is not quiescent inside its final resumption") {
    auto runtime = std::make_shared<arknet::manual_runtime>();
    module_task_group group(runtime);
    bridge_type bridge(runtime, 1);
    callback_type native;
    bool resumed{};
    auto operation = [&]() -> module_task<> {
        auto result = co_await bridge.wait(
            [&](callback_type completion) {
                native = std::move(completion);
                return std::uint64_t{1};
            },
            [](std::uint64_t) {});
        CHECK(result.data == "terminal");
        CHECK(bridge.pending() == 0);
        CHECK_FALSE(bridge.quiescent());
        resumed = true;
    };
    CHECK(bridge.quiescent());
    REQUIRE(group.start(operation(), [](std::exception_ptr error) { CHECK_FALSE(error); }));
    group.poll();
    REQUIRE(native);
    CHECK_FALSE(bridge.quiescent());
    native({}, {"terminal", false, false});
    CHECK_FALSE(bridge.quiescent());
    REQUIRE(bridge.dispatch());
    CHECK_FALSE(bridge.quiescent());
    group.poll();
    CHECK(resumed);
    CHECK(bridge.pending() == 0);
    CHECK(bridge.quiescent());
    CHECK(group.pending() == 0);
    group.close();
}

TEST_CASE("One reserved operation supports sequential A then B after task admission stops") {
    auto runtime = std::make_shared<arknet::manual_runtime>();
    module_task_group group(runtime);
    bridge_type bridge(runtime, 1);
    ark::detail::completion_queue<response> service(1, 1);
    using ticket = ark::detail::completion_queue<response>::ticket;
    std::vector<ticket> submitted;
    bool inside_source_callback{};
    bool resumed_inside_source{};
    unsigned received{};
    auto start = [&](callback_type completion) {
        auto accepted = service.admit(1, [&, completion = std::move(completion)](response value) mutable {
            inside_source_callback = true;
            completion({}, std::move(value));
            inside_source_callback = false;
        });
        REQUIRE(accepted);
        submitted.push_back(std::move(accepted));
        return static_cast<std::uint64_t>(submitted.size());
    };
    auto sequence = [&]() -> module_task<> {
        auto first = co_await bridge.wait(start, [](std::uint64_t) {});
        resumed_inside_source |= inside_source_callback;
        CHECK(first.data == std::string("A\0", 2));
        ++received;
        auto second = co_await bridge.wait(start, [](std::uint64_t) {});
        resumed_inside_source |= inside_source_callback;
        CHECK(second.data == "B");
        CHECK(first.data == std::string("A\0", 2));
        ++received;
    };
    group.start(sequence(), [](std::exception_ptr error) { CHECK_FALSE(error); });
    group.poll();
    REQUIRE(submitted.size() == 1);
    group.stop_accepting();
    service.complete(submitted[0], {std::string("A\0", 2), false, false});
    CHECK(service.dispatch() == 1);
    CHECK(received == 0);
    bridge.dispatch();
    group.poll();
    REQUIRE(submitted.size() == 2);
    CHECK(received == 1);
    service.complete(submitted[1], {"B", false, false});
    service.dispatch();
    bridge.dispatch();
    group.poll();
    CHECK(received == 2);
    CHECK_FALSE(resumed_inside_source);
    CHECK(bridge.pending() == 0);
    CHECK(group.pending() == 0);
    group.close();
}

TEST_CASE("Cancellation requested before ID publication forwards once and drains the native terminal") {
    auto runtime = std::make_shared<arknet::manual_runtime>();
    module_task_group group(runtime);
    bridge_type bridge(runtime, 1);
    callback_type native;
    std::vector<std::uint64_t> cancellations;
    module_task_group::task_id task{};
    unsigned received{};
    auto operation = [&]() -> module_task<> {
        auto result = co_await bridge.wait(
            [&](callback_type completion) {
                native = std::move(completion);
                CHECK(group.cancel(task));
                CHECK(cancellations.empty());
                return std::uint64_t{42};
            },
            [&](std::uint64_t id) {
                cancellations.push_back(id);
                native({}, {"cancel terminal", true, true});
            });
        CHECK(result.cancelled);
        CHECK(result.outcome_unknown);
        ++received;
    };
    task = group.start(operation(), [](std::exception_ptr error) { CHECK_FALSE(error); });
    group.poll();
    CHECK(cancellations == std::vector<std::uint64_t>{42});
    CHECK(received == 0);
    CHECK(group.cancel(task));
    CHECK(cancellations.size() == 1);
    bridge.dispatch();
    group.poll();
    CHECK(received == 1);
    CHECK(bridge.pending() == 0);
    CHECK(group.pending() == 0);
    group.close();
}

TEST_CASE("Ready results are not rewritten by cancellation and admission stop preserves terminal delivery") {
    auto runtime = std::make_shared<arknet::manual_runtime>();
    module_task_group group(runtime);
    bridge_type bridge(runtime, 1);
    callback_type native;
    response result;
    unsigned received{};
    group.start(collect_one(bridge, native, received, result, runtime));
    group.poll();
    native({}, {"already completed", false, true});
    bridge.stop_accepting();
    group.close();
    CHECK_FALSE(group.closed());
    CHECK(bridge.pending() == 1);
    bridge.dispatch();
    group.poll();
    CHECK(group.closed());
    CHECK(received == 1);
    CHECK_FALSE(result.cancelled);
    CHECK(result.outcome_unknown);
    CHECK(result.data == "already completed");
}

TEST_CASE("A throwing native cancellation request never replaces an accepted operation terminal") {
    bool cancel_before_publication{};
    SUBCASE("Cancellation was requested synchronously during native start") {
        cancel_before_publication = true;
    }
    SUBCASE("Cancellation was requested after native ID publication") {
        cancel_before_publication = false;
    }
    auto runtime = std::make_shared<arknet::manual_runtime>();
    module_task_group group(runtime);
    bridge_type bridge(runtime, 1);
    callback_type native;
    module_task_group::task_id task{};
    unsigned received{};
    unsigned cancellations{};
    auto operation = [&]() -> module_task<> {
        auto result = co_await bridge.wait(
            [&](callback_type completion) {
                native = std::move(completion);
                if (cancel_before_publication) {
                    CHECK(group.cancel(task));
                }
                return std::uint64_t{61};
            },
            [&](std::uint64_t id) {
                CHECK(id == 61);
                ++cancellations;
                throw std::runtime_error("native cancellation submission failed");
            });
        CHECK(result.data == "actual native terminal");
        CHECK_FALSE(result.cancelled);
        CHECK(result.outcome_unknown);
        ++received;
    };
    task = group.start(operation(), [](std::exception_ptr error) { CHECK_FALSE(error); });
    group.poll();
    if (!cancel_before_publication) {
        CHECK(group.cancel(task));
    }
    CHECK(cancellations == 1);
    REQUIRE(bridge.cancellation_exception());
    try {
        std::rethrow_exception(bridge.cancellation_exception());
    } catch (const std::runtime_error& error) {
        CHECK(std::string(error.what()) == "native cancellation submission failed");
    }
    CHECK(bridge.pending() == 1);
    CHECK(group.pending() == 1);
    CHECK(received == 0);
    CHECK_FALSE(bridge.dispatch());
    native({}, {"actual native terminal", false, true});
    bridge.dispatch();
    group.poll();
    CHECK(received == 1);
    CHECK(bridge.pending() == 0);
    CHECK(group.pending() == 0);
    native({}, {"late duplicate", true, false});
    CHECK_FALSE(bridge.dispatch());
    group.close();
}

TEST_CASE("Inline native refusal and initiation exceptions are asynchronously delivered without native success") {
    auto runtime = std::make_shared<arknet::manual_runtime>();
    module_task_group group(runtime);
    bridge_type bridge(runtime, 1);
    unsigned received{};
    bool initiated{};
    bool caught{};
    auto refused = [&]() -> module_task<> {
        auto result = co_await bridge.wait(
            [&](callback_type completion) {
                initiated = true;
                completion({}, {"native queue full", false, false});
                return std::uint64_t{};
            },
            [](std::uint64_t) { FAIL("Rejected native request must not be cancelled"); });
        CHECK(result.data == "native queue full");
        ++received;
    };
    group.start(refused(), [](std::exception_ptr error) { CHECK_FALSE(error); });
    group.poll();
    CHECK(initiated);
    CHECK(received == 0);
    CHECK(bridge.pending() == 1);
    bridge.dispatch();
    group.poll();
    CHECK(received == 1);
    auto throwing = [&]() -> module_task<> {
        try {
            co_await bridge.wait([](callback_type) -> std::uint64_t { throw std::runtime_error("initiation failed"); },
                [](std::uint64_t) {});
            FAIL("A C++ initiation exception must not become a native success");
        } catch (const std::runtime_error& error) {
            caught = std::string(error.what()) == "initiation failed";
        }
    };
    group.start(throwing(), [](std::exception_ptr error) { CHECK_FALSE(error); });
    group.poll();
    CHECK_FALSE(caught);
    CHECK(bridge.pending() == 1);
    bridge.dispatch();
    group.poll();
    CHECK(caught);
    CHECK(bridge.pending() == 0);
    group.close();
}

TEST_CASE("Bridge quota and executor mismatch fail before starting native operations") {
    auto runtime = std::make_shared<arknet::manual_runtime>();
    auto other_runtime = std::make_shared<arknet::manual_runtime>();
    module_task_group group(runtime, 3);
    bridge_type bridge(runtime, 1);
    bridge_type foreign(other_runtime, 1);
    callback_type native;
    response result;
    unsigned received{};
    unsigned extra_starts{};
    unsigned rejected{};
    group.start(collect_one(bridge, native, received, result, runtime));
    group.poll();
    auto full = [&]() -> module_task<> {
        try {
            co_await bridge.wait(
                [&](callback_type) {
                    ++extra_starts;
                    return std::uint64_t{1};
                },
                [](std::uint64_t) {});
        } catch (const std::length_error&) {
            ++rejected;
        }
    };
    auto wrong_executor = [&]() -> module_task<> {
        try {
            co_await foreign.wait(
                [&](callback_type) {
                    ++extra_starts;
                    return std::uint64_t{1};
                },
                [](std::uint64_t) {});
        } catch (const std::logic_error&) {
            ++rejected;
        }
    };
    group.start(full(), [](std::exception_ptr error) { CHECK_FALSE(error); });
    group.start(wrong_executor(), [](std::exception_ptr error) { CHECK_FALSE(error); });
    group.poll();
    CHECK(rejected == 2);
    CHECK(extra_starts == 0);
    CHECK(bridge.pending() == 1);
    CHECK(foreign.pending() == 0);
    native({}, {"first remains valid", false, false});
    bridge.dispatch();
    group.poll();
    CHECK(received == 1);
    bridge.stop_accepting();
    auto closed = [&]() -> module_task<> {
        try {
            co_await bridge.wait(
                [&](callback_type) {
                    ++extra_starts;
                    return std::uint64_t{1};
                },
                [](std::uint64_t) {});
            FAIL("Closed bridge accepted an operation");
        } catch (const std::logic_error&) {
            ++rejected;
        }
    };
    group.start(closed(), [](std::exception_ptr error) { CHECK_FALSE(error); });
    group.poll();
    CHECK(rejected == 3);
    CHECK(extra_starts == 0);
    CHECK(group.pending() == 0);
    group.close();
}

TEST_CASE("Bridge rejects foreign runtime and same-runtime strand before native initiation") {
    auto runtime = std::make_shared<arknet::manual_runtime>();
    auto foreign_runtime = std::make_shared<arknet::manual_runtime>();
    bridge_type bridge(runtime, 1);
    unsigned starts{};
    unsigned completed{};
    auto operation = [&]() -> module_task<> {
        co_await bridge.wait(
            [&](callback_type) {
                ++starts;
                return std::uint64_t{1};
            },
            [](std::uint64_t) {});
    };
    auto rejected = [&](std::exception_ptr error) {
        REQUIRE(error);
        CHECK_THROWS_AS(std::rethrow_exception(error), std::logic_error);
        ++completed;
    };
    asio::co_spawn(foreign_runtime->get_executor(), operation(), rejected);
    asio::co_spawn(asio::make_strand(runtime->get_executor()), operation(), rejected);
    foreign_runtime->poll();
    runtime->poll();
    CHECK(completed == 2);
    CHECK(starts == 0);
    CHECK(bridge.pending() == 0);
    CHECK(bridge.quiescent());
}
