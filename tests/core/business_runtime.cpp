// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "../network/doctest.h"

#include <ark/plugin_manager.hpp>
#include "detail/database_awaitable.hpp"

namespace {

class observed_module final : public ark::module
{
public:
    explicit observed_module(std::string name)
        : name_(std::move(name)) {
    }

    const std::string& get_name() const override {
        return name_;
    }

    std::vector<std::string> get_dependencies() const override {
        ++dependency_reads;
        if (dependency_metadata_throws)
        {
            throw std::runtime_error("dependency metadata must not be read again");
        }
        return dependencies;
    }

    bool update() override {
        if (on_update)
        {
            on_update();
        }
        return true;
    }

    bool ready_to_shut() const override {
        ++readiness_calls;
        return on_readiness ? on_readiness() : readiness_result;
    }

    bool pre_shut() override {
        ++pre_shut_calls;
        if (on_pre_shut)
        {
            on_pre_shut();
        }
        return pre_shut_result;
    }

    bool shut() override {
        ++shut_calls;
        if (on_shut)
        {
            on_shut();
        }
        return shut_result;
    }

    std::function<void()> on_update;
    std::function<bool()> on_readiness;
    std::function<void()> on_pre_shut;
    std::function<void()> on_shut;
    bool pre_shut_result{true};
    bool dependency_metadata_throws{};
    bool readiness_result{true};
    bool shut_result{true};
    unsigned pre_shut_calls{};
    mutable unsigned dependency_reads{};
    std::vector<std::string> dependencies;
    mutable unsigned readiness_calls{};
    unsigned shut_calls{};

private:
    std::string name_;
};

} // namespace

TEST_CASE("Manager stop from an update defers teardown until the active callback returns") {
    ark::plugin_manager manager;
    observed_module module("stop-from-update");
    REQUIRE(manager.add_module(module.get_name(), &module));
    REQUIRE(manager.add_update_module(&module));
    bool stopped_inside{};
    module.on_update = [&] {
        stopped_inside = manager.stop(2);
        CHECK(module.readiness_calls == 0);
        CHECK(module.pre_shut_calls == 0);
        CHECK(module.shut_calls == 0);
    };
    CHECK(manager.update(1));
    CHECK_FALSE(stopped_inside);
    CHECK(module.readiness_calls == 0);
    CHECK(module.pre_shut_calls == 0);
    CHECK(module.shut_calls == 0);
    CHECK(manager.stop(3));
    CHECK(module.pre_shut_calls == 1);
    CHECK(module.shut_calls == 1);
}

namespace {

ark::task<> ready_task() {
    co_return;
}

ark::task<> check_executor(std::shared_ptr<arknet::manual_runtime> runtime, unsigned& completed) {
    const auto executor = co_await asio::this_coro::executor;
    CHECK(executor == asio::any_io_executor(runtime->get_executor()));
    CHECK(runtime->get_executor().running_in_this_thread());
    ++completed;
}

ark::task<> consume_provider(ark::module& provider, std::string& result) {
    co_await asio::post(asio::use_awaitable);
    result = provider.get_name();
}

using bridge_type = ark::detail::database_awaitable<std::string>;

struct controlled_service
{
    using callback = std::function<void(std::exception_ptr, std::string)>;
    callback pending;
    callback first_callback;
    unsigned started{};
    unsigned cancelled{};
    bool defer_cancellation{};

    std::uint64_t start(callback completion) {
        CHECK_FALSE(pending);
        pending = std::move(completion);
        if (!first_callback)
        {
            first_callback = pending;
        }
        return ++started;
    }

    void complete(std::string value) {
        REQUIRE(pending);
        auto delivery = std::move(pending);
        delivery({}, std::move(value));
    }

    void cancel() {
        ++cancelled;
        if (!defer_cancellation)
        {
            complete_cancelled();
        }
    }

    void complete_cancelled() {
        REQUIRE(pending);
        auto delivery = std::move(pending);
        delivery(std::make_exception_ptr(arknet::system_error(asio::error::operation_aborted)), {});
    }
};

ark::task<> sequential_operations(controlled_service& service, bridge_type& bridge, std::string& result) {
    const auto first = co_await bridge.wait([&](auto completion) { return service.start(std::move(completion)); },
        [&](std::uint64_t) { service.cancel(); });
    const auto second = co_await bridge.wait([&](auto completion) { return service.start(std::move(completion)); },
        [&](std::uint64_t) { service.cancel(); });
    result = first + second;
}

using ark::module;

class removal_module final : public module
{
public:
    explicit removal_module(unsigned& destroyed)
        : destroyed_(destroyed) {
    }

