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
#include "ark/define.hpp"
#include "ark/enum.hpp"
#include "ark/kernel/class_meta.hpp"
#include "ark/macros.hpp"

namespace ark {

class class_meta;

class table_meta final {
public:
    using col_meta_list = shared_object_map<uint32_t, node_meta>;

private:
    // table name
    std::string name_{null_str};

    // index
    uint32_t index_{0u};

    // data type name
    std::string type_name_{null_str};

    std::shared_ptr<class_meta> class_meta_{nullptr};

    // mask type
    mask_type mask_;

public:
    table_meta() = delete;

    explicit table_meta(const std::string& name, const uint32_t index);

    virtual ~table_meta() = default;

    const std::string& get_name() const;

    size_t get_col_count() const;

    void set_class_meta(std::shared_ptr<class_meta> class_meta_ptr);

    std::shared_ptr<node_meta> find_meta(const uint32_t index) const;

    std::shared_ptr<class_meta> get_class_meta() const;

    data_type get_col_type(const uint32_t index) const;

    const mask_type get_mask() const;
    bool have_mask(const data_mask feature) const;
    void set_mask(const mask_type& feature);

    bool is_public() const;
    bool is_private() const;
    bool is_real_time() const;
    bool is_save() const;

    void set_type_name(const std::string& value);
    const std::string& get_type_name() const;

    uint32_t get_index() const;
    uint32_t get_index(const std::string& name) const;
};

} // namespace ark
