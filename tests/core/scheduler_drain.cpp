// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <array>
#include <atomic>
#include <barrier>
#include <chrono>
#include <future>
#include <memory>
#include <thread>
#include <vector>

#include "../network/doctest.h"

#include <ark/plugin_manager.hpp>
#include <ark/utility/scheduler.hpp>
#include "utility_plugin.hpp"

using namespace std::chrono_literals;

namespace ark {
struct scheduler_test_access
{
    static std::size_t outstanding(const scheduler& value) {
        return value.outstanding_.load();
    }

    static std::size_t future_one_shots(scheduler& value, bool advance = false) {
        std::lock_guard<std::mutex> lock(value.mutex_);
        std::size_t count{};
        decltype(value.tasks_) advanced;
        for (auto i = value.tasks_.begin(); i != value.tasks_.end();)
        {
            if (!i->second->recurring)
            {
                ++count;
                if (advance)
                {
                    auto node = value.tasks_.extract(i++);
                    node.key() = scheduler::clock::now();
                    advanced.insert(std::move(node));
                    continue;
                }
            }
            ++i;
        }
        value.tasks_.merge(advanced);
        value.sleeper_.interrupt();
        return count;
    }
};
} // namespace ark

namespace {

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

struct gate
{
    std::promise<void> promise;
    std::shared_future<void> future{promise.get_future()};
    std::atomic<bool> opened{false};

    void open() {
        if (!opened.exchange(true))
        {
            promise.set_value();
        }
    }

    bool wait() const {
        return future.wait_for(5s) == std::future_status::ready;
    }
};

struct release_on_exit
{
    gate& value;

    ~release_on_exit() {
        value.open();
    }
};

struct retiring_capture
{
    ark::scheduler& scheduler;
    gate& entered;
    gate& release;
    std::atomic<bool>& reentered;
    std::atomic<bool>& retired;

    ~retiring_capture() {
        scheduler.stop_accepting();
        reentered = !scheduler.ready_to_shut();
        entered.open();
        release.wait();
        retired = true;
    }
};

struct receiver
{
    gate entered;
    gate release;
    std::atomic<unsigned> calls{0};

    void run() {
        ++calls;
        entered.open();
        release.wait();
    }
};

struct cleanup_probe final : ark::module
{
    unsigned pre_shut_calls{};
    unsigned shut_calls{};
    std::function<void()> on_update;

    bool update() override {
        if (on_update)
        {
            on_update();
        }
        return true;
    }

    bool pre_shut() override {
        ++pre_shut_calls;
        return true;
    }

    bool shut() override {
        ++shut_calls;
        return true;
    }
};

} // namespace

TEST_CASE("Scheduler drains dispatched and queued callbacks while closing new registrations") {
    gate entered, release;
    ark::scheduler scheduler(1);
    release_on_exit unblock{release};
    std::atomic<unsigned> calls{};
    scheduler.in(0ms, [&] {
        ++calls;
        entered.open();
        release.wait();
    });
    REQUIRE(entered.wait());
    scheduler.in(0ms, [&] { ++calls; });
    REQUIRE(eventually([&] { return ark::scheduler_test_access::outstanding(scheduler) == 2; }));
    scheduler.stop_accepting();
    scheduler.stop_accepting();
    CHECK_FALSE(scheduler.ready_to_shut());
    CHECK_FALSE(scheduler.pre_shut());
    scheduler.in(0ms, [&] { calls += 100; });
    scheduler.interval(0ms, [&] { calls += 100; });
    scheduler.every(0ms, [&] { calls += 100; });
    CHECK_NOTHROW(scheduler.at("invalid time", [] {}));
    CHECK_NOTHROW(scheduler.cron("invalid cron", [] {}));
    release.open();
    REQUIRE(eventually([&] { return scheduler.ready_to_shut(); }));
    CHECK(scheduler.pre_shut());
    CHECK(scheduler.pre_shut());
    CHECK(calls == 2);
}

TEST_CASE("Scheduler destruction fallback keeps workers alive for accepted future one-shots") {
    std::atomic<unsigned> calls{};
    {
        ark::scheduler scheduler(1);
        scheduler.in(5ms, [&] { ++calls; });
    }
    CHECK(calls == 1);
}