    ~removal_module() override {
        ++destroyed_;
    }

    const std::string& get_name() const override {
        return name_;
    }

private:
    const std::string name_{GET_CLASS_NAME(removal_module)};
    unsigned& destroyed_;
};

struct removal_owner
{
    ark::plugin_manager& manager;
    std::unordered_map<std::string, module*> modules_;

    ark::plugin_manager* get_plugin_manager() {
        return &manager;
    }

    void uninstall() {
        ARK_UNREGISTER_MODULE(removal_module, removal_module);
    }
};

} // namespace

TEST_CASE("Registered modules share one bounded owner executor after completion publication") {
    ark::plugin_manager manager;
    observed_module first("first"), second("second");
    REQUIRE(manager.add_module(first.get_name(), &first));
    REQUIRE(manager.add_module(second.get_name(), &second));
    REQUIRE(manager.add_update_module(&first));
    REQUIRE(manager.add_update_module(&second));
    auto& first_tasks = manager.create_task_group(first, 1);
    auto& second_tasks = manager.create_task_group(second, 1);
    auto runtime = manager.get_task_runtime();
    REQUIRE(runtime == manager.get_task_runtime());
    unsigned first_updates{}, second_updates{}, handlers{}, completions{};
    first.on_update = [&] { ++first_updates; };
    second.on_update = [&] { ++second_updates; };
    for (unsigned index = 0; index < 4; ++index)
    {
        asio::post(runtime->get_executor(), [&] {
            CHECK(first_updates > 0);
            CHECK(second_updates > 0);
            ++handlers;
        });
    }
    REQUIRE(first_tasks.start(check_executor(runtime, completions)) != 0);
    REQUIRE(second_tasks.start(check_executor(runtime, completions)) != 0);
    CHECK(manager.update(1, 0));
    CHECK(handlers == 0);
    CHECK(manager.update(2, 1));
    CHECK(handlers == 1);
    CHECK(manager.pending_tasks() == 2);
    CHECK(manager.update(3));
    CHECK(handlers == 4);
    CHECK(completions == 2);
    CHECK(manager.pending_tasks() == 0);
    CHECK(manager.stop(4));
}

TEST_CASE("Manager stop cancels a suspended task and retains owners until late callbacks retire") {
    ark::plugin_manager manager;
    observed_module module("cancelled");
    REQUIRE(manager.add_module(module.get_name(), &module));
    REQUIRE(manager.add_update_module(&module));
    auto& tasks = manager.create_task_group(module, 2);
    bridge_type bridge(manager.get_task_runtime(), 1);
    controlled_service service;
    service.defer_cancellation = true;
    module.on_update = [&] { bridge.dispatch(); };
    std::string result;
    unsigned completed{};
    const auto owner = std::this_thread::get_id();
    REQUIRE(tasks.start(sequential_operations(service, bridge, result), [&](std::exception_ptr error) {
        CHECK(error);
        CHECK(std::this_thread::get_id() == owner);
        CHECK(manager.pending_tasks() == 0);
        CHECK_FALSE(manager.stop(3));
        CHECK(module.pre_shut_calls == 0);
        ++completed;
    }) != 0);
    REQUIRE(manager.update(1));
    REQUIRE(service.started == 1);
    CHECK_FALSE(manager.stop(2));
    CHECK_FALSE(tasks.closed());
    CHECK(tasks.start(ready_task()) == 0);
    CHECK(manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::stopping);
    CHECK(module.pre_shut_calls == 0);
    CHECK(manager.update(3));
    CHECK(completed == 0);
    CHECK(manager.pending_tasks() == 1);
    CHECK_FALSE(manager.stop(3));
    CHECK(service.cancelled == 1);
    CHECK(module.readiness_calls == 0);
    CHECK(module.pre_shut_calls == 0);
    service.complete_cancelled();
    CHECK(manager.update(3));
    CHECK(completed == 1);
    CHECK(tasks.closed());
    CHECK(service.cancelled == 1);
    CHECK(service.started == 1);
    CHECK(result.empty());
    CHECK(bridge.pending() == 0);
    service.first_callback({}, "late result");
    service.first_callback({}, "late duplicate");
    CHECK(manager.update(4));
    CHECK(completed == 1);
    CHECK(service.started == 1);
    CHECK(manager.stop(5));
    CHECK(manager.stop(6));
    CHECK(module.pre_shut_calls == 1);
    CHECK(module.shut_calls == 1);
}

