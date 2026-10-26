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

#pragma once

#include <type_traits>

#include "ark/array_pod.hpp"
#include "ark/macros.hpp"
#include "ark/platform.hpp"
#include "ark/string_pod.hpp"
//#include "base/guid_t128.hpp"
#include "ark/container/lru_map.hpp"

namespace ark {

template<typename element_type>
class is_buildin_type
{
public:
    enum
    {
        yes = std::is_same_v<element_type, uint16_t> || std::is_same_v<element_type, uint32_t> ||
              std::is_same_v<element_type, uint64_t> || std::is_same_v<element_type, int16_t> ||
              std::is_same_v<element_type, int32_t> || std::is_same_v<element_type, int64_t>,
        no = !yes
    };
};

template<typename element_type>
bool check_buildin_type(const element_type& t) {
    return is_buildin_type<element_type>::yes ? true : false;
}

//////////////////////////////////////////////////////////////////////////

template<typename element_type, typename node_type>
class array_map
{
};

// partial template specialization for KEY=std::string
template<typename node_type>
class array_map<std::string, node_type>
{
public:
    array_map() noexcept {
    }

    ~array_map() {
        release_all();
        indices_.clear();
    }

    void clear() {
        release_all();
        indices_.clear();
    }

    bool add_element(const std::string& name, node_type* data) {
        nodes_.push_back(data);
        try
        {
            if (indices_.add(name.c_str(), nodes_.size() - 1))
            {
                return true;
            }
        }
        catch (...)
        {
            nodes_.pop_back();
            throw;
        }
        nodes_.pop_back();
        return false;
    }

    node_type* get_element(const std::string& name) {
        size_t index;

        if (!indices_.get_data(name.c_str(), index))
        {
            return nullptr;
        }

        return nodes_[index];
    }

    node_type* operator[](size_t index) {
        ARK_ASSERT_RET_VAL(index < get_count(), nullptr);

        return nodes_[index];
    }

    // TODO:swap array data between index and last,
    // then index will be new last, so we can delete last
    // bool RemoveElement(const std::string& name)
    //{
    //    size_t index;
    //    if (!mxIndices.GetData(name.c_str(), index))
    //    {
    //        return false;
    //    }
    //
    //    //remove from name container
    //    mxIndices.Remove(name.c_str());

    //    if (mxNodes[index] != nullptr)
    //    {
    //        //delete mxNodes[index];
    //        mxNodes[index] = nullptr;
    //    }
    //    mxNodes.remove(index);
    //    return true;
    //}

    bool exist_element(const std::string& name) const {
        return indices_.exists(name.c_str());
    }

    bool exist_element(const std::string& name, size_t& index) const {
        return indices_.get_data(name.c_str(), index);
    }

    size_t get_count() const {
        return nodes_.size();
    }

protected:
    void release_all() {
        for (size_t i = 0; i < get_count(); ++i)
        {
            nodes_[i] = nullptr;
        }

        nodes_.clear();
        indices_.clear();
    }

private:
    pod_array<node_type*, 1, pod_array_allocator> nodes_;
    pod_string<char, size_t, string_traits<char>> indices_;
};

} // namespace ark
