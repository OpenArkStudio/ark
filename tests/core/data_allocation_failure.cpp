// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <new>
#include <string>
#include <utility>

#include "../network/doctest.h"

#include <ark/kernel/data_impl.hpp>
#include <ark/kernel/node_impl.hpp>

namespace {

bool fail_next_array{};
constexpr char text[] = "a payload longer than the inline buffer";
constexpr char bytes[] = "binary\0payload";

template<typename F>
bool default_failure(F&& operation) {
    fail_next_array = true;
    bool threw{};
    try
    { operation(); }
    catch (const std::bad_alloc&)
    { threw = true; }
    CHECK_FALSE(fail_next_array);
    return threw;
}

struct saved_value
{
    ark::data_type type;
    std::string content;
    const void* storage{};
    int number{};
};

saved_value save(const ark::data& value) {
    saved_value result{value.get_type(), {}, nullptr, 0};
    if (result.type == ark::data_type::dt_string)
    {
        result.content = value.get_string();
        result.storage = value.get_string();
    }
    else if (result.type == ark::data_type::dt_userdata)
    {
        std::size_t size{};
        const auto* data = static_cast<const char*>(value.get_user_data(size));
        result.content.assign(data, size);
        result.storage = value.get_raw_user_data();
    }
    else
    {
        result.number = value.get_int();
    }
    return result;
}

void unchanged(const ark::data& value, const saved_value& previous) {
    const auto current = save(value);
    CHECK(current.type == previous.type);
    CHECK(current.content == previous.content);
    CHECK(current.storage == previous.storage);
    CHECK(current.number == previous.number);
}

template<typename T>
void initial_value(T& value, int kind) {
    switch (kind)
    {
    case 0:
        value.set_int(42);
        break;
    case 1:
        value.set_string("a");
        break;
    case 2:
        value.set_string("old allocated string value");
        break;
    default:
        value.set_user_data(bytes, sizeof(bytes));
        break;
    }
}

class tracked_allocator
{
public:
    enum class failure
    {
        none,
        null,
        exception
    };
    static inline failure next{failure::none};
    static inline std::size_t next_id{};
    static inline std::map<void*, std::pair<std::size_t, std::size_t>> live;

    void* alloc(std::size_t size) {
        const auto mode = std::exchange(next, failure::none);
        if (mode == failure::null)
        {
            return nullptr;
        }
        if (mode == failure::exception)
        {
            throw std::bad_alloc{};
        }
        auto* value = new char[size];
        live.emplace(value, std::make_pair(id_, size));
        return value;
    }

    void free(void* value, std::size_t size) {
        if (!value)
        {
            return;
        }
        const auto found = live.find(value);
        if (found == live.end() || found->second != std::make_pair(id_, size))
        {
            std::abort();
        }
        live.erase(found);
        delete[] static_cast<char*>(value);
    }

    void swap(tracked_allocator& other) noexcept {
        std::swap(id_, other.id_);
    }

private:
    std::size_t id_{++next_id};
};

using tracked_data = ark::data_base<8, tracked_allocator>;

} // namespace

// Exercise the real default allocator and default ARK_NEW_ARRAY_RET expansion.
// Only its standard nothrow new[] call fails; successful storage uses normal new[]/delete[].
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    if (fail_next_array)
    {
        fail_next_array = false;
        return nullptr;
    }
    try
    { return ::operator new[](size); }
    catch (...)
    { return nullptr; }
}

void operator delete[](void* value, const std::nothrow_t&) noexcept {
    ::operator delete[](value);
}

TEST_CASE("Default data allocator null string construction reports bad_alloc") {
    CHECK(default_failure([] { ark::data_impl value(ark::data_type::dt_string, text); }));
}

TEST_CASE("Default data allocator null user data construction reports bad_alloc") {
    CHECK(default_failure([] { ark::data_impl value(ark::data_type::dt_userdata, bytes, sizeof(bytes)); }));
}