TEST_CASE("Owner update and raw poll guards recover after handler exceptions") {
    ark::plugin_manager manager;
    observed_module module("throwing-owner");
    REQUIRE(manager.add_module(module.get_name(), &module));
    REQUIRE(manager.add_update_module(&module));
    module.on_update = [&] { throw std::runtime_error("module update failed"); };
    CHECK_THROWS_AS(manager.update(1), std::runtime_error);
    module.on_update = {};
    auto runtime = manager.get_task_runtime();
    asio::post(runtime->get_executor(), [&] {
        CHECK_THROWS_AS(manager.update(2), std::logic_error);
        throw std::runtime_error("task handler failed");
    });
    CHECK_THROWS_AS(manager.update(2), std::runtime_error);
    unsigned delivered{};
    asio::post(runtime->get_executor(), [&] { ++delivered; });
    CHECK(manager.update(3));
    CHECK(delivered == 1);
    CHECK(manager.stop(4));
}

TEST_CASE("Final completion cannot teardown or mutate owners even during an external context poll") {
    ark::plugin_manager manager;
    observed_module module("final-completion");
    REQUIRE(manager.add_module(module.get_name(), &module));
    auto& tasks = manager.create_task_group(module);
    auto runtime = manager.get_task_runtime();
    unsigned completed{};
    REQUIRE(tasks.start(ready_task(), [&](std::exception_ptr error) {
        CHECK_FALSE(error);
        CHECK(manager.pending_tasks() == 0);
        CHECK_FALSE(manager.stop(1));
        CHECK(module.readiness_calls == 0);
        CHECK_THROWS_AS(manager.remove_module(module.get_name()), std::logic_error);
        CHECK_THROWS_AS(manager.update(1), std::logic_error);
        CHECK_THROWS_AS(runtime->poll(), std::logic_error);
        CHECK(module.pre_shut_calls == 0);
        ++completed;
    }) != 0);
    // Compatibility consumers can still hold a runtime handle. That must not
    // create a hole in teardown guards when the manager did not initiate poll.
    runtime->poll();
    CHECK(completed == 1);
    CHECK(module.readiness_calls == 0);
    CHECK(manager.stop(2));
    CHECK(module.pre_shut_calls == 1);
}

TEST_CASE("Update and completion exceptions preserve the owner and release reentrancy guards") {
    ark::plugin_manager manager;
    observed_module module("throwing");
    REQUIRE(manager.add_module(module.get_name(), &module));
    REQUIRE(manager.add_update_module(&module));
    auto& tasks = manager.create_task_group(module);
    module.on_update = [] { throw std::runtime_error("module update failed"); };
    REQUIRE(tasks.start(ready_task(), [&](std::exception_ptr error) {
        CHECK_FALSE(error);
        CHECK_FALSE(manager.stop(3));
        throw std::runtime_error("completion failed");
    }) != 0);
    CHECK_THROWS_AS(manager.update(1), std::runtime_error);
    CHECK(manager.pending_tasks() == 1);
    CHECK_FALSE(manager.stop(2));
    CHECK(module.pre_shut_calls == 0);
    module.on_update = {};
    CHECK_THROWS_AS(manager.update(3), std::runtime_error);
    CHECK(manager.pending_tasks() == 0);
    CHECK(module.pre_shut_calls == 0);
    CHECK(manager.update(4));
    CHECK(manager.stop(5));
}

