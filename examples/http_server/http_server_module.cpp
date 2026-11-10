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

#include "ark/http/http_server_module.hpp"
#include "ark/module_base.hpp"
#include "http_server_plugin.hpp"

namespace ark {
namespace {

class http_server_sample_module_impl final : public module_base<http_server_sample_module> {
public:
    bool init() override;
    bool post_init() override;

private:
    http_server_module* http_server_{};
};

bool http_server_sample_module_impl::init() {
    http_server_ = find_module<http_server_module>();
    return http_server_ != nullptr;
}

bool http_server_sample_module_impl::post_init() {
    http_server_->start_server("0.0.0.0", 8080, 4);
    return true;
}

} // namespace

std::unique_ptr<http_server_sample_module> make_http_server_sample_module() {
    return std::make_unique<http_server_sample_module_impl>();
}

} // namespace ark
