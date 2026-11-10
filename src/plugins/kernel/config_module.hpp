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

#include "ark/kernel/static_entity.hpp"
#include "ark/kernel/static_entity_manager.hpp"
#include "ark/module.hpp"

namespace ark {

class config_module : public module {
public:
    virtual bool load() = 0;
    virtual bool save() = 0;
    virtual bool clear() = 0;

    // find config
    virtual std::shared_ptr<static_entity> find_static_entity(const id_type config_id) = 0;
};

} // namespace ark
