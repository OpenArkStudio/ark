// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <stdexcept>

#include "../network/doctest.h"

#include "detail/database_request_control.hpp"

using ark::detail::database_request_control;
using stage = database_request_control::stage;

TEST_CASE("Native inline rejection with zero ID completes without deadlocking") {
    database_request_control control;
    const auto submission = control.begin(stage::command);
    const auto native_submit = [&] {
        CHECK(control.complete(submission));
        CHECK(control.finish());
        return 0;
    };
    CHECK_FALSE(control.publish(submission, native_submit()));
    CHECK_FALSE(control.cancel().active);
}

TEST_CASE("Cancellation before native ID publication is forwarded when the ID arrives") {
    database_request_control control;
    const auto submission = control.begin(stage::acquire);
    const auto requested = control.cancel();
    CHECK(requested.active);
    CHECK_FALSE(requested.native);
    const auto deferred = control.publish(submission, 41);
    REQUIRE(deferred);
    CHECK(deferred->target == stage::acquire);
    CHECK(deferred->id == 41);
    CHECK(control.complete(submission));
    CHECK(control.finish());
}

TEST_CASE("A completed stage cannot publish an old ID over the next command") {
    database_request_control control;
    const auto acquire = control.begin(stage::acquire);
    CHECK(control.complete(acquire));
    const auto command = control.begin(stage::command);
    CHECK(control.cancel().active);
    CHECK_FALSE(control.publish(acquire, 12));
    const auto deferred = control.publish(command, 27);
    REQUIRE(deferred);
    CHECK(deferred->target == stage::command);
    CHECK(deferred->id == 27);
    CHECK_FALSE(control.complete(acquire));
    CHECK(control.complete(command));
}

TEST_CASE("Completion before native submit returns preserves the next stage ID") {
    database_request_control control;
    const auto prepare = control.begin(stage::command);
    CHECK(control.complete(prepare));
    const auto execute = control.begin(stage::command);
    CHECK_FALSE(control.publish(execute, 8));
    CHECK_FALSE(control.publish(prepare, 7));
    const auto cancellation = control.cancel();
    REQUIRE(cancellation.native);
    CHECK(cancellation.native->id == 8);
    CHECK(control.complete(execute));
    CHECK(control.finish());
    CHECK_FALSE(control.publish(execute, 8));
}

TEST_CASE("A throwing native initiation rolls its phase back before terminal completion") {
    database_request_control control;
    const auto submission = control.begin(stage::acquire);
    try {
        throw std::runtime_error("native initiation failed");
    } catch (const std::runtime_error&) {
        CHECK(control.complete(submission));
        CHECK(control.finish());
    }
    CHECK_FALSE(control.publish(submission, 1));
    CHECK_FALSE(control.cancel().active);
}
