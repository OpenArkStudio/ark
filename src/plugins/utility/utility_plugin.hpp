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

#pragma once

#include <memory>

#include "ark/plugin_base.hpp"
#include "ark/utility/compress_module.hpp"
#include "ark/utility/guid_module.hpp"
#include "ark/utility/schedule_module.hpp"
#include "ark/utility/timer_module.hpp"

namespace ark {

class utility_plugin final : public plugin_base<utility_plugin> {
public:
    void install() override;
    void uninstall() override;
};

// Plugin-private factories return public interfaces; this header is not staged in the SDK.
namespace detail {
std::unique_ptr<guid_module> make_guid_module();
std::unique_ptr<timer_module> make_timer_module();
std::unique_ptr<schedule_module> make_schedule_module();
std::unique_ptr<compress_module> make_compress_module();
} // namespace detail

} // namespace ark
