// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <atomic>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "../network/doctest.h"

#include "detail/database_completion_queue.hpp"

using ark::detail::completion_queue;

TEST_CASE("Queue quiescence includes pending and ready completions") {
    completion_queue<int> queue(1, 1);
    CHECK(queue.quiescent());
    auto pending = queue.admit(1, [&](int value) {
        CHECK(value == 42);
        CHECK_FALSE(queue.quiescent());
    });
    REQUIRE(pending);
    CHECK_FALSE(queue.quiescent());
    queue.complete(pending, 42);
    CHECK_FALSE(queue.quiescent());
    queue.stop_accepting();
    CHECK_FALSE(queue.quiescent());
    CHECK(queue.dispatch() == 1);
    CHECK(queue.quiescent());
}

TEST_CASE("Nested dispatch stays active after a reentrant stop removes all reservations") {
    completion_queue<int> queue(2, 2);
    unsigned callbacks{};
    auto outer = queue.admit(1, [&](int) {
        ++callbacks;
        CHECK_FALSE(queue.quiescent());
        CHECK(queue.dispatch(1) == 1);
        CHECK_FALSE(queue.quiescent());
    });
    auto inner = queue.admit(1, [&](int) {
        ++callbacks;
        queue.stop();
        CHECK_FALSE(queue.quiescent());
    });
    REQUIRE(outer);
    REQUIRE(inner);
    queue.complete(outer, 1);
    queue.complete(inner, 2);
    CHECK(queue.dispatch() == 1);
    CHECK(callbacks == 2);
    CHECK(queue.quiescent());
}

TEST_CASE("Throwing callbacks retire dispatch activity only after unwinding") {
    completion_queue<int> queue(1, 1);
    auto pending = queue.admit(1, [&](int) {
        queue.stop();
        CHECK_FALSE(queue.quiescent());
        throw std::runtime_error("expected business failure");
    });
    REQUIRE(pending);
    queue.complete(pending, 1);
    CHECK_THROWS_AS(queue.dispatch(), std::runtime_error);
    CHECK(queue.quiescent());
}

TEST_CASE("Callback captures are destroyed while dispatch is still active") {
    completion_queue<int> queue(1, 1);
    unsigned destroyed{};
    auto capture = std::shared_ptr<int>(new int, [&](int* value) {
        ++destroyed;
        CHECK_FALSE(queue.quiescent());
        delete value;
    });
    auto pending = queue.admit(1, [&, capture](int) { queue.stop(); });
    REQUIRE(pending);
    capture.reset();
    queue.complete(pending, 1);
    CHECK(queue.dispatch() == 1);
    CHECK(destroyed == 1);
    CHECK(queue.quiescent());
}

TEST_CASE("Response ownership is released while dispatch is still active") {
    completion_queue<std::shared_ptr<int>> queue(1, 1);
    unsigned destroyed{};
    auto response = std::shared_ptr<int>(new int, [&](int* value) {
        ++destroyed;
        CHECK_FALSE(queue.quiescent());
        delete value;
    });
    auto pending = queue.admit(1, [&](std::shared_ptr<int> value) {
        REQUIRE(value);
        queue.stop();
    });
    REQUIRE(pending);
    queue.complete(pending, std::move(response));
    CHECK(queue.dispatch() == 1);
    CHECK(destroyed == 1);
    CHECK(queue.quiescent());
}

TEST_CASE("Completed but undispatched operations keep admission reservations") {
    completion_queue<std::string> queue(2, 12);
    std::vector<std::string> received;
    auto first = queue.admit(8, [&](std::string result) { received.push_back(std::move(result)); });
    REQUIRE(first);
    CHECK_FALSE(queue.admit(5, [](auto) {}));
    auto second = queue.admit(4, [&](std::string result) { received.push_back(std::move(result)); });
    REQUIRE(second);
    queue.complete(first, "first");
    CHECK_FALSE(queue.admit(1, [](auto) {}));
    CHECK(queue.dispatch(1) == 1);
    CHECK(received == std::vector<std::string>{"first"});
    REQUIRE(queue.admit(8, [](auto) {}));
    queue.stop();
}

