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

#include "ark/app.hpp"
#include "ark/macros.hpp"
#include "ark/platform.hpp"
#include "ark/socket_func.hpp"
#include "ark/string_utils.hpp"

namespace ark {

// bus address, like IP address, 8.8.8.8
class bus_addr {
public:
    bus_id_t bus_id{0};

    uint8_t channel_id; // channel id
    uint8_t zone_id;    // zone id
    uint8_t app_type;   // application id
    uint8_t inst_id;    // instance id

public:
    bus_addr() = default;

    explicit bus_addr(const int id)
        : bus_id(id) {
        init_from_bus_id();
    }

    explicit bus_addr(const uint8_t c_id, const uint8_t z_id, const uint8_t p_id, const uint8_t i_id)
        : channel_id(c_id)
        , zone_id(z_id)
        , app_type(p_id)
        , inst_id(i_id) {
        init_to_bus_id();
    }

    inline bus_addr& operator=(const int rhs) {
        this->bus_id = rhs;
        init_from_bus_id();
        return *this;
    }

    inline std::string to_string() {
        std::string bus_name = ARK_FORMAT("{}.{}.{}.{}", channel_id, zone_id, app_type, inst_id);
        return bus_name;
    }

    bool from_string(const std::string& bus_name) {
        ARK_ASSERT_RET_VAL(!bus_name.empty(), false);

        std::vector<std::string> slices;
        string_utils::split(slices, bus_name, '.');
        ARK_ASSERT_RET_VAL(slices.size() >= 4, false);

        channel_id = ARK_LEXICAL_CAST<uint8_t>(slices[0]);
        zone_id = ARK_LEXICAL_CAST<uint8_t>(slices[1]);
        app_type = ARK_LEXICAL_CAST<uint8_t>(slices[2]);
        inst_id = ARK_LEXICAL_CAST<uint8_t>(slices[3]);

        init_to_bus_id();

        return true;
    }

private:
    void init_to_bus_id() {
        bus_id =
            (bus_id_t(channel_id) << 24) + (bus_id_t(zone_id) << 16) + (bus_id_t(app_type) << 8) + bus_id_t(inst_id);
    }

    void init_from_bus_id() {
        inst_id = uint8_t(bus_id & 0x000000FF);
        app_type = uint8_t((bus_id & 0x0000FF00) >> 8);
        zone_id = uint8_t((bus_id & 0x00FF0000) >> 16);
        channel_id = uint8_t((bus_id & 0xFF000000) >> 24);
    }
};

// bus relation, app connect other app with direct way or waiting sync message
class bus_relation {
public:
    uint8_t app_type{std::underlying_type<ark::app_type>::type(ark::app_type::ark_app_default)};
    uint8_t target_app_type{std::underlying_type<ark::app_type>::type(ark::app_type::ark_app_default)};
    bool connection_type{false};
};

class proc_config {
public:
    bus_id_t bus_id{0};
    uint32_t max_connection{0};
    uint8_t thread_num{0};
    endpoint intranet_ep;
    endpoint server_ep;
    // to add other fields
};

class reg_center {
public:
    std::string ip{};
    uint16_t port{8500};
    std::string service_name{};
    std::string check_interval{"10s"};
    std::string check_timeout{"3s"};
};

class app_config {
public:
    ark::reg_center reg_center; // register center

    std::map<std::string, app_type> name2types; // app_name -> app_type
    std::map<app_type, std::string> type2names; // app_type -> app_name

    std::map<app_type, std::vector<app_type>> connection_relations; // app_type -> target_types

    proc_config self_proc; // self process info

    std::map<bus_id_t, proc_config> other_proc_list;
};

} // namespace ark
