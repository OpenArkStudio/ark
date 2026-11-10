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
#include "ark/kernel/entity.hpp"
#include "ark/macros.hpp"
#include "ark/platform.hpp"

namespace ark {

class entity;

class container {
public:
    virtual ~container() = default;

    // get entity parent unique id
    virtual const std::string& get_name() const = 0;
    virtual const mask_type get_mask() const = 0;
    virtual const guid_t& get_parent_id() const = 0;
    virtual uint32_t get_index() const = 0;
    virtual uint32_t first() = 0;
    virtual uint32_t next() = 0;
    virtual std::shared_ptr<entity> find(uint32_t index) const = 0;
    virtual uint32_t find(const guid_t& id) const = 0;
    virtual bool exist(uint32_t index) const = 0;
    virtual bool exist(const guid_t& id) const = 0;
    virtual bool place(std::shared_ptr<entity> entity_ptr) = 0;
    virtual bool place(uint32_t index, std::shared_ptr<entity> entity_ptr) = 0;
    virtual bool swap(const uint32_t src_index, const uint32_t dest_index) = 0;
    virtual bool swap(const guid_t& src_entity, const guid_t& dest_entity) = 0;
    virtual bool swap(
        std::shared_ptr<container> src_container_ptr, const uint32_t src_index, const uint32_t dest_index) = 0;
    virtual bool swap(
        std::shared_ptr<container> src_container_ptr, const guid_t& src_entity, const guid_t& dest_entity) = 0;

    virtual bool remove(const uint32_t index) = 0;
    virtual bool remove(const guid_t& id) = 0;

    virtual bool destroy(const uint32_t index) = 0;
    virtual bool destroy(const guid_t& id) = 0;
};

} // namespace ark
