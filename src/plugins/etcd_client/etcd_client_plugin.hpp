#pragma once

#include "ark/plugin_base.hpp"

namespace ark {

class etcd_client_plugin final : public plugin_base<etcd_client_plugin>
{
public:
    void install() override;
    void uninstall() override;
};

} // namespace ark
