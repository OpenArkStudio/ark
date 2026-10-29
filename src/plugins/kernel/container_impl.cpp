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

#include "ark/kernel/container_impl.hpp"

namespace ark {

container_impl::container_impl(std::shared_ptr<container_meta> container_meta, const guid_t& parent_id,
    std::shared_ptr<class_call_back_manager> call_back_mgr)
    : parent_(parent_id) {
    container_meta_ = container_meta;
    call_back_mgr_ = call_back_mgr;
}

// get parent unique id
const guid_t& container_impl::get_parent_id() const {
    return parent_;
}

uint32_t container_impl::get_index() const {
    ARK_ASSERT_RET_VAL(container_meta_ != nullptr, false);

    return container_meta_->get_index();
}

uint32_t container_impl::first() {
    iter_ = entity_data_list_.begin();
    if (entity_data_list_.end() == iter_)
    {
        return null_int;
    }

    return iter_->first;
}

uint32_t container_impl::next() {
    if (entity_data_list_.end() == iter_)
    {
        return null_int;
    }

    ++iter_;
    if (entity_data_list_.end() == iter_)
    {
        return null_int;
    }

    return iter_->first;
}

std::shared_ptr<entity> container_impl::find(uint32_t index) const {
    return entity_data_list_.find_value(index);
}

uint32_t container_impl::find(const guid_t& id) const {
    for (auto iter : entity_data_list_)
    {
        if (id == iter.second->get_id())
        {
            return iter.first;
        }
    }

    return null_int;
}

bool container_impl::exist(uint32_t index) const {
    return (entity_data_list_.find(index) != entity_data_list_.end());
}

bool container_impl::exist(const guid_t& id) const {
    return (find(id) > 0);
}

bool container_impl::place(std::shared_ptr<entity> entity_ptr) {
    // should not be in container
    ARK_ASSERT_RET_VAL(entity_ptr != nullptr, false);

    ARK_ASSERT_RET_VAL(find(entity_ptr->get_id()) == 0, false);

    return place(select_index(), entity_ptr);
}

bool container_impl::place(uint32_t index, std::shared_ptr<entity> entity_ptr) {
    // class should be same
    if (!entity_ptr || !container_meta_ || entity_ptr->get_class_name() != container_meta_->get_class_name())
    {
        return false;
    }

    auto iter = entity_data_list_.find(index);
    if (iter != entity_data_list_.end())
    {
        // has exist other entity
        return false;
    }

    if (!place_entity(index, entity_ptr))
    {
        return false;
    }

    on_container_place(index, entity_ptr);
    return true;
}

bool container_impl::swap(const uint32_t src_index, const uint32_t dest_index) {
    ARK_ASSERT_RET_VAL(src_index > 0u && dest_index > 0u && src_index != dest_index, false);

    auto iter_src = entity_data_list_.find(src_index);
    if (iter_src == entity_data_list_.end())
    {
        // fail if source do not exist
        return false;
    }

    auto src_entity_ptr = iter_src->second;
    ARK_ASSERT_RET_VAL(src_entity_ptr != nullptr, false);

    auto iter_dest = entity_data_list_.find(dest_index);
    if (iter_dest == entity_data_list_.end())
    {
        entity_data_list_.erase(src_index);
        if (!place_entity(dest_index, src_entity_ptr))
        {
            return false;
        }

        on_container_swap(src_index, dest_index);
    }
    else
    {
        // exchange
        auto dest_entity_ptr = iter_dest->second;
        iter_src->second = dest_entity_ptr;
        iter_dest->second = src_entity_ptr;

        on_container_swap(src_index, dest_index);
    }

    return true;
}

bool container_impl::swap(const guid_t& src_entity, const guid_t& dest_entity) {
    auto src_index = find(src_entity);
    auto dest_index = find(dest_entity);

    return swap(src_index, dest_index);
}

bool container_impl::swap(
    std::shared_ptr<container> src_container_ptr, const uint32_t src_index, const uint32_t dest_index) {
    if (src_container_ptr == nullptr || src_index == 0 || dest_index == 0)
    {
        return false;
    }

    auto src_entity_ptr = src_container_ptr->find(src_index);
    if (src_entity_ptr == nullptr)
    {
        return false;
    }

    // class should be same
    if (!container_meta_ || src_entity_ptr->get_class_name() != container_meta_->get_class_name())
    {
        return false;
    }

    if (!src_container_ptr->remove(src_index))
    {
        return false;
    }

    auto iter_dest = entity_data_list_.find(dest_index);
    if (iter_dest == entity_data_list_.end())
    {
        if (!place_entity(dest_index, src_entity_ptr))
        {
            return false;
        }

        on_container_place(dest_index, src_entity_ptr);
    }
    else
    {
        auto dest_src_entity_ptr = iter_dest->second;
        if (!remove(dest_index))
        {
            return false;
        }

        if (!src_container_ptr->place(src_index, dest_src_entity_ptr) || !place_entity(dest_index, src_entity_ptr))
        {
            return false;
        }

        on_container_place(dest_index, src_entity_ptr);
    }

    return true;
}

bool container_impl::swap(
    std::shared_ptr<container> src_container_ptr, const guid_t& src_entity, const guid_t& dest_entity) {
    if (src_container_ptr == nullptr)
    {
        return false;
    }

    auto src_index = src_container_ptr->find(src_entity);
    auto dest_index = find(dest_entity);

    return swap(src_container_ptr, src_index, dest_index);
}

bool container_impl::remove(const uint32_t index) {
    auto entity_ptr = entity_data_list_.find_value(index);
    if (nullptr == entity_ptr)
    {
        return false;
    }

    entity_data_list_.erase(index);
    on_container_remove(index, entity_ptr);

    return true;
}

bool container_impl::remove(const guid_t& id) {
    auto index = find(id);
    return remove(index);
}

bool container_impl::destroy(const uint32_t index) {
    auto entity_ptr = entity_data_list_.find_value(index);
    if (nullptr == entity_ptr)
    {
        return false;
    }

    on_container_destroy(index, entity_ptr);
    entity_data_list_.erase(index);

    return true;
}

bool container_impl::destroy(const guid_t& id) {
    auto index = find(id);
    return destroy(index);
}

uint32_t container_impl::select_index() const {
    if (entity_data_list_.size() == current_index_)
    {
        return current_index_ + 1;
    }
    else
    {
        // index starts from 1
        uint32_t index = 1u;
        for (auto iter : entity_data_list_)
        {
            if (iter.first != index)
            {
                break;
            }

            index += 1;
        }

        return index;
    }
}

bool container_impl::place_entity(const uint32_t index, std::shared_ptr<entity> entity_ptr) {
    if (!entity_data_list_.insert(index, entity_ptr).second)
    {
        return false;
    }

    if (current_index_ < index)
    {
        current_index_ = index;
    }

    return true;
}

void container_impl::on_container_place(const uint32_t index, std::shared_ptr<entity> entity_ptr) {
    ARK_ASSERT_RET_NONE(entity_ptr != nullptr);
    entity_ptr->set_parent_container(shared_from_this());

    ARK_ASSERT_RET_NONE(call_back_mgr_ != nullptr && container_meta_ != nullptr);

    call_back_mgr_->on_container_call_back(
        parent_, container_meta_->get_index(), container_meta_->get_mask(), container_op_type::op_place, index, 0u);
}

void container_impl::on_container_swap(const uint32_t index, const uint32_t swap_index) {
    ARK_ASSERT_RET_NONE(call_back_mgr_ != nullptr && container_meta_ != nullptr);
    call_back_mgr_->on_container_call_back(parent_, container_meta_->get_index(), container_meta_->get_mask(),
        container_op_type::op_swap, index, swap_index);
}

void container_impl::on_container_remove(const uint32_t index, std::shared_ptr<entity> entity_ptr) {
    entity_ptr->set_parent_container(nullptr);

    ARK_ASSERT_RET_NONE(call_back_mgr_ != nullptr && container_meta_ != nullptr);
    call_back_mgr_->on_container_call_back(
        parent_, container_meta_->get_index(), container_meta_->get_mask(), container_op_type::op_remove, index, 0u);
}

void container_impl::on_container_destroy(const uint32_t index, std::shared_ptr<entity> entity_ptr) {
    entity_ptr->set_parent_container(nullptr);

    ARK_ASSERT_RET_NONE(call_back_mgr_ != nullptr && container_meta_ != nullptr);
    call_back_mgr_->on_container_call_back(parent_, container_meta_->get_index(), container_meta_->get_mask(),
        container_op_type::op_destroy, index, 0u, entity_ptr);
}

bool container_impl::init_entity_list(const uint32_t index, std::shared_ptr<entity> entity_ptr) {
    return entity_data_list_.insert(index, entity_ptr).second;
}

const std::string& container_impl::get_name() const {
    ARK_ASSERT_RET_VAL(container_meta_ != nullptr, null_str);

    return container_meta_->get_name();
}

const mask_type container_impl::get_mask() const {
    ARK_ASSERT_RET_VAL(container_meta_ != nullptr, 0);

    return container_meta_->get_mask();
}

} // namespace ark
