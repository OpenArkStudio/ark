// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <string_view>

#include "../network/doctest.h"
#include <google/protobuf/message.h>

#include "ark/platform.hpp"

// Reproduce Win32's object-like macro after Reflection's declaration is fixed.
#pragma push_macro("GetMessage")
#undef GetMessage
#define GetMessage ark_nested_wire_get_message_sentinel

#include "ark/kernel/nested_wire.hpp"

#define ARK_NESTED_WIRE_STRINGIZE_IMPL(value) #value
#define ARK_NESTED_WIRE_STRINGIZE(value) ARK_NESTED_WIRE_STRINGIZE_IMPL(value)
static_assert(std::string_view(ARK_NESTED_WIRE_STRINGIZE(GetMessage)) == "ark_nested_wire_get_message_sentinel");
#undef ARK_NESTED_WIRE_STRINGIZE
#undef ARK_NESTED_WIRE_STRINGIZE_IMPL
#pragma pop_macro("GetMessage")

#include "nested_stream_fixture.hpp"

TEST_CASE("Nested wire preserves the caller GetMessage macro while round-tripping a baseline") {
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
