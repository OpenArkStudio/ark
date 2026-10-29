// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <string>

#include "../network/doctest.h"

#include "ark/kernel/entity_stream.hpp"

namespace {

bool armed{};
std::size_t allocation_count{}, failure_position{};

class allocation_failure final
{
public:
    explicit allocation_failure(std::size_t position) {
        allocation_count = 0;
        failure_position = position;
        armed = true;
    }

    ~allocation_failure() {
        armed = false;
    }
};

using namespace ark::entity_sync;

scalar_baseline initial() {
    return {std::string(128, 's'), 7, 1,
        {17, "Player",
            {{1, false}, {2, std::numeric_limits<std::int32_t>::min()}, {3, std::numeric_limits<std::uint32_t>::max()},
                {4, std::numeric_limits<std::int64_t>::min()}, {5, std::numeric_limits<std::uint64_t>::max()},
                {6, 0.5f}, {7, 1.5}, {8, std::string(4096, 'x')}, {9, guid_value{42}}, {10, std::string(4096, '\0')}}}};
}

scalar_snapshot changed(const scalar_baseline& source) {
    auto next = source.snapshot;
    next.fields.at(8) = std::string(8192, 'y');
    next.fields.at(2) = std::int32_t{19};
    return next;
}

} // namespace

void* operator new(std::size_t size) {
    if (armed && ++allocation_count == failure_position)
    {
        throw std::bad_alloc{};
    }
    if (void* result = std::malloc(size ? size : 1))
    {
        return result;
    }
    throw std::bad_alloc{};
}

void operator delete(void* pointer) noexcept {
    std::free(pointer);
}

void operator delete(void* pointer, std::size_t) noexcept {
    std::free(pointer);
}

TEST_CASE("Scalar publisher construction and baseline copies preserve their source at every failed allocation") {
    const auto source = initial();
    const auto untouched = source.snapshot;
    for (const bool construct : {true, false})
    {
        bool completed = false;
        std::size_t failures{};
        scalar_publisher original(source);
        for (std::size_t position = 1; position < 256 && !completed; ++position)
        {
            std::optional<scalar_publisher> publisher;
            std::optional<scalar_baseline> baseline;
            bool failed = false;
            {
                allocation_failure failure(position);
                try
                {
                    if (construct)
                    {
                        publisher.emplace(source);
                    }
                    else
                    {
                        baseline.emplace(original.baseline());
                    }
                    completed = true;
                }
                catch (const std::bad_alloc&)
                { failed = true; }
            }
            failures += failed;
            CHECK(source.snapshot == untouched);
            const auto retained = original.baseline();
            CHECK(retained.snapshot == untouched);
            CHECK(retained.generation == 7);
            CHECK(retained.revision == 1);
            if (completed)
            {
                CHECK((construct ? publisher->baseline().snapshot : baseline->snapshot) == untouched);
            }
        }
        CHECK(completed);
        CHECK(failures > 0);
    }
}

TEST_CASE("Scalar publisher allocation failure preserves revision and permits one successful retry") {
    const auto source = initial();
    const auto next = changed(source);
    const auto untouched = next;
    bool completed = false;
    std::size_t failures{};
    for (std::size_t position = 1; position < 256 && !completed; ++position)
    {
        scalar_publisher publisher(source);
        bool failed = false;
        std::shared_ptr<const scalar_delta> packet;
        {
            allocation_failure failure(position);
            try
            { packet = publisher.update(next); }
            catch (const std::bad_alloc&)
            { failed = true; }
        }
        failures += failed;
        CHECK(next == untouched);
        if (failed)
        {
            const auto before = publisher.baseline();
            CHECK(before.snapshot == source.snapshot);
            CHECK(before.revision == 1);
            CHECK_FALSE(packet);
            packet = publisher.update(next);
        }
        REQUIRE(packet);
        CHECK(packet->required_revision == 1);
        CHECK(packet->revision == 2);
        CHECK(publisher.baseline().snapshot == next);
        CHECK_FALSE(publisher.update(next));
        completed = !failed;
    }
    CHECK(completed);
    CHECK(failures > 0);
}

TEST_CASE("Scalar baseline allocation failures retain first-install and prior-generation state") {
    const auto source = initial();
    auto replacement = source;
    replacement.generation = 8;
    replacement.snapshot = changed(source);
    for (const bool installed : {false, true})
    {
        bool completed = false;
        std::size_t failures{};
        for (std::size_t position = 1; position < 256 && !completed; ++position)
        {
            scalar_replica replica(source.schema_id, 17, "Player");
            if (installed)
            {
                REQUIRE(replica.apply(source) == scalar_apply_result::applied);
            }
            const auto retained = replica.state();
            const auto& incoming = installed ? replacement : source;
            bool failed = false;
            {
                allocation_failure failure(position);
                try
                { completed = replica.apply(incoming) == scalar_apply_result::applied; }
                catch (const std::bad_alloc&)
                { failed = true; }
            }
            failures += failed;
            if (failed)
            {
                CHECK(replica.state() == retained);
                CHECK(replica.valid() == installed);
                CHECK(replica.revision() == (installed ? 1 : 0));
                if (installed)
                {
                    CHECK(replica.apply(source) == scalar_apply_result::duplicate);
                }
                REQUIRE(replica.apply(incoming) == scalar_apply_result::applied);
            }
            CHECK(*replica.state() == incoming.snapshot);
            CHECK(replica.apply(incoming) == scalar_apply_result::duplicate);
            if (retained)
            {
                CHECK(*retained == source.snapshot);
            }
        }
        CHECK(completed);
        CHECK(failures > 0);
    }
}

TEST_CASE("Scalar delta allocation failures preserve committed state and exactly one revision advance") {
    const auto source = initial();
    const auto next = changed(source);
    scalar_publisher publisher(source);
    const auto packet = publisher.update(next);
    REQUIRE(packet);
    bool completed = false;
    std::size_t failures{};
    for (std::size_t position = 1; position < 256 && !completed; ++position)
    {
        scalar_replica replica(source.schema_id, 17, "Player");
        REQUIRE(replica.apply(source) == scalar_apply_result::applied);
        const auto retained = replica.state();
        bool failed = false;
        {
            allocation_failure failure(position);
            try
            { completed = replica.apply(*packet) == scalar_apply_result::applied; }
            catch (const std::bad_alloc&)
            { failed = true; }
        }
        failures += failed;
        if (failed)
        {
            CHECK(replica.state() == retained);
            CHECK(replica.valid());
            CHECK(replica.revision() == 1);
            REQUIRE(replica.apply(*packet) == scalar_apply_result::applied);
        }
        CHECK(*replica.state() == next);
        CHECK(replica.revision() == 2);
        CHECK(replica.apply(*packet) == scalar_apply_result::duplicate);
        CHECK(*retained == source.snapshot);
    }
    CHECK(completed);
    CHECK(failures > 0);
}
