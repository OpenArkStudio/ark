// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <string_view>

#include "../network/doctest.h"

#include "ark/platform.hpp"

// The public codec must also protect a first Protobuf include from the macro.
#pragma push_macro("GetMessage")
#undef GetMessage
#define GetMessage ark_nested_wire_first_get_message_sentinel

#include "ark/kernel/nested_wire.hpp"

#define ARK_NESTED_WIRE_STRINGIZE_IMPL(value) #value
#define ARK_NESTED_WIRE_STRINGIZE(value) ARK_NESTED_WIRE_STRINGIZE_IMPL(value)
static_assert(std::string_view(ARK_NESTED_WIRE_STRINGIZE(GetMessage)) == "ark_nested_wire_first_get_message_sentinel");
#undef ARK_NESTED_WIRE_STRINGIZE
#undef ARK_NESTED_WIRE_STRINGIZE_IMPL
#pragma pop_macro("GetMessage")

#include "nested_stream_fixture.hpp"

TEST_CASE("Nested wire protects first Protobuf declarations from a caller GetMessage macro") {
    using namespace ark::entity_sync;
    const auto trusted = nested_test::binding();
    const auto initial = nested_test::initial();
    const auto bytes = nested_wire::encode(initial, trusted);
    const auto decoded = nested_wire::decode_baseline(bytes, trusted);
    CHECK(decoded.snapshot == initial.snapshot);
    nested_replica receiver(trusted);
    CHECK(receiver.apply(decoded) == nested_apply_result::applied);
    REQUIRE(receiver.state() != nullptr);
    CHECK(*receiver.state() == initial.snapshot);
}
