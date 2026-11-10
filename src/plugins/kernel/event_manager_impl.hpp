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

#include "ark/container/list.hpp"
#include "ark/container/map.hpp"
#include "ark/define.hpp"
#include "ark/kernel/data_list_impl.hpp"
#include "ark/kernel/event_manager.hpp"

namespace ark {

class event_manager_impl final : public event_manager {
public:
    event_manager_impl() = delete;

    explicit event_manager_impl(guid_t self)
        : self_(self) {
    }

    void update() override {
        for (auto iter : need_remove_events_) {
            event_callbacks_.erase(iter);
        }

        need_remove_events_.clear();
    }

    bool add_event_call_back(const int event_id, event_process_functor&& cb) override {
        auto event_info_ptr = event_callbacks_.find_value(event_id);
        if (event_info_ptr == nullptr) {
            auto event_info = std::make_shared<list<event_process_functor>>();
            event_info->emplace_back(cb);
            return event_callbacks_.insert(event_id, event_info).second;
        } else {
            event_info_ptr->emplace_back(cb);
            return true;
        }
    }

    bool remove_event_call_back(const int event_id) override {
        need_remove_events_.emplace_back(event_id);
        return true;
    }

    bool do_event(const int event_id, const data_list& args) override {
        auto event_info = event_callbacks_.find_value(event_id);
        if (event_info == nullptr) {
            return false;
        }

        for (auto iter : *event_info) {
            iter(self_, event_id, args);
        }

        return true;
    }

    bool clear() override {
        need_remove_events_.clear();
        event_callbacks_.clear();

        return true;
    }

protected:
    bool has_event_call_back(const int event_id) override {
        auto event_info = event_callbacks_.find_value(event_id);
        return (event_info != nullptr);
    }

private:
    guid_t self_;

    list<int> need_remove_events_;
    shared_object_map<int, list<event_process_functor>> event_callbacks_;
};

} // namespace ark
