#pragma once

#include "ark/plugin_base.hpp"

namespace ark {

class kafka_plugin final : public plugin_base<kafka_plugin>
{
public:
    void install() override;
    void uninstall() override;
};

} // namespace ark
