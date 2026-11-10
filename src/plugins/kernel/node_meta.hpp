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

#include "ark/define.hpp"
#include "ark/enum.hpp"
#include "ark/macros.hpp"

namespace ark {

class node_meta final {
public:
    node_meta() = delete;

    explicit node_meta(const std::string& name, const uint32_t index)
        : name_(name)
        , index_(index) {
    }

    virtual ~node_meta() = default;

    const std::string& get_name() {
        return name_;
    }

    void set_type(data_type value) {
        type_ = value;
    }

    data_type get_type() const {
        return type_;
    }

    void set_mask(mask_type mask) {
        mask_ = mask;
    }

    bool have_mask(data_mask mask) const {
        return mask_.test((size_t)mask);
    }

    uint32_t get_index() const {
        return index_;
    }

    mask_type get_mask() const {
        return mask_;
    }

private:
    // data name
    std::string name_{null_str};

    // data type
    data_type type_{data_type::dt_empty};

    //node index
    uint32_t index_{0u};

    // data mask
    mask_type mask_{0};
};

} // namespace ark