TEST_CASE("IO producers only deliver business callbacks on the module thread") {
    completion_queue<std::string> queue(16, 128);
    const auto owner = std::this_thread::get_id();
    std::vector<completion_queue<std::string>::ticket> records;
    unsigned callbacks{};
    bool wrong_thread{};
    for (unsigned index = 0; index < 16; ++index) {
        records.push_back(queue.admit(1, [&](std::string result) {
            wrong_thread |= std::this_thread::get_id() != owner;
            CHECK(result == std::string("binary\0value", 12));
            ++callbacks;
        }));
        REQUIRE(records.back());
    }
    std::vector<std::thread> workers;
    for (unsigned lane = 0; lane < 4; ++lane) {
        workers.emplace_back([&, lane] {
            for (unsigned index = lane; index < records.size(); index += 4) {
                queue.complete(records[index], std::string("binary\0value", 12));
                queue.complete(records[index], "duplicate must be ignored");
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }
    CHECK(callbacks == 0);
    CHECK(queue.dispatch(5) == 5);
    CHECK(callbacks == 5);
    CHECK(queue.dispatch() == 11);
    CHECK(callbacks == 16);
    CHECK_FALSE(wrong_thread);
}

TEST_CASE("Throwing business callbacks release reservation and later completions remain usable") {
    completion_queue<int> queue(2, 4);
    auto first = queue.admit(2, [](int) { throw std::runtime_error("consumer failure"); });
    int result{};
    auto second = queue.admit(2, [&](int value) { result = value; });
    queue.complete(first, 1);
    queue.complete(second, 2);
    CHECK_THROWS_AS(queue.dispatch(), std::runtime_error);
    CHECK(queue.dispatch() == 1);
    CHECK(result == 2);
    REQUIRE(queue.admit(4, [](int) {}));
}

TEST_CASE("Stop removes business captures even while native callbacks retain records") {
    completion_queue<int> queue(2, 4);
    auto capture = std::make_shared<int>(1);
    std::weak_ptr<int> observed = capture;
    unsigned callbacks{};
    auto pending = queue.admit(1, [capture, &callbacks](int) { ++callbacks; });
    auto ready = queue.admit(1, [&](int) { ++callbacks; });
    capture.reset();
    REQUIRE_FALSE(observed.expired());
    queue.complete(ready, 2);
    queue.stop();
    CHECK(observed.expired());
    queue.complete(pending, 1);
    CHECK(queue.dispatch() == 0);
    CHECK(callbacks == 0);
    CHECK_FALSE(queue.admit(1, [](int) {}));
    queue.stop();
}

TEST_CASE("Stop admission preserves accepted pending and ready terminal callbacks") {
    completion_queue<int> queue(2, 4);
    std::vector<int> values;
    auto pending = queue.admit(2, [&](int value) { values.push_back(value); });
    auto ready = queue.admit(2, [&](int value) { values.push_back(value); });
    REQUIRE(pending);
    REQUIRE(ready);
    queue.complete(ready, 2);
    queue.stop_accepting();
    CHECK_FALSE(queue.admit(1, [](int) {}));
    std::thread worker([&] { queue.complete(pending, 1); });
    worker.join();
    CHECK(queue.dispatch() == 2);
    CHECK(values == std::vector<int>{2, 1});
    queue.stop_accepting();
    CHECK(queue.dispatch() == 0);
}

TEST_CASE("A callback may stop the service without dispatching following callbacks") {
    completion_queue<int> queue(2, 4);
    unsigned callbacks{};
    auto first = queue.admit(1, [&](int) {
        ++callbacks;
        queue.stop();
    });
    auto second = queue.admit(1, [&](int) { ++callbacks; });
    queue.complete(first, 1);
    queue.complete(second, 2);
    CHECK(queue.dispatch() == 1);
    CHECK(callbacks == 1);
}

TEST_CASE("Only completion production is allowed off the module thread") {
    completion_queue<int> queue(1, 1);
    std::atomic<bool> rejected{};
    std::thread worker([&] {
        try {
            queue.dispatch();
        } catch (const std::logic_error&) {
            rejected = true;
        }
    });
    worker.join();
    CHECK(rejected.load());
    auto abandoned = queue.admit(1, [](int) {});
    REQUIRE(abandoned);
    queue.abandon(abandoned);
    REQUIRE(queue.admit(1, [](int) {}));
}

TEST_CASE("Module dispatch overlaps native completion producers") {
    constexpr unsigned count = 256;
    completion_queue<unsigned> queue(count, count);
    std::vector<completion_queue<unsigned>::ticket> records;
    unsigned callbacks{};
    unsigned total{};
    for (unsigned index = 0; index < count; ++index) {
        records.push_back(queue.admit(1, [&](unsigned value) {
            ++callbacks;
            total += value;
        }));
    }
    std::vector<std::thread> producers;
    for (unsigned lane = 0; lane < 4; ++lane) {
        producers.emplace_back([&, lane] {
            for (unsigned index = lane; index < count; index += 4) {
                queue.complete(records[index], index + 1);
                std::this_thread::yield();
            }
        });
    }
    const auto expires = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (callbacks < count && std::chrono::steady_clock::now() < expires) {
        queue.dispatch(7);
        std::this_thread::yield();
    }
    for (auto& producer : producers) {
        producer.join();
    }
    queue.dispatch(count);
    CHECK(callbacks == count);
    CHECK(total == count * (count + 1) / 2);
}
