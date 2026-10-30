// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <new>
#include <thread>

#include "../network/doctest.h"

namespace {
thread_local bool reject_allocations{};
thread_local bool reject_submission{};
thread_local unsigned rejected_allocations{};

void before_scheduler_submission() {
    if (reject_submission)
    {
        reject_submission = false;
        reject_allocations = true;
    }
}
} // namespace

// Some standard libraries allocate empty map sentinels. Arm only after batch
// setup so the fault reaches the next real ctpl allocation on every platform.
#define ARK_SCHEDULER_SUBMISSION_TEST_HOOK ::before_scheduler_submission
#include <ark/utility/scheduler.hpp>
#undef ARK_SCHEDULER_SUBMISSION_TEST_HOOK

void* operator new(std::size_t size) {
    if (reject_allocations)
    {
        ++rejected_allocations;
        throw std::bad_alloc{};
    }
    if (auto* memory = std::malloc(size ? size : 1))
    {
        return memory;
    }
    throw std::bad_alloc{};
}

void operator delete(void* memory) noexcept {
    std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept {
    std::free(memory);
}

namespace ark {
struct scheduler_test_access
{
    // The initial production dispatcher has already reached readiness. Reopen
    // only test state, leaving that dispatcher retired, so this thread can run
    // the actual dispatch()/manage_tasks() with deterministic allocation faults.
    static void prepare_manual_dispatch(scheduler& value) {
        std::lock_guard<std::mutex> lock(value.mutex_);
        value.dispatcher_retired_ = false;
        value.stopping_ = false;
    }

    static void insert(scheduler& value, std::shared_ptr<scheduler_task> task) {
        value.add_task(scheduler::clock::time_point::min(), std::move(task));
    }

    static void dispatch(scheduler& value) {
        try
        { value.dispatch(); }
        catch (...)
        {
            reject_allocations = false;
            throw;
        }
    }

    static std::size_t retained(scheduler& value) {
        std::lock_guard<std::mutex> lock(value.mutex_);
        return value.tasks_.size();
    }

    static bool work_retired(scheduler& value) {
        std::lock_guard<std::mutex> lock(value.mutex_);
        return value.outstanding_ == 0 && !value.callback_contexts_;
    }

    static void recover_only_for_test(scheduler& value) {
        decltype(value.tasks_) retired;
        {
            std::lock_guard<std::mutex> lock(value.mutex_);
            retired.swap(value.tasks_);
        }
        retired.clear();
        value.dispatcher_retired_ = true;
        value.dispatcher_retired_.notify_all();
    }
};
} // namespace ark

namespace {
using namespace std::chrono_literals;

template<typename predicate>
bool eventually(predicate&& ready) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!ready())
    {
        if (std::chrono::steady_clock::now() >= deadline)
        {
            return false;
        }
        std::this_thread::yield();
    }
    return true;
}

struct test_recovery
{
    ark::scheduler& scheduler;

    ~test_recovery() {
        reject_allocations = false;
        reject_submission = false;
        // No production retry/recovery is claimed. Retire retained owners only
        // after observing failure with readiness closed and submitted work done.
        if (eventually([&] { return ark::scheduler_test_access::work_retired(scheduler); }))
        {
            ark::scheduler_test_access::recover_only_for_test(scheduler);
        }
    }
};

struct capture
{
    std::atomic<unsigned>& destroyed;

    ~capture() {
        ++destroyed;
    }
};

struct failing_recurrence final : ark::scheduler_task
{
    unsigned phase;

    failing_recurrence(unsigned phase, std::function<void()>&& callback)
        : scheduler_task(std::move(callback), true)
        , phase(phase) {
    }

    clock::time_point get_new_time() const override {
        reject_allocations = true;
        if (phase == 1)
        {
            throw std::bad_alloc{};
        }
        // The next allocation is the production recurrence map's emplace.
        return clock::now() + 24h;
    }
};

void check_failure(unsigned phase) {
    std::atomic<unsigned> destroyed{}, submitted_calls{}, unsubmitted_calls{};
    {
        ark::scheduler scheduler(1);
        scheduler.stop_accepting();
        REQUIRE(eventually([&] { return scheduler.ready_to_shut(); }));
        ark::scheduler_test_access::prepare_manual_dispatch(scheduler);
        test_recovery recover{scheduler};
        if (phase == 0)
        {
            scheduler.in(0ms, [owner = std::make_shared<capture>(destroyed), &submitted_calls] { ++submitted_calls; });
        }
        else
        {
            ark::scheduler_test_access::insert(scheduler,
                std::make_shared<failing_recurrence>(
                    phase, [owner = std::make_shared<capture>(destroyed), &submitted_calls] { ++submitted_calls; }));
        }
        scheduler.in(0ms, [owner = std::make_shared<capture>(destroyed), &unsubmitted_calls] { ++unsubmitted_calls; });
        rejected_allocations = 0;
        reject_allocations = false;
        reject_submission = phase == 0;
        CHECK_THROWS_AS(ark::scheduler_test_access::dispatch(scheduler), std::bad_alloc);
        REQUIRE(eventually([&] { return ark::scheduler_test_access::work_retired(scheduler); }));
        CHECK(rejected_allocations == (phase == 1 ? 0 : 1));
        CHECK(ark::scheduler_test_access::retained(scheduler) == 2);
        CHECK(destroyed == 0);
        CHECK(submitted_calls == (phase == 0 ? 0 : 1));
        CHECK(unsubmitted_calls == 0);
        CHECK_FALSE(scheduler.ready_to_shut());
        CHECK_FALSE(scheduler.pre_shut());
        scheduler.stop_accepting();
        CHECK_FALSE(scheduler.ready_to_shut());
        // Persistent rejection stayed armed through rollback and dispatch's
        // catch. Its exact attempt count proves rollback did not allocate.
    }
    CHECK(destroyed == 2);
}
} // namespace

TEST_CASE("Scheduler submission allocation failure retains every extracted callback owner") {
    check_failure(0);
}

TEST_CASE("Scheduler recurrence calculation failure retains submitted and unsubmitted owners") {
    check_failure(1);
}

TEST_CASE("Scheduler recurrence insertion allocation failure rolls owners back without allocating") {
    check_failure(2);
}
