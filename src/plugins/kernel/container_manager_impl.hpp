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

#include "ark/kernel/container_impl.hpp"
#include "ark/kernel/container_manager.hpp"

namespace ark {

class container_manager_impl final : public container_manager {
private:
    // container
    container_list container_data_;

public:
    explicit container_manager_impl() = default;

    // find container
    std::shared_ptr<container> find_container(const uint32_t index) const override;

    std::shared_ptr<container> create_container(
        std::shared_ptr<class_meta> class_meta_ptr, const uint32_t index, const guid_t& parent_id) override;

    bool destroy_container(const uint32_t index) override;

    const container_list& get_container_list() const override;
};

} // namespace ark
