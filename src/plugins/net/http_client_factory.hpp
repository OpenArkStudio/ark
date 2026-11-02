// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

#include <memory>

#include "ark/net/http_client_module.hpp"

// Plugin-private construction; not part of the public header/install mapping.
namespace ark::detail {

std::unique_ptr<http_client_module> make_http_client_module(plugin_manager* manager = nullptr);

} // namespace ark::detail
