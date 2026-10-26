#pragma once

#include "ark/etcd_client/etcd_client_module.hpp"
#include "ark/module_base.hpp"
#include "ark/net/http_client_module.hpp"

namespace ark {

class etcd_client_module_impl final : public module_base<etcd_client_module>
{
public:
    bool init() override;

    std::shared_ptr<etcd_client> create_etcd_client(
        std::vector<std::string> endpoints, int io_timeout = 5, int connect_timeout = 2) override;

private:
    http_client_module* http_client_module_;
};

} // namespace ark
