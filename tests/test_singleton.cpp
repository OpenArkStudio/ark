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

#include <array>
#include <atomic>
#include <barrier>
#include <cstddef>
#include <iostream>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string_view>
#include <thread>

#define DOCTEST_CONFIG_IMPLEMENT
#include "network/doctest.h"

#include "ark/logger.hpp"
#include "ark/pattern/singleton.hpp"

namespace {

// Flush independent progress before the process guard can terminate a hung case.
class singleton_progress : public doctest::IReporter
{
public:
    explicit singleton_progress(const doctest::ContextOptions&) {
    }

    void report_query(const doctest::QueryData&) override {
    }

    void test_run_start() override {
    }

    void test_run_end(const doctest::TestRunStats&) override {
    }

    void test_case_start(const doctest::TestCaseData& test) override {
        std::cerr << "[singleton] case: " << test.m_name << std::endl;
    }

    void test_case_reenter(const doctest::TestCaseData& test) override {
        test_case_start(test);
    }

    void test_case_end(const doctest::CurrentTestCaseStats&) override {
        std::cerr << "[singleton] case complete" << std::endl;
    }

    void test_case_exception(const doctest::TestCaseException&) override {
    }

    void subcase_start(const doctest::SubcaseSignature&) override {
    }

    void subcase_end() override {
    }

    void log_assert(const doctest::AssertData& assertion) override {
        std::cerr << "[singleton] assertion completed: " << assertion.m_file << ':' << assertion.m_line << ' '
                  << assertion.m_expr << (assertion.m_failed ? " FAILED" : " passed") << std::endl;
    }

    void log_message(const doctest::MessageData&) override {
    }

    void test_case_skipped(const doctest::TestCaseData&) override {
    }
};

DOCTEST_REGISTER_LISTENER("singleton-progress", 0, singleton_progress);

class concurrent_singleton : public ark::singleton<concurrent_singleton>
{
public:
    inline static std::atomic<unsigned> constructions{};

    concurrent_singleton() {
        ++constructions;
    }
};

template<int identity>
class test_singleton : public ark::singleton<test_singleton<identity>>
{
public:
    inline static int constructions = 0;
    inline static int destructions = 0;
    int value = 0;

    test_singleton() {
        ++constructions;
    }

    ~test_singleton() {
        ++destructions;
    }
};

class throwing_singleton : public ark::singleton<throwing_singleton>
{
public:
    inline static int attempts = 0;
    inline static int destructions = 0;

    throwing_singleton() {
        if (++attempts == 1)
        {
            throw std::runtime_error("singleton construction failed");
        }
    }

    ~throwing_singleton() {
        ++destructions;
    }
};

class allocation_failure_singleton : public ark::singleton<allocation_failure_singleton>
{
public:
    inline static int allocations = 0;

    static void* operator new(std::size_t, const std::nothrow_t&) noexcept {
        ++allocations;
        return nullptr;
    }

