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
#include "ark/kernel/node_manager.hpp"
#include "ark/platform.hpp"

namespace ark {

class static_entity
{
public:
    virtual ~static_entity() = default;

    // query data
    virtual const std::string& get_class_name() const = 0;
    virtual id_type get_config_id() const = 0;
    virtual bool get_bool(const std::string& name) const = 0;
    virtual int32_t get_int32(const std::string& name) const = 0;
    virtual uint32_t get_u_int32(const std::string& name) const = 0;
    virtual int64_t get_int64(const std::string& name) const = 0;
    virtual uint64_t get_u_int64(const std::string& name) const = 0;
    virtual float get_float(const std::string& name) const = 0;
    virtual double get_double(const std::string& name) const = 0;
    virtual const std::string& get_string(const std::string& name) const = 0;
    virtual const std::wstring& get_w_string(const std::string& name) const = 0;

    virtual bool get_bool(const uint32_t index) const = 0;
    virtual int32_t get_int32(const uint32_t index) const = 0;
    virtual uint32_t get_u_int32(const uint32_t index) const = 0;
    virtual int64_t get_int64(const uint32_t index) const = 0;
    virtual uint64_t get_u_int64(const uint32_t index) const = 0;
    virtual float get_float(const uint32_t index) const = 0;
    virtual double get_double(const uint32_t index) const = 0;
    virtual const std::string& get_string(const uint32_t index) const = 0;
    virtual const std::wstring& get_w_string(const uint32_t index) const = 0;
};

} // namespace ark