TEST_CASE("Default data allocator failed setters retain scalar inline string heap string and user data") {
    for (int kind = 0; kind < 4; ++kind)
    {
        for (bool user_data : {false, true})
        {
            ark::data_impl value;
            initial_value(value, kind);
            const auto previous = save(value);
            CHECK(default_failure([&] {
                if (user_data)
                {
                    value.set_user_data(bytes, sizeof(bytes));
                }
                else
                {
                    value.set_string(text);
                }
            }));
            unchanged(value, previous);
        }
    }
}

TEST_CASE("Default data allocator failed copies and assignments retain both operands") {
    for (bool user_data : {false, true})
    {
        ark::data_impl source;
        if (user_data)
        {
            source.set_user_data(bytes, sizeof(bytes));
        }
        else
        {
            source.set_string(text);
        }
        const auto before_source = save(source);
        const ark::data& erased = source;
        for (int operation = 0; operation < 5; ++operation)
        {
            ark::data_impl target(ark::data_type::dt_string, "old allocated string value");
            const auto before_target = save(target);
            CHECK(default_failure([&] {
                switch (operation)
                {
                case 0:
                {
                    ark::data_impl copy(source);
                    break;
                }
                case 1:
                {
                    ark::data_impl copy(erased);
                    break;
                }
                case 2:
                    target = source;
                    break;
                case 3:
                    target = erased;
                    break;
                default:
                    target.assign(erased);
                    break;
                }
            }));
            unchanged(source, before_source);
            unchanged(target, before_target);
        }
    }
}

TEST_CASE("Default data allocator inline strings need no heap allocation") {
    ark::data_impl value(ark::data_type::dt_string, text);
    fail_next_array = true;
    value.set_string("abc");
    CHECK(fail_next_array);
    CHECK(std::string(value.get_string()) == "abc");
    fail_next_array = false;
}

TEST_CASE("Data setters retain self source storage until replacement is complete") {
    for (const auto input : {"abc", text})
    {
        ark::data_impl value(ark::data_type::dt_string, input);
        value.set_string(value.get_string());
        CHECK(std::string(value.get_string()) == input);
        value.set_string(value.get_string() + 1);
        CHECK(std::string(value.get_string()) == std::string(input).substr(1));
    }
    ark::data_impl value(ark::data_type::dt_string, text);
    auto previous = save(value);
    CHECK(default_failure([&] { value.set_string(value.get_string() + 1); }));
    unchanged(value, previous);
    const auto& self = value;
    CHECK(default_failure([&] { value = self; }));
    unchanged(value, previous);
    value.set_user_data(bytes, sizeof(bytes));
    previous = save(value);
    CHECK(default_failure([&] { value.set_raw_user_data(value.get_raw_user_data()); }));
    unchanged(value, previous);
    value.set_raw_user_data(value.get_raw_user_data());
    CHECK(save(value).content == std::string(bytes, sizeof(bytes)));
    std::size_t size{};
    const auto* inner = static_cast<const char*>(value.get_user_data(size));
    value.set_user_data(inner + 1, size - 1);
    CHECK(save(value).content == std::string(bytes + 1, sizeof(bytes) - 1));
}

TEST_CASE("Custom data allocators preserve old values and matching storage ownership on null and throw") {
    for (auto mode : {tracked_allocator::failure::null, tracked_allocator::failure::exception})
    {
        REQUIRE(tracked_allocator::live.empty());
        for (int kind = 0; kind < 4; ++kind)
        {
            for (int operation = 0; operation < 6; ++operation)
            {
                tracked_data value;
                initial_value(value, kind);
                tracked_data source(ark::data_type::dt_string, text);
                if (operation == 3)
                {
                    source.set_user_data(bytes, sizeof(bytes));
                }
                const auto previous = save(value);
                const auto allocations = tracked_allocator::live;
                tracked_allocator::next = mode;
                bool threw{};
                try
                {
                    switch (operation)
                    {
                    case 0:
                        value.set_string(text);
                        break;
                    case 1:
                        value.set_user_data(bytes, sizeof(bytes));
                        break;
                    case 2:
                        value = source;
                        break;
                    case 3:
                        value.assign(source);
                        break;
                    case 4:
                    {
                        tracked_data copy(source);
                        break;
                    }
                    default:
                    {
                        tracked_data copy(ark::data_type::dt_userdata, bytes, sizeof(bytes));
                        break;
                    }
                    }
                }
                catch (const std::bad_alloc&)
                { threw = true; }
                CHECK(threw);
                CHECK(tracked_allocator::next == tracked_allocator::failure::none);
                unchanged(value, previous);
                CHECK(tracked_allocator::live == allocations);
            }
        }
        CHECK(tracked_allocator::live.empty());
    }
}