    static void operator delete(void*) noexcept {
    }
};

// Runs only in an isolated diagnostic process, never as a singleton assertion.
int probe_call_once() {
    std::once_flag flag;
    unsigned calls{};
    bool caught{};
    std::cerr << "[call_once probe] first-begin" << std::endl;
    try
    {
        std::call_once(flag, [&] {
            ++calls;
            throw std::runtime_error("expected call_once diagnostic exception");
        });
    }
    catch (const std::runtime_error&)
    { caught = true; }
    if (!caught || calls != 1)
    {
        std::cerr << "[call_once probe] unexpected first-call result" << std::endl;
        return 1;
    }
    std::cerr << "[call_once probe] retry-begin" << std::endl;
    std::call_once(flag, [&] { ++calls; });
    std::cerr << "[call_once probe] retry-end; calls=" << calls << std::endl;
    return calls == 2 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--probe-call-once")
    {
        return probe_call_once();
    }
    return doctest::Context(argc, argv).run();
}

TEST_CASE("singleton owns lazy construction until explicit shutdown") {
    using single = test_singleton<0>;
    auto* pl = single::instance();
    auto& pr = single::get_instance();

    pl->value = 1024;

    CHECK(pr.value == 1024);
    CHECK(&pr == pl);
    CHECK(single::constructions == 1);
    CHECK(single::destructions == 0);

    single::shutdown();
    CHECK(single::destructions == 1);
    CHECK(single::instance() == nullptr);
    single::shutdown();
    CHECK(single::destructions == 1);
}

TEST_CASE("singleton shutdown borrows injected storage without deleting it") {
    using single = test_singleton<1>;
    auto external = std::make_unique<single>();
    single::set_instance(external.get());
    CHECK(single::instance() == external.get());

    single::shutdown();
    CHECK(single::destructions == 0);
    if (single::destructions != 0)
    {
        // Keep the regression safe when run against the old deleting implementation.
        external.release();
    }
    CHECK(single::instance() == nullptr);
    single::shutdown();
    external.reset();
    CHECK(single::destructions == 1);
}

TEST_CASE("singleton shutdown before first access prevents lazy construction") {
    using single = test_singleton<2>;
    single::shutdown();
    single::shutdown();
    CHECK(single::instance() == nullptr);
    CHECK(single::constructions == 0);
    single::shutdown();
}

TEST_CASE("singleton retains an owned instance across temporary borrowing") {
    using single = test_singleton<3>;
    auto* owned = single::instance();
    {
        single external;
        single::set_instance(&external);
        CHECK(single::instance() == &external);
        CHECK(single::destructions == 0);
        single::set_instance(owned);
        CHECK(single::instance() == owned);
    }
    CHECK(single::destructions == 1);
    single::shutdown();
    CHECK(single::destructions == 2);
    CHECK(single::instance() == nullptr);
}

TEST_CASE("singleton shutdown releases retained ownership while borrowing another instance") {
    using single = test_singleton<4>;
    single::instance();
    single external;
    single::set_instance(&external);
    single::shutdown();
    CHECK(single::destructions == 1);
    external.value = 42;
    CHECK(external.value == 42);
    single::shutdown();
    CHECK(single::destructions == 1);
}

TEST_CASE("singleton can replace borrowed instances and detach before initialization") {
    using single = test_singleton<5>;
    single first;
    single second;
    single::set_instance(&first);
    single::set_instance(&second);
    single::set_instance(nullptr);
    auto* owned = single::instance();
    CHECK(owned != nullptr);
    CHECK(owned != &first);
    CHECK(owned != &second);
    CHECK(single::constructions == 3);
    CHECK(single::destructions == 0);
    single::set_instance(nullptr);
    CHECK(single::instance() == nullptr);
    single::shutdown();
    CHECK(single::destructions == 1);
}

TEST_CASE("singleton permits explicit borrowing after shutdown without lazy reconstruction") {
    using single = test_singleton<6>;
    single::shutdown();
    single external;
    single::set_instance(&external);
    CHECK(single::instance() == &external);
    CHECK(&single::get_instance() == &external);
    single::set_instance(nullptr);
    CHECK(single::instance() == nullptr);
    CHECK(single::constructions == 1);
    single::shutdown();
    CHECK(single::destructions == 0);
}

TEST_CASE("singleton detaches an initialized borrow without constructing an owned instance") {
    using single = test_singleton<7>;
    single external;
    single::set_instance(&external);
    CHECK(single::instance() == &external);
    single::set_instance(nullptr);
    CHECK(single::instance() == nullptr);
    CHECK(single::constructions == 1);
    single::shutdown();
    CHECK(single::destructions == 0);
}

TEST_CASE("singleton constructor exceptions propagate and permit initialization retry") {
    std::cerr << "[singleton] invoking first constructor; expecting runtime_error" << std::endl;
    CHECK_THROWS_AS(throwing_singleton::instance(), std::runtime_error);
    std::cerr << "[singleton] invoking constructor retry after exception" << std::endl;
    CHECK(throwing_singleton::instance() != nullptr);
    CHECK(throwing_singleton::attempts == 2);
    throwing_singleton::shutdown();
    CHECK(throwing_singleton::destructions == 1);
    CHECK(throwing_singleton::instance() == nullptr);
}

TEST_CASE("singleton nothrow allocation failure returns null without implicit retries") {
    CHECK(allocation_failure_singleton::instance() == nullptr);
    CHECK(allocation_failure_singleton::instance() == nullptr);
    CHECK(allocation_failure_singleton::allocations == 1);
    allocation_failure_singleton::shutdown();
    CHECK(allocation_failure_singleton::instance() == nullptr);
}

TEST_CASE("singleton synchronizes concurrent lazy initialization") {
    constexpr std::size_t count = 8;
    std::barrier start(static_cast<std::ptrdiff_t>(count));
    std::array<concurrent_singleton*, count> results{};
    std::array<std::thread, count> threads;
    std::cerr << "[singleton] starting concurrent instance calls and joining all callers" << std::endl;
    for (std::size_t i = 0; i < count; ++i)
    {
        threads[i] = std::thread([&, i] {
            start.arrive_and_wait();
            results[i] = concurrent_singleton::instance();
        });
    }
    for (auto& thread : threads)
    {
        thread.join();
    }
    REQUIRE(results[0] != nullptr);
    for (auto* result : results)
    {
        CHECK(result == results[0]);
    }
    CHECK(concurrent_singleton::constructions.load() == 1);
    concurrent_singleton::shutdown();
    CHECK(concurrent_singleton::instance() == nullptr);
}

TEST_CASE("logger owns default initialization and borrows host injection") {
    ark::logger::init(nullptr);
    auto* owned = ark::logger::instance();
    REQUIRE(owned != nullptr);
    ark::logger::init(nullptr);
    CHECK(ark::logger::instance() == owned);
    ark::logger external;
    ark::logger::init(&external);
    CHECK(ark::logger::instance() == &external);
    ark::logger::init(owned);
    CHECK(ark::logger::instance() == owned);
    ark::logger::init(&external);
    ark::logger::shutdown();
    CHECK(ark::logger::instance() == nullptr);
    ark::logger::init(nullptr);
    CHECK(ark::logger::instance() == nullptr);
    ark::logger::init(&external);
    CHECK(ark::logger::instance() == &external);
    ark::logger::shutdown();
}
