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

#include "ark/kernel/class_meta_module_impl.hpp"
#include "ark/plugin_manager.hpp"
#include "detail/entity_json_runtime.hpp"

namespace ark {

bool class_meta_module_impl::init() {
    class_meta_manager_ = ARK_NEW class_meta_manager;

    try {
        if (!load()) {
            ARK_DELETE(class_meta_manager_);
            return false;
        }
    } catch (...) {
        ARK_DELETE(class_meta_manager_);
        throw;
    }

    return true;
}

bool class_meta_module_impl::shut() {
    ARK_DELETE(class_meta_manager_);

    return true;
}

bool class_meta_module_impl::load() {
    const auto document = entity_json::load(detail::entity_json_file());
    detail::load_entity_json_metadata(*class_meta_manager_, document);
    return true;
}

bool class_meta_module_impl::add_class_call_back(
    const std::string& class_name, class_event_functor&& cb, const int32_t prio) {
    auto class_meta_ptr = class_meta_manager_->find_meta(class_name);
    ARK_ASSERT_RET_VAL(class_meta_ptr != nullptr, false);

    auto callback_ptr = class_meta_ptr->get_class_call_back_manager();
    ARK_ASSERT_RET_VAL(callback_ptr != nullptr, false);

    return callback_ptr->add_class_call_back(std::move(cb), prio);
}

bool class_meta_module_impl::do_class_event(
    const guid_t& id, const std::string& class_name, const entity_event class_event, const data_list& args) {
    auto class_meta_ptr = class_meta_manager_->find_meta(class_name);
    ARK_ASSERT_RET_VAL(class_meta_ptr != nullptr, false);

    auto callback_ptr = class_meta_ptr->get_class_call_back_manager();
    ARK_ASSERT_RET_VAL(callback_ptr != nullptr, false);

    return callback_ptr->on_class_event(id, class_name, class_event, args);
}

std::shared_ptr<class_meta> class_meta_module_impl::find_meta(const std::string& class_name) const {
    return class_meta_manager_->find_meta(class_name);
}

const class_meta_manager::class_meta_list& class_meta_module_impl::get_meta_list() const {
    return class_meta_manager_->get_meta_list();
}

std::shared_ptr<class_meta> class_meta_module_impl::find_static_meta(const std::string& class_name) const {
    return class_meta_manager_->find_static_meta(class_name);
}

const class_meta_manager::class_meta_list& class_meta_module_impl::get_static_meta_list() const {
    return class_meta_manager_->get_static_meta_list();
}

} // namespace ark
