/*
 * This source file is part of ARK
 * For the latest info, see https://github.com/ArkNX
 *
 * Copyright (c) 2013-2020 ArkNX authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"),
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <iostream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "ark/etcd_client/etcd_client_module.hpp"
#include "ark/module_base.hpp"
#include "ark/net/http_client_module.hpp"
#include "http_client_sample_plugin.hpp"

using namespace std;

namespace ark {
namespace {

class http_client_sample_module_impl final : public module_base<http_client_sample_module> {
public:
    bool init() override;
    bool post_init() override;

private:
    http_client_module* http_client_{};
    std::shared_ptr<etcd_client> etcd_client_;
};

bool http_client_sample_module_impl::init() {
    http_client_ = find_module<http_client_module>();
    auto* etcd = find_module<etcd_client_module>();
    if (!http_client_ || !etcd) {
        return false;
    }
    etcd_client_ =
        etcd->create_etcd_client(vector<string>{"172.20.3.74:2379" /*, "172.20.3.75:2379", "172.20.3.76:2379" */});
    return http_client_ != nullptr && etcd_client_ != nullptr;
}

bool http_client_sample_module_impl::post_init() {
    std::cout << "Start testing Http Client" << endl;

    string body = "{\"key\": \"Zm9v\", \"value\": \"YmFy\"}";
    auto future = http_client_->post("172.20.3.74", 2379, "/v3/kv/put", nullptr, &body);
    auto val = future.get();
    std::cout << this_thread::get_id() << "    " << val.first << "    " << val.second << endl;

    http_client_->post(
        [](pair<bool, string> rsp) {
            cout << this_thread::get_id() << "    " << rsp.first << "    " << rsp.second << endl;
        },
        "172.20.3.74", 2379, "/v3/kv/put", nullptr, &body);

    etcd_client_->grant([](pair<int64_t, string> rsp) { cout << rsp.first << "  |  " << rsp.second << endl; }, 100);

    std::cout << "Finish testing Http Client" << endl;
    return true;
}

} // namespace

std::unique_ptr<http_client_sample_module> make_http_client_sample_module() {
    return std::make_unique<http_client_sample_module_impl>();
}

} // namespace ark
