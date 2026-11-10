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

#include "ark/kernel/data_impl.hpp"
#include "ark/kernel/row.hpp"
#include "ark/kernel/table_impl.hpp"

namespace ark {

class table_manager final {
public:
    using table_list = object_map<uint32_t, table>;

private:
    // class meta
    std::shared_ptr<class_meta> class_meta_{nullptr};

    // table list
    table_list table_list_;

public:
    table_manager() = delete;

    explicit table_manager(std::shared_ptr<class_meta> class_meta_ptr) {
        class_meta_ = class_meta_ptr;
    }

    // table operation
    table* create_table(const uint32_t index, table_impl::table_callback_functor&& func) {
        ARK_ASSERT_RET_VAL(class_meta_ != nullptr, nullptr);

        table* table_ptr = table_list_.find_value(index);
        ARK_ASSERT_RET_VAL(table_ptr == nullptr, nullptr);

        auto table_meta_ptr = class_meta_->find_table_meta(index);
        ARK_ASSERT_RET_VAL(table_meta_ptr != nullptr, nullptr);

        table_ptr = ARK_NEW table_impl(table_meta_ptr, std::move(func));
        ARK_ASSERT_RET_VAL(table_ptr != nullptr, nullptr);

        if (!table_list_.insert(index, table_ptr).second) {
            ARK_DELETE(table_ptr);
            return nullptr;
        }

        return table_ptr;
    }

    table* find_table(const uint32_t index) {
        return table_list_.find_value(index);
    }

    const table_list& get_table_list() const {
        return table_list_;
    }
};

} // namespace ark
