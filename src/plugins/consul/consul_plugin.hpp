#pragma once

#include "ark/plugin_base.hpp"

namespace ark {

class consul_plugin final : public plugin_base<consul_plugin>
{
public:
    void install() override;
    void uninstall() override;
};

} // namespace ark
