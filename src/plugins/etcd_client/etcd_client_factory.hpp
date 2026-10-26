// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

#include <memory>

#include "ark/etcd_client/etcd_client_module.hpp"

// Plugin-private construction; deliberately absent from public header mappings.
namespace ark::detail {

std::unique_ptr<etcd_client_module> make_etcd_client_module();

} // namespace ark::detail