TEST_CASE("Scheduler future recurring capture retirement is outside locks and inside readiness") {
    gate entered, release;
    std::atomic<bool> reentered{}, retired{};
    ark::scheduler scheduler(1);
    release_on_exit unblock{release};
    auto capture = std::make_shared<retiring_capture>(scheduler, entered, release, reentered, retired);
    std::atomic<unsigned> calls{};
    scheduler.every(24h, [capture, &calls] { ++calls; });
    capture.reset();
    scheduler.stop_accepting();
    REQUIRE(entered.wait());
    CHECK(reentered);
    CHECK_FALSE(scheduler.ready_to_shut());
    CHECK_FALSE(scheduler.pre_shut());
    release.open();
    REQUIRE(eventually([&] { return scheduler.ready_to_shut(); }));
    CHECK(retired);
    CHECK(calls == 0);
}

TEST_CASE("Scheduler dispatched capture destruction completes before readiness") {
    gate entered, release;
    std::atomic<bool> reentered{}, retired{};
    ark::scheduler scheduler(1);
    release_on_exit unblock{release};
    auto capture = std::make_shared<retiring_capture>(scheduler, entered, release, reentered, retired);
    scheduler.in(0ms, [capture] {});
    capture.reset();
    REQUIRE(entered.wait());
    CHECK(reentered);
    CHECK_FALSE(scheduler.ready_to_shut());
    release.open();
    REQUIRE(eventually([&] { return scheduler.ready_to_shut(); }));
    CHECK(retired);
}

TEST_CASE("Scheduler retains already admitted future one-shot work through drain") {
    ark::scheduler scheduler(1);
    std::atomic<unsigned> calls{};
    scheduler.in(24h, [&] { ++calls; });
    scheduler.at("2099-01-01 00:00:00", [&] { ++calls; });
    scheduler.stop_accepting();
    CHECK_FALSE(scheduler.ready_to_shut());
    CHECK_FALSE(scheduler.pre_shut());
    CHECK(ark::scheduler_test_access::future_one_shots(scheduler) == 2);
    // Advance only the test's two existing deadlines; execution, drain, owner
    // retirement and readiness still run through the production dispatcher.
    CHECK(ark::scheduler_test_access::future_one_shots(scheduler, true) == 2);
    REQUIRE(eventually([&] { return scheduler.ready_to_shut(); }));
    CHECK(calls == 2);
}

TEST_CASE("Scheduler callback exception owners retire before readiness") {
    gate entered, release;
    ark::scheduler scheduler(1);
    release_on_exit unblock{release};

    struct callback_error
    {
        gate& entered;
        gate& release;

        ~callback_error() {
            entered.open();
            release.wait();
        }
    };

    scheduler.in(0ms, [&] { throw callback_error{entered, release}; });
    REQUIRE(entered.wait());
    scheduler.stop_accepting();
    CHECK_FALSE(scheduler.ready_to_shut());
    release.open();
    REQUIRE(eventually([&] { return scheduler.ready_to_shut(); }));
}

TEST_CASE("Scheduler reports standard exception cause before retiring its callback frame") {
    ark::scheduler scheduler(1);
    std::atomic<bool> reported{};

    struct callback_error final : std::exception
    {
        std::atomic<bool>& reported;

        explicit callback_error(std::atomic<bool>& reported)
            : reported(reported) {
        }

        const char* what() const noexcept override {
            reported = true;
            return "scheduler callback diagnostic";
        }
    };

    scheduler.in(0ms, [&] { throw callback_error(reported); });
    scheduler.stop_accepting();
    REQUIRE(eventually([&] { return scheduler.ready_to_shut(); }));
    CHECK(reported);
}

TEST_CASE("Scheduler capture destruction can schedule same-instance one-shot completion") {
    gate entered, release;
    ark::scheduler scheduler(1);
    release_on_exit unblock{release};
    std::atomic<unsigned> calls{};

    struct continuation_capture
    {
        ark::scheduler& scheduler;
        std::atomic<unsigned>& calls;

        ~continuation_capture() {
            scheduler.in(0ms, [&calls = calls] { ++calls; });
        }
    };

    auto capture = std::make_shared<continuation_capture>(scheduler, calls);
    scheduler.in(0ms, [capture, &entered, &release] {
        entered.open();
        release.wait();
    });
    capture.reset();
    REQUIRE(entered.wait());
    scheduler.stop_accepting();
    release.open();
    REQUIRE(eventually([&] { return scheduler.ready_to_shut(); }));
    CHECK(calls == 1);
}