TEST_CASE("Manager owner and active registry checks reject mutations before they take effect") {
    ark::plugin_manager manager;
    observed_module module("owner"), extra("extra");
    REQUIRE(manager.add_module(module.get_name(), &module));
    REQUIRE(manager.add_update_module(&module));
    auto& tasks = manager.create_task_group(module);
    unsigned rejected{};
    std::thread worker([&] {
        auto rejects = [&](auto action) {
            try
            { action(); }
            catch (const std::logic_error&)
            { ++rejected; }
        };
        rejects([&] { manager.get_task_runtime(); });
        rejects([&] { manager.get_tasks(module); });
        rejects([&] { manager.create_task_group(extra); });
        rejects([&] { manager.pending_tasks(); });
        rejects([&] { manager.get_startup_failure(); });
        rejects([&] { manager.get_shutdown_state(); });
        rejects([&] { manager.get_shutdown_exception(); });
        rejects([&] { manager.get_shutdown_failure(); });
        rejects([&] { manager.update(1); });
        rejects([&] { manager.stop(1); });
        rejects([&] { manager.stop_until(1, std::chrono::steady_clock::now()); });
        rejects([&] { manager.start(1, {}, {}, {}); });
        rejects([&] { manager.add_module(extra.get_name(), &extra); });
        rejects([&] { manager.remove_module(module.get_name()); });
        rejects([&] { manager.add_update_module(&extra); });
        rejects([&] { manager.remove_update_module(module.get_name()); });
        rejects([&] { tasks.start(ready_task()); });
    });
    worker.join();
    CHECK(rejected == 17);
    module.on_update = [&] {
        CHECK_THROWS_AS(manager.update(1), std::logic_error);
        CHECK_THROWS_AS(manager.remove_module(module.get_name()), std::logic_error);
        CHECK_THROWS_AS(manager.remove_update_module(module.get_name()), std::logic_error);
        CHECK_THROWS_AS(manager.add_module(extra.get_name(), &extra), std::logic_error);
    };
    CHECK(manager.update(1));
    CHECK(&manager.get_tasks(module) == &tasks);
    CHECK(manager.stop(2));
}

TEST_CASE("Pending unregister throws before the real module macro can delete its owner") {
    ark::plugin_manager manager;
    unsigned destroyed{};
    auto* module = new removal_module(destroyed);
    removal_owner plugin{manager, {{module->get_name(), module}}};
    REQUIRE(manager.add_module(module->get_name(), module));
    auto& tasks = manager.create_task_group(*module);
    REQUIRE(tasks.start(ready_task()) != 0);
    CHECK_THROWS_AS(plugin.uninstall(), std::logic_error);
    CHECK(destroyed == 0);
    CHECK(manager.pending_tasks() == 1);
    CHECK(&manager.get_tasks(*module) == &tasks);
    CHECK(manager.update(1));
    CHECK(manager.stop(2));
    CHECK_NOTHROW(plugin.uninstall());
    CHECK(destroyed == 1);
    CHECK(plugin.modules_.empty());
}

TEST_CASE("An empty provider cannot unregister while another module owns accepted work") {
    ark::plugin_manager manager;
    unsigned destroyed{};
    auto* provider = new removal_module(destroyed);
    removal_owner plugin{manager, {{provider->get_name(), provider}}};
    observed_module consumer("consumer");
    REQUIRE(manager.add_module(provider->get_name(), provider));
    REQUIRE(manager.add_module(consumer.get_name(), &consumer));
    auto& provider_tasks = manager.create_task_group(*provider);
    auto& consumer_tasks = manager.create_task_group(consumer);
    std::string received;
    REQUIRE(consumer_tasks.start(consume_provider(*provider, received)) != 0);
    CHECK(provider_tasks.pending() == 0);
    CHECK_THROWS_AS(plugin.uninstall(), std::logic_error);
    CHECK(destroyed == 0);
    CHECK(manager.pending_tasks() == 1);
    CHECK(manager.update(1));
    CHECK(received == provider->get_name());
    CHECK(manager.pending_tasks() == 0);
    CHECK(manager.stop(2));
    CHECK_NOTHROW(plugin.uninstall());
    CHECK(destroyed == 1);
}

TEST_CASE("Managed task groups enforce registration capacity identity and terminal cancellation") {
    ark::plugin_manager manager;
    observed_module first("first"), unknown("unknown");
    CHECK_THROWS_AS(manager.create_task_group(unknown), std::logic_error);
    REQUIRE(manager.add_module(first.get_name(), &first));
    CHECK_FALSE(manager.add_module("alias", &first));
    CHECK_FALSE(manager.add_module("null", nullptr));
    CHECK_THROWS_AS(manager.get_tasks(first), std::logic_error);
    CHECK_THROWS_AS(manager.create_task_group(first, 0), std::invalid_argument);
    auto* original = &manager.create_task_group(first, 1);
    CHECK_THROWS_AS(manager.create_task_group(first), std::logic_error);
    std::vector<std::unique_ptr<observed_module>> modules;
    for (unsigned index = 0; index < 40; ++index)
    {
        auto next = std::make_unique<observed_module>("next-" + std::to_string(index));
        REQUIRE(manager.add_module(next->get_name(), next.get()));
        manager.create_task_group(*next);
        modules.push_back(std::move(next));
    }
    CHECK(&manager.get_tasks(first) == original);
    CHECK(manager.stop(1));
    CHECK(original->closed());
    CHECK(original->start(ready_task()) == 0);
    CHECK_THROWS_AS(manager.create_task_group(unknown), std::logic_error);
    CHECK_THROWS_AS(manager.add_module(unknown.get_name(), &unknown), std::logic_error);
    CHECK_THROWS_AS(manager.start(2, {}, {}, {}), std::logic_error);
    CHECK_FALSE(manager.update(2));
    CHECK(manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::stopped);
}

