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

#include "ark/container/list.hpp"
#include "ark/container/map.hpp"
#include "ark/kernel/class_meta.hpp"

namespace ark {

class class_meta_manager final {
public:
    using class_meta_list = shared_object_hashmap<std::string, class_meta>;

private:
    // class meta list
    class_meta_list class_meta_list_;
    class_meta_list static_class_meta_list_;

public:
    class_meta_manager() = default;
    virtual ~class_meta_manager() = default;

    // create meta
    std::shared_ptr<class_meta> create_meta(const std::string& name) {
        return create_meta(class_meta_list_, name);
    }

    // create meta
    std::shared_ptr<class_meta> create_static_meta(const std::string& name) {
        return create_meta(static_class_meta_list_, name);
    }

    // find meta do not create
    std::shared_ptr<class_meta> find_meta(const std::string& name) const {
        return class_meta_list_.find_value(name);
    }

    // find meta do not create
    std::shared_ptr<class_meta> find_static_meta(const std::string& name) const {
        return static_class_meta_list_.find_value(name);
    }

    const class_meta_list& get_meta_list() const {
        return class_meta_list_;
    }

    const class_meta_list& get_static_meta_list() const {
        return static_class_meta_list_;
    }

    bool after_loaded() {
        for (auto iter_class_meta : class_meta_list_) {
            auto meta = iter_class_meta.second;
            ARK_ASSERT_RET_VAL(meta != nullptr, false);

            // init table col meta
            auto& table_meta_list = meta->get_table_meta_list();
            for (auto iter : table_meta_list) {
                auto table_meta_ptr = iter.second;
                ARK_ASSERT_RET_VAL(table_meta_ptr != nullptr, false);

                auto& type_name = table_meta_ptr->get_type_name();
                auto class_meta_ptr = find_meta(type_name);
                ARK_ASSERT_RET_VAL(class_meta_ptr != nullptr, false);

                table_meta_ptr->set_class_meta(class_meta_ptr);
            }
        }

        return true;
    }

private:
    std::shared_ptr<class_meta> create_meta(class_meta_list& class_meta_list, const std::string& name) {
        // return if found
        auto iter = class_meta_list.find(name);
        if (iter != class_meta_list.end()) {
            return iter->second;
        }

        // create new class meta
        std::shared_ptr<class_meta> meta_ptr = std::make_shared<class_meta>(name);
        class_meta_list.insert(name, meta_ptr);

        return meta_ptr;
    }
};

} // namespace ark