TEST_CASE("Scheduler fast callback captures retain continuation authority through handoff") {
    ark::scheduler scheduler(8);
    std::atomic<unsigned> calls{};

    struct continuation_capture
    {
        ark::scheduler& scheduler;
        std::atomic<unsigned>& calls;

        ~continuation_capture() {
            scheduler.in(0ms, [&calls = calls] { ++calls; });
        }
    };

    for (unsigned i = 0; i < 64; ++i)
    {
        scheduler.in(24h, [capture = std::make_shared<continuation_capture>(scheduler, calls)] {});
    }
    scheduler.stop_accepting();
    CHECK(ark::scheduler_test_access::future_one_shots(scheduler, true) == 64);
    REQUIRE(eventually([&] { return scheduler.ready_to_shut(); }));
    CHECK(calls == 64);
}

TEST_CASE("Scheduler worker continuation authority is limited to the same instance") {
    gate entered, release;
    ark::scheduler first(1), other(1);
    release_on_exit unblock{release};
    std::atomic<unsigned> calls{};
    first.in(0ms, [&] {
        entered.open();
        release.wait();
        first.in(0ms, [&] { ++calls; });
        first.every(0ms, [&] { calls += 100; });
        first.interval(0ms, [&] { calls += 100; });
        other.in(0ms, [&] { calls += 100; });
    });
    REQUIRE(entered.wait());
    first.stop_accepting();
    other.stop_accepting();
    release.open();
    REQUIRE(eventually([&] { return first.ready_to_shut() && other.ready_to_shut(); }));
    CHECK(calls == 1);
}

TEST_CASE("Scheduler interval callback can stop admission without rearming itself") {
    ark::scheduler scheduler(1);
    std::atomic<unsigned> calls{};
    std::atomic<bool> callback_saw_pending{};
    scheduler.interval(0ms, [&] {
        ++calls;
        scheduler.stop_accepting();
        callback_saw_pending = !scheduler.ready_to_shut() && !scheduler.pre_shut();
        scheduler.in(0ms, [&] { calls += 100; });
    });
    REQUIRE(eventually([&] { return scheduler.ready_to_shut(); }));
    CHECK(callback_saw_pending);
    CHECK(calls == 101);
}

TEST_CASE("Scheduler interval rearms after completion during normal operation") {
    gate entered, release;
    ark::scheduler scheduler(2);
    release_on_exit unblock{release};
    std::atomic<unsigned> calls{};
    scheduler.interval(0ms, [&] {
        if (++calls == 1)
        {
            entered.open();
            release.wait();
        }
        else
        {
            scheduler.stop_accepting();
        }
    });
    REQUIRE(entered.wait());
    CHECK(ark::scheduler_test_access::outstanding(scheduler) == 1);
    release.open();
    REQUIRE(eventually([&] { return scheduler.ready_to_shut(); }));
    CHECK(calls == 2);
}

TEST_CASE("Scheduler every retains overlapping dispatch during normal operation") {
    gate entered, release;
    ark::scheduler scheduler(2);
    release_on_exit unblock{release};
    std::atomic<unsigned> calls{};
    std::atomic<bool> first_finished{}, overlap{};
    scheduler.every(1ms, [&] {
        if (++calls == 1)
        {
            entered.open();
            release.wait();
            first_finished = true;
        }
        else
        {
            overlap = !first_finished;
            scheduler.stop_accepting();
        }
    });
    REQUIRE(entered.wait());
    REQUIRE(eventually([&] { return overlap.load(); }));
    CHECK_FALSE(scheduler.ready_to_shut());
    release.open();
    REQUIRE(eventually([&] { return scheduler.ready_to_shut(); }));
    CHECK(calls >= 2);
}

