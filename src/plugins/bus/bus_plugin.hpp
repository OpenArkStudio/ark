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

#include "ark/bus/bus_module.hpp"
#include "ark/plugin_base.hpp"

namespace ark {

class msg_module;

class bus_plugin final : public plugin_base<bus_plugin> {
public:
    void install() override;
    void uninstall() override;
};

namespace detail {
std::unique_ptr<bus_module> make_bus_module();
std::unique_ptr<msg_module> make_msg_module();
} // namespace detail

} // namespace ark
