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

#include "ark/container/map.hpp"
#include "ark/kernel/static_entity_impl.hpp"

namespace ark {

class static_entity_manager final {
public:
    using static_object_list = shared_object_map<id_type, static_entity>;

    static_entity_manager() = default;
    virtual ~static_entity_manager() = default;

    std::shared_ptr<static_entity> create_static_entity(std::shared_ptr<class_meta> class_meta_ptr,
        std::shared_ptr<node_manager> node_manager_ptr, const id_type config_id) {
        ARK_ASSERT_RET_VAL(class_meta_ptr != nullptr && node_manager_ptr != nullptr, nullptr);

        // return nullptr if existed
        auto iter = static_object_list_.find(config_id);
        ARK_ASSERT_RET_VAL(iter == static_object_list_.end(), nullptr);

        // create new class
        auto object_ptr = std::make_shared<static_entity_impl>(class_meta_ptr, node_manager_ptr, config_id);
        if (!static_object_list_.insert(config_id, object_ptr).second) {
            return nullptr;
        }

        return object_ptr;
    }

    std::shared_ptr<static_entity> find_object(const id_type config_id) const {
        return static_object_list_.find_value(config_id);
    }

private:
    static_object_list static_object_list_;
};

} // namespace ark
