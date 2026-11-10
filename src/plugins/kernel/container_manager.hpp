/*
 * This source file is part of ArkNX
 * For the latest info, see https://github.com/ArkNX
 *
 * Copyright (c) 2013-2020 ArkNX authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
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

#include "ark/kernel/class_meta.hpp"
#include "ark/kernel/container.hpp"

namespace ark {

class container_manager {
public:
    using container_list = shared_object_map<uint32_t, container>;

    virtual ~container_manager() = default;

    virtual std::shared_ptr<container> find_container(const uint32_t index) const = 0;
    virtual std::shared_ptr<container> create_container(
        std::shared_ptr<class_meta> class_meta_ptr, const uint32_t index, const guid_t& parent_id) = 0;
    virtual bool destroy_container(const uint32_t index) = 0;
    virtual const container_list& get_container_list() const = 0;
};

} // namespace ark
