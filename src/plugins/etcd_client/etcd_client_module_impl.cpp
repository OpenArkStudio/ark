#include "ark/etcd_client/etcd_client_impl.hpp"
#include "ark/etcd_client/etcd_client_module_impl.hpp"
#include "etcd_client_factory.hpp"

namespace ark {

std::unique_ptr<etcd_client_module> detail::make_etcd_client_module() {
    return std::make_unique<etcd_client_module_impl>();
}

bool etcd_client_module_impl::init() {
    http_client_module_ = find_module<http_client_module>();
    return http_client_module_ != nullptr;
}

std::shared_ptr<etcd_client> etcd_client_module_impl::create_etcd_client(
    std::vector<std::string> endpoints, int io_timeout, int connect_timeout) {
    return std::make_shared<etcd_client_impl>(http_client_module_, std::move(endpoints), connect_timeout, io_timeout);
}

} // namespace ark