TEST_CASE("Every module must be ready before any shutdown hook and updates retire native resources") {
    ark::plugin_manager manager;
    observed_module first("first"), second("second");
    REQUIRE(manager.add_module(first.get_name(), &first));
    REQUIRE(manager.add_module(second.get_name(), &second));
    REQUIRE(manager.add_update_module(&second));
    first.readiness_result = false;
    second.readiness_result = false;
    auto& tasks = manager.create_task_group(first);
    REQUIRE(tasks.start(ready_task()) != 0);
    CHECK_FALSE(manager.stop(1));
    CHECK(first.readiness_calls == 0);
    CHECK(second.readiness_calls == 0);
    REQUIRE(manager.update(2));
    REQUIRE(manager.pending_tasks() == 0);

    for (unsigned attempt = 0; attempt < 2; ++attempt)
    {
        CHECK_FALSE(manager.stop(3 + attempt));
        CHECK(manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::stopping);
        CHECK(manager.get_shutdown_failure().empty());
        CHECK_FALSE(manager.get_shutdown_exception());
        CHECK(first.readiness_calls == attempt + 1);
        CHECK(second.readiness_calls == attempt + 1);
        CHECK(first.pre_shut_calls == 0);
        CHECK(second.pre_shut_calls == 0);
        CHECK(first.shut_calls == 0);
        CHECK(second.shut_calls == 0);
        first.readiness_result = true;
    }
    second.on_update = [&] { second.readiness_result = true; };
    CHECK(manager.update(5));
    auto check_all_ready = [&] {
        CHECK(first.readiness_calls == 3);
        CHECK(second.readiness_calls == 3);
    };
    first.on_pre_shut = check_all_ready;
    second.on_pre_shut = check_all_ready;
    CHECK(manager.stop(6));
    CHECK(manager.stop(7));
    CHECK(first.pre_shut_calls == 1);
    CHECK(second.pre_shut_calls == 1);
    CHECK(first.shut_calls == 1);
    CHECK(second.shut_calls == 1);
}

TEST_CASE("Readiness exceptions retain resources and produce a sticky module phase failure") {
    ark::plugin_manager manager;
    observed_module module("native-retirement"), other("other");
    REQUIRE(manager.add_module(module.get_name(), &module));
    REQUIRE(manager.add_module(other.get_name(), &other));
    auto& tasks = manager.create_task_group(module);
    other.readiness_result = false;
    module.on_readiness = []() -> bool { throw std::runtime_error("native readiness failed"); };
    CHECK_FALSE(manager.stop(1));
    CHECK(manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::failed);
    CHECK(manager.get_shutdown_failure() == "ready_to_shut: native-retirement");
    REQUIRE(manager.get_shutdown_exception());
    CHECK_THROWS_WITH_AS(
        std::rethrow_exception(manager.get_shutdown_exception()), "native readiness failed", std::runtime_error);
    CHECK(module.pre_shut_calls == 0);
    CHECK(other.pre_shut_calls == 0);
    CHECK(module.shut_calls == 0);
    CHECK(other.shut_calls == 0);
    CHECK_FALSE(manager.stop(2));
    CHECK_FALSE(manager.update(3));
    CHECK(module.readiness_calls == 1);
    CHECK(&manager.get_tasks(module) == &tasks);
    CHECK(tasks.start(ready_task()) == 0);
}

