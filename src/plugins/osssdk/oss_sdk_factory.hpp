// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

#include <memory>

#include "ark/osssdk/oss_sdk_module.hpp"

// Plugin-private construction; deliberately absent from public header mappings.
namespace ark::detail {

std::unique_ptr<oss_sdk_module> make_oss_sdk_module();

} // namespace ark::detail
