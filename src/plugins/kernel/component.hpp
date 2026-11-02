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

#include <type_traits>

#include "ark/define.hpp"
#include "ark/module.hpp"

namespace ark {

class component : public module
{
public:
    component() = delete;

    component(guid_t self, const std::string& name) {
        initialized_ = false;
        enabled_ = true;
        self_ = self;
        name_ = name;
    }

    template<typename value_type>
    std::shared_ptr<value_type> create_new_instance() {
        std::shared_ptr<component> component_ptr = create_new_instance();

        if (nullptr != component_ptr)
        {
            if constexpr (std::is_base_of_v<component, value_type>)
            {
                std::shared_ptr<value_type> value_ptr = std::dynamic_pointer_cast<value_type>(component_ptr);

                if (nullptr != value_ptr)
                {
                    return value_ptr;
                }
            }
        }

        return std::shared_ptr<value_type>();
    }

    virtual bool set_enable(const bool enable) {
        return enabled_;
    }

    virtual bool enable() {
        return enabled_;
    }

    virtual bool set_has_init(const bool enable) {
        initialized_ = enable;
        return initialized_;
    }

    virtual bool has_init() {
        return initialized_;
    }

    virtual guid_t self() {
        return null_guid;
    }

    virtual const std::string& get_component_name() const {
        return name_;
    }

    virtual int on_a_sync_event(const guid_t& self, const int event, std::string& arg) {
        return 0;
    }

protected:
    virtual std::shared_ptr<component> create_new_instance() {
        return nullptr;
    };

private:
    bool enabled_;
    bool initialized_;
    guid_t self_;
    std::string name_;
};

} // namespace ark