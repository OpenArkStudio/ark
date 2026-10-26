// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

#include "ark/plugin_base.hpp"

namespace ark::guest_room {

class plugin final : public plugin_base<plugin>
{
public:
    void install() override;
    void uninstall() override;
};

} // namespace ark::guest_room