TEST_CASE("Readiness hooks share the active lifecycle and registry reentrancy guards") {
    ark::plugin_manager manager;
    observed_module module("readiness-owner"), extra("extra");
    REQUIRE(manager.add_module(module.get_name(), &module));
    REQUIRE(manager.add_update_module(&module));
    auto& tasks = manager.create_task_group(module);
    module.on_readiness = [&] {
        CHECK_FALSE(manager.stop(2));
        CHECK_THROWS_AS(manager.update(2), std::logic_error);
        CHECK_THROWS_AS(manager.start(2, {}, {}, {}), std::logic_error);
        CHECK_THROWS_AS(manager.remove_module(module.get_name()), std::logic_error);
        CHECK_THROWS_AS(manager.remove_update_module(module.get_name()), std::logic_error);
        CHECK_THROWS_AS(manager.add_module(extra.get_name(), &extra), std::logic_error);
        CHECK_THROWS_AS(manager.add_update_module(&extra), std::logic_error);
        CHECK_THROWS_AS(manager.create_task_group(extra), std::logic_error);
        CHECK(module.pre_shut_calls == 0);
        CHECK(&manager.get_tasks(module) == &tasks);
        return true;
    };
    CHECK(manager.stop(1));
    CHECK(module.readiness_calls == 1);
    CHECK(module.pre_shut_calls == 1);
    CHECK(module.shut_calls == 1);
}

TEST_CASE("Modules without a readiness override keep their default shutdown behavior") {
    ark::plugin_manager manager;
    ark::module module;
    CHECK(module.ready_to_shut());
    REQUIRE(manager.add_module("default-readiness", &module));
    CHECK(manager.stop(1));
    CHECK(manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::stopped);
    CHECK(manager.get_shutdown_failure().empty());
}

TEST_CASE("Shutdown hook failure is observable and retains modules without repeated teardown") {
    for (unsigned failure = 0; failure < 4; ++failure)
    {
        CAPTURE(failure);
        ark::plugin_manager manager;
        observed_module module("failed-shutdown");
        REQUIRE(manager.add_module(module.get_name(), &module));
        auto& tasks = manager.create_task_group(module);
        module.pre_shut_result = failure != 0;
        module.shut_result = failure != 1;
        if (failure == 2)
        {
            module.on_pre_shut = [] { throw std::runtime_error("pre_shut failed"); };
        }
        if (failure == 3)
        {
            module.on_shut = [] { throw std::runtime_error("shut failed"); };
        }
        CHECK_FALSE(manager.stop(1));
        CHECK(manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::failed);
        CHECK(bool(manager.get_shutdown_exception()) == (failure >= 2));
        CHECK(manager.get_shutdown_failure() ==
              (failure == 0 || failure == 2 ? "pre_shut: failed-shutdown" : "shut: failed-shutdown"));
        CHECK(module.pre_shut_calls == 1);
        CHECK(module.shut_calls == (failure == 1 || failure == 3 ? 1 : 0));
        CHECK_FALSE(manager.stop(2));
        CHECK_FALSE(manager.update(3));
        CHECK(module.pre_shut_calls == 1);
        CHECK(module.shut_calls == (failure == 1 || failure == 3 ? 1 : 0));
        CHECK(&manager.get_tasks(module) == &tasks);
        CHECK(tasks.start(ready_task()) == 0);
    }
    ark::plugin_manager empty;
    CHECK(empty.stop(1));
    CHECK(empty.stop(2));
}

TEST_CASE("Shutdown closes callback sources before reverse dependent object cleanup") {
    ark::plugin_manager manager;
    observed_module provider("provider"), consumer("consumer");
    consumer.dependencies = {"provider"};
    REQUIRE(manager.add_module(provider.get_name(), &provider));
    REQUIRE(manager.add_module(consumer.get_name(), &consumer));
    REQUIRE(manager.start(0, {}, {}, {}));
    auto& tasks = manager.create_task_group(consumer);
    REQUIRE(tasks.start(ready_task()) != 0);
    std::vector<std::string> events;
    consumer.on_pre_shut = [&] {
        CHECK(tasks.closed());
        CHECK(tasks.start(ready_task()) == 0);
        CHECK(manager.pending_tasks() == 0);
        CHECK(provider.pre_shut_calls == 0);
        events.emplace_back("consumer-pre");
    };
    consumer.on_shut = [&] {
        CHECK(provider.shut_calls == 0);
        events.emplace_back("consumer-shut");
    };
    provider.on_pre_shut = [&] { events.emplace_back("provider-pre"); };
    provider.on_shut = [&] { events.emplace_back("provider-shut"); };
    CHECK_FALSE(manager.stop(1));
    CHECK(events.empty());
    CHECK(manager.update(2));
    CHECK(manager.stop(3));
    CHECK(manager.stop(4));
    CHECK(events == std::vector<std::string>{"consumer-pre", "provider-pre", "consumer-shut", "provider-shut"});
    CHECK(provider.dependency_reads == 1);
    CHECK(consumer.dependency_reads == 1);
}

