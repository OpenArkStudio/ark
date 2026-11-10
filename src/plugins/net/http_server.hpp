#pragma once

#include <memory>

#include <arknet/http/http_server.hpp>

namespace ark {

class http_server {
private:
    std::shared_ptr<arknet::http_server> server_;

public:
    http_server();
    ~http_server();
};

} // namespace ark
