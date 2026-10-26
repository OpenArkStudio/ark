// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

#include <memory>

#include "ark/kafka/kafka_module.hpp"

// Plugin-private construction; deliberately absent from public header mappings.
namespace ark::detail {

std::unique_ptr<kafka_module> make_kafka_module();

} // namespace ark::detail
