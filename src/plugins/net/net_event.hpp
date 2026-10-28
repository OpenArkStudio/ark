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

#include <new>

#include "ark/net/net_define.hpp"
#include "ark/platform.hpp"

namespace ark {

class net_event final
{
    net_event_type type_{net_event_type::none};
    conv_id_t id_{0};
    std::string ip_{};
    bus_id_t bus_id_{0};

public:
    static net_event* alloc_event() {
        return new (std::nothrow) net_event();
    }

    static void release(net_event*& event) {
        delete event;
        event = nullptr;
    }

    net_event_type get_type() const {
        return type_;
    }

    conv_id_t get_id() const {
        return id_;
    }

    const std::string& get_ip() const {
        return ip_;
    }

    bus_id_t get_bus_id() const {
        return bus_id_;
    }

    void set_type(net_event_type value) {
        type_ = value;
    }

    void set_id(conv_id_t value) {
        id_ = value;
    }

    void set_ip(const std::string& value) {
        ip_ = value;
    }

    void set_bus_id(bus_id_t value) {
        bus_id_ = value;
    }
};

} // namespace ark
