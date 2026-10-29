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
#include "ark/kernel/class_meta.hpp"
#include "ark/kernel/container.hpp"

namespace ark {

namespace entity_sync {
class snapshot_access;
}

class container_impl final
    : public container
    , public std::enable_shared_from_this<container_impl>
{
public:
    using entity_data_list = shared_object_map<uint32_t, entity>;

private:
    friend class kernel_module_impl;
    friend class entity_sync::snapshot_access;

    // contain entity class meta
    std::shared_ptr<container_meta> container_meta_{nullptr};

    // index start
    uint32_t current_index_{0};

    // parent id
    guid_t parent_{null_guid};

    // entity data
    entity_data_list::iterator iter_;
    entity_data_list entity_data_list_;

    // call back
    std::shared_ptr<class_call_back_manager> call_back_mgr_{nullptr};

public:
    container_impl() = delete;

    explicit container_impl(std::shared_ptr<container_meta> container_meta, const guid_t& parent_id,
        std::shared_ptr<class_call_back_manager> class_meta);

    const std::string& get_name() const override;

    const mask_type get_mask() const override;

    // get parent unique id
    const guid_t& get_parent_id() const override;

    uint32_t get_index() const override;

    uint32_t first() override;
    uint32_t next() override;

    std::shared_ptr<entity> find(uint32_t index) const override;
    uint32_t find(const guid_t& id) const override;

    bool exist(uint32_t index) const override;
    bool exist(const guid_t& id) const override;

    bool place(std::shared_ptr<entity> entity_ptr) override;
    bool place(uint32_t index, std::shared_ptr<entity> entity_ptr) override;

    bool swap(const uint32_t src_index, const uint32_t dest_index) override;
    bool swap(const guid_t& src_entity, const guid_t& dest_entity) override;

    bool swap(
        std::shared_ptr<container> src_container_ptr, const uint32_t src_index, const uint32_t dest_index) override;
    bool swap(
        std::shared_ptr<container> src_container_ptr, const guid_t& src_entity, const guid_t& dest_entity) override;

    bool remove(const uint32_t index) override;
    bool remove(const guid_t& id) override;

    bool destroy(const uint32_t index) override;
    bool destroy(const guid_t& id) override;

private:
    uint32_t select_index() const;

    bool place_entity(const uint32_t index, std::shared_ptr<entity> entity_ptr);

    void on_container_place(const uint32_t index, std::shared_ptr<entity> entity_ptr);
    void on_container_swap(const uint32_t index, const uint32_t swap_index);
    void on_container_remove(const uint32_t index, std::shared_ptr<entity> entity_ptr);
    void on_container_destroy(const uint32_t index, std::shared_ptr<entity> entity_ptr);

    bool init_entity_list(const uint32_t index, std::shared_ptr<entity> entity_ptr);
};

} // namespace ark
