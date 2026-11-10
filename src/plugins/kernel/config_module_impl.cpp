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

#include "ark/kernel/config_module_impl.hpp"
#include "ark/plugin_manager.hpp"
#include "detail/entity_json_runtime.hpp"

namespace ark {

bool config_module_impl::init() {
    class_meta_module_ = find_module<class_meta_module>();

    static_entity_manager_ = ARK_NEW static_entity_manager();

    return true;
}

bool config_module_impl::post_init() {
    // Static data requires the schema loaded during the metadata module's init phase.
    return load();
}

bool config_module_impl::shut() {
    return clear();
}

bool config_module_impl::load() {
    if (loaded_) {
        return false;
    }

    auto& class_meta_list = class_meta_module_->get_static_meta_list();
    for (auto iter : class_meta_list) {
        auto class_meta = iter.second;
        const auto& config = class_meta->get_json_config();
        if (!config) {
            throw entity_json::error(
                detail::entity_json_file(), "/configs", "configuration metadata has no validated JSON rows");
        }
        detail::load_entity_json_config(*static_entity_manager_, class_meta, *config);
    }

    loaded_ = true;

    return true;
}

bool config_module_impl::save() {
    return false;
}

bool config_module_impl::clear() {
    ARK_DELETE(static_entity_manager_);
    return true;
}

std::shared_ptr<static_entity> config_module_impl::find_static_entity(const id_type config_id) {
    ARK_ASSERT_RET_VAL(static_entity_manager_ != nullptr, nullptr);

    return static_entity_manager_->find_object(config_id);
}

} // namespace ark
