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

#include <memory>
#include <new>
#include <stdexcept>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "network/doctest.h"

#include "ark/logger.hpp"
#include "ark/pattern/singleton.hpp"

namespace {

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

} // namespace

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
    CHECK_THROWS_AS(throwing_singleton::instance(), std::runtime_error);
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