TEST_CASE("Shutdown builds one snapshot for a manual manager without startup") {
    ark::plugin_manager manager;
    observed_module module("manual");
    REQUIRE(manager.add_module(module.get_name(), &module));
    CHECK(module.dependency_reads == 0);
    CHECK(manager.stop(1));
    CHECK(manager.stop(2));
    CHECK(module.dependency_reads == 1);
    CHECK(module.pre_shut_calls == 1);
}

TEST_CASE("Shutdown rebuilds after legal independent additions and removals") {
    ark::plugin_manager manager;
    observed_module retained("retained"), added("added");
    auto removed = std::make_unique<observed_module>("removed");
    REQUIRE(manager.add_module(removed->get_name(), removed.get()));
    REQUIRE(manager.add_module(retained.get_name(), &retained));
    REQUIRE(manager.start(0, {}, {}, {}));
    manager.remove_module(removed->get_name());
    removed.reset();
    REQUIRE(manager.add_module(added.get_name(), &added));
    std::vector<std::string> events;
    retained.on_pre_shut = [&] { events.emplace_back("retained"); };
    added.on_pre_shut = [&] { events.emplace_back("added"); };
    CHECK(manager.stop(1));
    CHECK(events == std::vector<std::string>{"retained", "added"});
    CHECK(retained.dependency_reads == 2);
    CHECK(added.dependency_reads == 1);
    CHECK(retained.pre_shut_calls == 1);
    CHECK(added.pre_shut_calls == 1);
}

TEST_CASE("Shutdown does not use the cached pointer after same-key replacement") {
    ark::plugin_manager manager;
    observed_module consumer("consumer"), replacement("provider");
    consumer.dependencies = {"provider"};
    auto original = std::make_unique<observed_module>("provider");
    REQUIRE(manager.add_module(original->get_name(), original.get()));
    REQUIRE(manager.add_module(consumer.get_name(), &consumer));
    REQUIRE(manager.start(0, {}, {}, {}));
    manager.remove_module(original->get_name());
    original.reset();
    REQUIRE(manager.add_module(replacement.get_name(), &replacement));
    std::vector<std::string> events;
    consumer.on_pre_shut = [&] { events.emplace_back("consumer"); };
    replacement.on_pre_shut = [&] { events.emplace_back("replacement"); };
    CHECK(manager.stop(1));
    CHECK(events == std::vector<std::string>{"consumer", "replacement"});
    CHECK(replacement.pre_shut_calls == 1);
    CHECK(replacement.dependency_reads == 1);
    CHECK(consumer.dependency_reads == 2);
}

TEST_CASE("Shutdown retains resources after a required provider is removed") {
    ark::plugin_manager manager;
    observed_module consumer("consumer"), provider("provider");
    consumer.dependencies = {"provider"};
    REQUIRE(manager.add_module(consumer.get_name(), &consumer));
    REQUIRE(manager.add_module(provider.get_name(), &provider));
    REQUIRE(manager.start(0, {}, {}, {}));
    manager.remove_module(provider.get_name());
    CHECK_FALSE(manager.stop(1));
    CHECK(manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::failed);
    CHECK(manager.get_shutdown_failure() == "shutdown: consumer requires missing module 'provider'");
    CHECK(manager.get_startup_failure().empty());
    CHECK_FALSE(manager.stop(2));
    CHECK(consumer.dependency_reads == 2);
    CHECK(consumer.pre_shut_calls == 0);
    CHECK(provider.pre_shut_calls == 0);
}

