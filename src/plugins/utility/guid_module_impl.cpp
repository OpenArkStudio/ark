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

#include "ark/bus.hpp"
#include "ark/define.hpp"
#include "ark/module_base.hpp"
#include "ark/time/date_time.hpp"
#include "ark/uid_generator.hpp"
#include "ark/utility/guid_module.hpp"
#include "utility_plugin.hpp"

namespace ark {

class guid_module_impl final : public module_base<guid_module> {
public:
    bool init() override;

    guid_t create_guid() override;
    std::string parse_uid(const guid_t& id) override;

private:
#ifdef ARK_THREAD_SAFE
    std::unique_ptr<uid_generator_thread_safe> uid_generator_{nullptr};
#else
    std::unique_ptr<uid_generator> uid_generator_{nullptr};
#endif // ARK_THREAD_SAFE
};

bool guid_module_impl::init() {
#ifdef ARK_THREAD_SAFE
    uid_generator_ = std::make_unique<uid_generator_thread_safe>();
#else
    uid_generator_ = std::make_unique<uid_generator>();
#endif //ARK_THREAD_SAFE

    return true;
}

guid_t guid_module_impl::create_guid() {
    ARK_ASSERT_RET_VAL(uid_generator_ != nullptr, null_guid);

#if defined(ARK_USE_GUID_32)
    return uid_generator_->get_uid();
#elif defined(ARK_USE_GUID_128)
#else
    bus_addr bus_addr(get_plugin_manager()->get_bus_id());
    int64_t id = int64_t(bus_addr.zone_id) << (2 * 8) | int64_t(bus_addr.app_type) << (1 * 8) |
                 int64_t(bus_addr.inst_id) << (0 * 8);
    return uid_generator_->get_uid(id);
#endif
}

std::string guid_module_impl::parse_uid(const guid_t& id) {
    return uid_generator_->parse_uid(id);
}

namespace detail {
std::unique_ptr<guid_module> make_guid_module() {
    return std::make_unique<guid_module_impl>();
}
} // namespace detail

} // namespace ark