TEST_CASE("Scheduler concurrent accepted at continuations and cron use caller-owned calendar storage") {
    constexpr unsigned workers = 8;
    constexpr unsigned rounds = 32;
    ark::scheduler scheduler(workers);
    ark::cron expression("17 3 * * *");
    std::barrier start(workers + 1);
    std::array<ark::scheduler::clock::time_point, workers> from, expected;
    std::atomic<unsigned> calls{};
    std::atomic<bool> calendars_match{true};
    for (unsigned i = 0; i < workers; ++i)
    {
        from[i] = ark::scheduler::clock::from_time_t(1700000000 + i * 86400);
        expected[i] = expression.cron_to_next(from[i]);
        scheduler.in(0ms, [&, i] {
            start.arrive_and_wait();
            for (unsigned round = 0; round < rounds; ++round)
            {
                if (expression.cron_to_next(from[i]) != expected[i])
                {
                    calendars_match = false;
                }
                scheduler.at("2000-01-01 00:00:00", [&] { ++calls; });
            }
        });
    }
    scheduler.stop_accepting();
    start.arrive_and_wait();
    REQUIRE(eventually([&] { return scheduler.ready_to_shut(); }));
    CHECK(calendars_match);
    CHECK(calls == workers * rounds);
}

TEST_CASE("Scheduler concurrent registration and stop retires every admitted capture") {
    ark::scheduler scheduler(2);
    std::barrier start(5);
    std::atomic<unsigned> owners{}, destroyed{}, calls{};

    struct owner
    {
        std::atomic<unsigned>& destroyed;

        ~owner() {
            ++destroyed;
        }
    };

    std::vector<std::thread> producers;
    for (unsigned i = 0; i < 4; ++i)
    {
        producers.emplace_back([&] {
            start.arrive_and_wait();
            for (unsigned j = 0; j < 500; ++j)
            {
                auto capture = std::make_shared<owner>(destroyed);
                ++owners;
                scheduler.every(24h, [capture, &calls] { ++calls; });
            }
        });
    }
    start.arrive_and_wait();
    scheduler.stop_accepting();
    for (auto& producer : producers)
    {
        producer.join();
    }
    REQUIRE(eventually([&] { return scheduler.ready_to_shut(); }));
    CHECK(destroyed == owners);
    CHECK(calls == 0);
}

TEST_CASE("Scheduler readiness includes a registration copying captures when stop begins") {
    gate entered, release;
    ark::scheduler scheduler(1);
    release_on_exit unblock{release};

    struct copying_callable
    {
        gate& entered;
        gate& release;

        copying_callable(gate& entered, gate& release)
            : entered(entered)
            , release(release) {
        }

        copying_callable(const copying_callable& other)
            : entered(other.entered)
            , release(other.release) {
            entered.open();
            release.wait();
        }

        void operator()() const {
        }
    } callable(entered, release);

    std::thread producer([&] { scheduler.every(24h, callable); });
    const bool copying = entered.wait();
    scheduler.stop_accepting();
    CHECK(copying);
    CHECK_FALSE(scheduler.ready_to_shut());
    release.open();
    producer.join();
    REQUIRE(eventually([&] { return scheduler.ready_to_shut(); }));
}

TEST_CASE("Schedule module lets the owner advance accepted callback completion before cleanup") {
    ark::plugin_manager manager;
    cleanup_probe probe;
    receiver callback;
    auto module = ark::detail::make_schedule_module();
    release_on_exit unblock{callback.release};
    REQUIRE(module->init());
    module->set_name("scheduler");
    REQUIRE(manager.add_module("scheduler", module.get()));
    REQUIRE(manager.add_module("probe", &probe));
    REQUIRE(manager.add_update_module(&probe));
    module->add_interval_schedule(24h, &callback, &receiver::run);
    REQUIRE(callback.entered.wait());
    CHECK_FALSE(manager.stop(1));
    CHECK_FALSE(module->ready_to_shut());
    CHECK_FALSE(module->pre_shut());
    CHECK(probe.pre_shut_calls == 0);
    CHECK(probe.shut_calls == 0);
    module->add_interval_schedule(0ms, &callback, &receiver::run);
    probe.on_update = [&] { callback.release.open(); };
    REQUIRE(manager.update(2));
    REQUIRE(eventually([&] { return module->ready_to_shut(); }));
    CHECK(manager.stop(3));
    CHECK(manager.stop(4));
    CHECK(probe.pre_shut_calls == 1);
    CHECK(probe.shut_calls == 1);
    CHECK(callback.calls == 1);
}

namespace {
ark::task<> schedule_accepted_completion(ark::schedule_module& module, receiver& callback, bool& scheduled) {
    module.add_at_schedule("2000-01-01 00:00:00", &callback, &receiver::run);
    scheduled = true;
    co_return;
}
} // namespace

