// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <array>
#include <atomic>
#include <chrono>
#include <exception>
#include <future>
#include <stdexcept>
#include <thread>
#include <vector>

#include "../network/doctest.h"

#include <ark/container/queue.hpp>
#include <ark/container/thread_safe_queue.hpp>
#include <ark/lock/rw_lock.hpp>

namespace {

struct throwing_value {
    int value = 0;
    bool throw_copy = false;
    bool throw_assign = false;

    throwing_value() = default;

    explicit throwing_value(int value)
        : value(value) {
    }

    throwing_value(const throwing_value& other)
        : value(other.value) {
        if (other.throw_copy) {
            throw std::runtime_error("copy failed");
        }
    }

    throwing_value& operator=(const throwing_value& other) {
        if (throw_assign) {
            throw std::runtime_error("assignment failed");
        }
        value = other.value;
        return *this;
    }
};

template<typename operation_type>
bool queue_operation_finishes(ark::queue<throwing_value>& values, operation_type operation) {
    std::promise<void> done;
    auto completion = done.get_future();
    std::exception_ptr failure;
    std::thread worker([&] {
        try {
            operation();
        } catch (...) {
            failure = std::current_exception();
        }
        done.set_value();
    });
    const bool finished = completion.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
    if (!finished) {
        // Recover a leaked spin lock so the regression reports a failure instead of hanging the process.
        values.unlock();
    }
    worker.join();
    if (failure) {
        std::rethrow_exception(failure);
    }
    return finished;
}

} // namespace

DOCTEST_TEST_CASE("Thread-safe queue supports const observation and preserves FIFO across batch drains") {
    ark::thread_safe_queue<int> values;
    const auto& observed = values;
    DOCTEST_CHECK(observed.empty());
    DOCTEST_CHECK(values.size() == 0);

    values.push(10);
    values.push(20);
    DOCTEST_CHECK_FALSE(observed.empty());
    DOCTEST_CHECK(values.size() == 2);

    int output = 0;
    DOCTEST_REQUIRE(values.try_pop(output));
    DOCTEST_CHECK(output == 10);
    values.push(30);
    auto batch = values.grab_all();
    DOCTEST_REQUIRE(batch.size() == 2);
    DOCTEST_CHECK(batch.front() == 20);
    batch.pop();
    DOCTEST_CHECK(batch.front() == 30);
    DOCTEST_CHECK(observed.empty());
    DOCTEST_CHECK(values.size() == 0);
    DOCTEST_CHECK_FALSE(values.try_pop(output));
    DOCTEST_CHECK(output == 10);

    values.push(40);
    DOCTEST_REQUIRE(values.try_pop(output));
    DOCTEST_CHECK(output == 40);
    DOCTEST_CHECK(observed.empty());
}

DOCTEST_TEST_CASE("Queue operations release their lock when copying or assigning an element throws") {
    ark::queue<throwing_value> values;
    throwing_value input(42);
    input.throw_copy = true;
    DOCTEST_REQUIRE_THROWS_AS(values.push(input), std::runtime_error);

    input.throw_copy = false;
    DOCTEST_CHECK(queue_operation_finishes(values, [&] { values.push(input); }));
    DOCTEST_REQUIRE(values.count() == 1);

    throwing_value output;
    output.throw_assign = true;
    DOCTEST_REQUIRE_THROWS_AS(values.pop(output), std::runtime_error);
    output.throw_assign = false;
    bool popped = false;
    DOCTEST_CHECK(queue_operation_finishes(values, [&] { popped = values.pop(output); }));
    DOCTEST_REQUIRE(popped);
    DOCTEST_CHECK(output.value == 42);
    DOCTEST_CHECK(values.count() == 0);
    DOCTEST_CHECK_FALSE(values.pop(output));
}

DOCTEST_TEST_CASE("Queue producers consumers and count observers share one synchronization boundary") {
    constexpr int producer_count = 4;
    constexpr int values_per_producer = 400;
    constexpr int total = producer_count * values_per_producer;
    ark::queue<int> values;
    std::array<std::atomic<int>, total> received{};
    std::atomic<int> finished_producers{0};
    std::atomic<int> invalid_values{0};
    std::atomic<bool> invalid_count{false};
    std::vector<std::thread> workers;
    for (int producer = 0; producer < producer_count; ++producer) {
        workers.emplace_back([&, producer] {
            for (int value = 0; value < values_per_producer; ++value) {
                values.push(producer * values_per_producer + value);
            }
            ++finished_producers;
        });
    }
    for (int consumer = 0; consumer < 3; ++consumer) {
        workers.emplace_back([&] {
            while (finished_producers.load() < producer_count || values.count() > 0) {
                int value;
                if (values.pop(value)) {
                    if (value < 0 || value >= total) {
                        ++invalid_values;
                    } else {
                        ++received[value];
                    }
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }
    std::thread observer([&] {
        while (finished_producers.load() < producer_count || values.count() > 0) {
            const int size = values.count();
            if (size < 0 || size > total) {
                invalid_count = true;
            }
        }
    });
    for (auto& worker : workers) {
        worker.join();
    }
    observer.join();
    DOCTEST_CHECK(invalid_values.load() == 0);
    DOCTEST_CHECK_FALSE(invalid_count.load());
    DOCTEST_CHECK(values.count() == 0);
    for (const auto& count : received) {
        DOCTEST_REQUIRE(count.load() == 1);
    }
}

DOCTEST_TEST_CASE("Scoped reader-writer guards release during exception unwinding") {
    ark::reader_writer_lock lock;
    DOCTEST_REQUIRE_THROWS_AS(
        [&] {
            ark::scope_w_lock guard(lock);
            throw std::runtime_error("write failed");
        }(),
        std::runtime_error);
    { ark::scope_r_lock guard(lock); }
    DOCTEST_REQUIRE_THROWS_AS(
        [&] {
            ark::scope_r_lock guard(lock);
            throw std::runtime_error("read failed");
        }(),
        std::runtime_error);
    { ark::scope_w_lock guard(lock); }
}
