#pragma once

#include <future>
#include <string>

namespace ark {

class etcd_client {
public:
    virtual ~etcd_client() = default;

    /**
     * Grant a lease
     * @param cb will be called in the same thread where AFIHttpClientModule::Update is called.
     *          <LeaseID, ErrorMessage>. Success if LeaseID >= 0, otherwise ErrorMessage containers the error message.
     * @param ttl in seconds
     */
    virtual void grant(std::function<void(std::pair<int64_t, std::string>)> cb, int ttl) = 0;
    /**
     * Keep the lease alive
     * @param cb will be called in the same thread where AFIHttpClientModule::Update is called.
     *          <Result, ErrorMessage>. Success if Result == true, otherwise ErrorMessage containers the error message.
     * @param leaseID
     */
    virtual void keepalive(std::function<void(std::pair<bool, std::string>)> cb, int64_t lease_id) = 0;
};

} // namespace ark