TEST_CASE("Shutdown uses unchanged cached metadata but validates an invalidated graph once") {
    for (const bool changed : {false, true})
    {
        ark::plugin_manager manager;
        observed_module module("module"), added("added");
        REQUIRE(manager.add_module(module.get_name(), &module));
        REQUIRE(manager.start(0, {}, {}, {}));
        module.dependency_metadata_throws = true;
        if (changed)
        {
            REQUIRE(manager.add_module(added.get_name(), &added));
            CHECK_FALSE(manager.stop(1));
            CHECK(manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::failed);
            CHECK(manager.get_shutdown_failure() == "shutdown: module");
            REQUIRE(manager.get_shutdown_exception());
            CHECK_THROWS_WITH_AS(std::rethrow_exception(manager.get_shutdown_exception()),
                "dependency metadata must not be read again", std::runtime_error);
            CHECK_FALSE(manager.stop(2));
            CHECK(module.dependency_reads == 2);
            CHECK(module.pre_shut_calls == 0);
            CHECK(added.pre_shut_calls == 0);
        }
        else
        {
            CHECK(manager.stop(1));
            CHECK(module.dependency_reads == 1);
            CHECK(module.pre_shut_calls == 1);
        }
    }
}

TEST_CASE("Shutdown rejects a cyclic manual graph before calling any hook") {
    ark::plugin_manager manager;
    observed_module module("self");
    module.dependencies = {"self"};
    REQUIRE(manager.add_module(module.get_name(), &module));
    CHECK_FALSE(manager.stop(1));
    CHECK(manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::failed);
    CHECK(manager.get_shutdown_failure() == "shutdown: cycle prevents shutdown; blocked modules: self");
    CHECK(module.pre_shut_calls == 0);
}

TEST_CASE("A successful readiness hook cannot enter cleanup after its deadline") {
    ark::plugin_manager manager;
    observed_module module("readiness-deadline");
    REQUIRE(manager.add_module(module.get_name(), &module));
    module.on_readiness = [&] {
        CHECK_FALSE(manager.stop_until(2, std::chrono::steady_clock::now() - std::chrono::seconds(1)));
        return true;
    };
    CHECK_FALSE(manager.stop_until(1, std::chrono::steady_clock::now() + std::chrono::hours(1)));
    CHECK(manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::timed_out);
    CHECK(manager.get_shutdown_failure() == "ready_to_shut: readiness-deadline");
    CHECK(module.readiness_calls == 1);
    CHECK(module.pre_shut_calls == 0);
    CHECK(module.shut_calls == 0);
    CHECK(manager.update(3));
    CHECK_FALSE(manager.stop(4));
    CHECK(module.readiness_calls == 1);
}

TEST_CASE("Cleanup deadline retains later resources and disables module updates") {
    ark::plugin_manager manager;
    observed_module module("cleanup-deadline");
    REQUIRE(manager.add_module(module.get_name(), &module));
    REQUIRE(manager.add_update_module(&module));
    unsigned updates{};
    module.on_update = [&] { ++updates; };
    module.on_pre_shut = [&] {
        CHECK_FALSE(manager.stop_until(2, std::chrono::steady_clock::now() - std::chrono::seconds(1)));
    };
    CHECK_FALSE(manager.stop_until(1, std::chrono::steady_clock::now() + std::chrono::hours(1)));
    CHECK(manager.get_shutdown_state() == ark::plugin_manager::shutdown_state::cleanup_timed_out);
    CHECK(manager.get_shutdown_failure() == "pre_shut: cleanup-deadline");
    CHECK(module.pre_shut_calls == 1);
    CHECK(module.shut_calls == 0);
    CHECK_FALSE(manager.update(3));
    CHECK(updates == 0);
    CHECK_FALSE(manager.stop(4));
    CHECK_FALSE(manager.stop_until(5, std::chrono::steady_clock::time_point::max()));
    CHECK(module.pre_shut_calls == 1);
    CHECK(module.shut_calls == 0);
}

TEST_CASE("Expired and future manager deadlines preserve terminal stop semantics") {
    ark::plugin_manager expired;
    CHECK_FALSE(expired.stop_until(1, std::chrono::steady_clock::now() - std::chrono::seconds(1)));
    CHECK(expired.get_shutdown_state() == ark::plugin_manager::shutdown_state::timed_out);
    CHECK_FALSE(expired.stop(2));
    CHECK(expired.update(3));

    ark::plugin_manager normal;
    observed_module module("within-deadline");
    REQUIRE(normal.add_module(module.get_name(), &module));
    CHECK(normal.stop_until(1, std::chrono::steady_clock::now() + std::chrono::hours(1)));
    CHECK(normal.get_shutdown_state() == ark::plugin_manager::shutdown_state::stopped);
    CHECK(normal.stop(2));
    CHECK(normal.stop_until(3, std::chrono::steady_clock::now() - std::chrono::seconds(1)));
    CHECK(module.pre_shut_calls == 1);
    CHECK(module.shut_calls == 1);
}