TEST_CASE("Custom allocator node conversion rejects null allocation without losing old storage") {
    auto meta = std::make_shared<ark::node_meta>("value", 1);
    meta->set_type(ark::data_type::dt_string);
    ark::node_string source(meta);
    source.set_string(text);
    for (auto mode : {tracked_allocator::failure::null, tracked_allocator::failure::exception})
    {
        tracked_data value(ark::data_type::dt_string, "old allocated string value");
        const auto previous = save(value);
        tracked_allocator::next = mode;
        CHECK_THROWS_AS(value.from(&source), std::bad_alloc);
        unchanged(value, previous);
    }
    CHECK(tracked_allocator::live.empty());
}

TEST_CASE("Custom allocator self source failures retain string and user data") {
    for (auto mode : {tracked_allocator::failure::null, tracked_allocator::failure::exception})
    {
        tracked_data value(ark::data_type::dt_string, text);
        auto previous = save(value);
        tracked_allocator::next = mode;
        CHECK_THROWS_AS(value.set_string(value.get_string() + 1), std::bad_alloc);
        unchanged(value, previous);
        value.set_user_data(bytes, sizeof(bytes));
        previous = save(value);
        tracked_allocator::next = mode;
        CHECK_THROWS_AS(value.set_raw_user_data(value.get_raw_user_data()), std::bad_alloc);
        unchanged(value, previous);
    }
    CHECK(tracked_allocator::live.empty());
}

TEST_CASE("Custom allocator setters swap allocator state with inline heap and user storage") {
    {
        tracked_data value(ark::data_type::dt_string, "short");
        value.set_string(text);
        value.set_string(value.get_string() + 1);
        CHECK(std::string(value.get_string()) == std::string(text + 1));
        value.set_string("short");
        value.set_string(value.get_string() + 1);
        CHECK(std::string(value.get_string()) == "hort");
        value.set_user_data(bytes, sizeof(bytes));
        value.set_raw_user_data(value.get_raw_user_data());
        CHECK(save(value).content == std::string(bytes, sizeof(bytes)));
        value.set_string("small");
    }
    CHECK(tracked_allocator::live.empty());
}

TEST_CASE("Failed string replacement preserves GUID storage in the selected GUID mode") {
    const auto expected = std::numeric_limits<ark::guid_t>::max();
    auto value = ark::data_impl::from_guid(expected);
    CHECK(default_failure([&] { value.set_string(text); }));
    CHECK(value.get_type() == ark::data_type::dt_guid);
#if defined(ARK_USE_GUID_32)
    CHECK(value.get_int() == expected);
#else
    CHECK(value.get_int64() == expected);
#endif
    ark::data_impl copy(value);
    CHECK(copy.get_type() == ark::data_type::dt_guid);
    CHECK(copy.equal(value));
    value.set_string("abc");
    CHECK(std::string(value.get_string()) == "abc");
    CHECK(copy.get_type() == ark::data_type::dt_guid);
}

TEST_CASE("Zero length user data retains its allocation header and replacement guarantee") {
    ark::data_impl value(ark::data_type::dt_userdata, bytes, 0);
    std::size_t size = 1;
    CHECK(value.get_user_data(size) != nullptr);
    CHECK(size == 0);
    const auto previous = save(value);
    CHECK(default_failure([&] { value.set_user_data(bytes, 0); }));
    unchanged(value, previous);
    value.set_raw_user_data(value.get_raw_user_data());
    size = 1;
    CHECK(value.get_user_data(size) != nullptr);
    CHECK(size == 0);
}