TEST_CASE("Schedule module permits one-shot continuations only in manager accepted-task polling") {
    ark::plugin_manager manager;
    cleanup_probe probe;
    receiver callback;
    auto module = ark::detail::make_schedule_module();
    release_on_exit unblock{callback.release};
    module->set_plugin_manager(&manager);
    REQUIRE(module->init());
    REQUIRE(manager.add_module("scheduler", module.get()));
    REQUIRE(manager.add_module("probe", &probe));
    auto& tasks = manager.create_task_group(probe);
    bool scheduled{};
    REQUIRE(tasks.start(schedule_accepted_completion(*module, callback, scheduled)) != 0);
    CHECK_FALSE(manager.stop(1));
    CHECK_FALSE(module->ready_to_shut());
    // The same owner outside task polling remains a refused external root.
    module->add_at_schedule("2000-01-01 00:00:00", &callback, &receiver::run);
    REQUIRE(manager.update(2));
    REQUIRE(scheduled);
    REQUIRE(callback.entered.wait());
    CHECK_FALSE(module->ready_to_shut());
    CHECK(callback.calls == 1);
    CHECK(probe.pre_shut_calls == 0);
    callback.release.open();
    REQUIRE(eventually([&] { return module->ready_to_shut(); }));
    CHECK(manager.stop(3));
    CHECK(probe.pre_shut_calls == 1);
    CHECK(probe.shut_calls == 1);
}

TEST_CASE("Manager deadline retains accepted future one-shot work without cleanup") {
    ark::plugin_manager manager;
    ark::scheduler scheduler(1);

    struct scheduler_probe final : ark::module
    {
        ark::scheduler& scheduler;
        unsigned cleanup_calls{};

        explicit scheduler_probe(ark::scheduler& scheduler)
            : scheduler(scheduler) {
        }

        bool stop_accepting() override {
            scheduler.stop_accepting();
            return true;
        }

        bool ready_to_shut() const override {
            return scheduler.ready_to_shut();
        }

        bool pre_shut() override {
            ++cleanup_calls;
            return scheduler.pre_shut();
        }

        bool shut() override {
            ++cleanup_calls;
            return true;
        }
    } probe(scheduler);

    std::atomic<unsigned> calls{};
    scheduler.in(24h, [&] { ++calls; });
    REQUIRE(manager.add_module("scheduler", &probe));
    CHECK_FALSE(manager.stop_until(1, std::chrono::steady_clock::now() + 5s));
    CHECK_FALSE(scheduler.ready_to_shut());
    CHECK(ark::scheduler_test_access::future_one_shots(scheduler) == 1);
    CHECK_FALSE(manager.stop_until(2, std::chrono::steady_clock::now()));
    CHECK(manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::timed_out);
    CHECK(probe.cleanup_calls == 0);
    CHECK(ark::scheduler_test_access::future_one_shots(scheduler, true) == 1);
    REQUIRE(eventually([&] { return scheduler.ready_to_shut(); }));
    CHECK(calls == 1);
    CHECK_FALSE(manager.stop(3));
    CHECK(probe.cleanup_calls == 0);
}

TEST_CASE("Schedule module timeout keeps all module cleanup hooks closed after work retires") {
    ark::plugin_manager manager;
    cleanup_probe probe;
    receiver callback;
    auto module = ark::detail::make_schedule_module();
    release_on_exit unblock{callback.release};
    REQUIRE(module->init());
    REQUIRE(manager.add_module("scheduler", module.get()));
    REQUIRE(manager.add_module("probe", &probe));
    module->add_interval_schedule(24h, &callback, &receiver::run);
    REQUIRE(callback.entered.wait());
    CHECK_FALSE(manager.stop_until(1, std::chrono::steady_clock::now() + 5s));
    CHECK_FALSE(manager.stop_until(2, std::chrono::steady_clock::now()));
    CHECK(manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::timed_out);
    CHECK(probe.pre_shut_calls == 0);
    CHECK(probe.shut_calls == 0);
    callback.release.open();
    REQUIRE(eventually([&] { return module->ready_to_shut(); }));
    CHECK_FALSE(manager.stop(3));
    CHECK(probe.pre_shut_calls == 0);
    CHECK(probe.shut_calls == 0);
}
