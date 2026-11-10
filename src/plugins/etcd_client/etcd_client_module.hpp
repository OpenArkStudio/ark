#pragma once

#include "ark/etcd_client/etcd_client.hpp"
#include "ark/module.hpp"

namespace ark {

class etcd_client_module : public module {
public:
    virtual std::shared_ptr<etcd_client> create_etcd_client(
        std::vector<std::string> endpoints, int io_timeout = 5, int connect_timeout = 2) = 0;
};

} // namespace ark
