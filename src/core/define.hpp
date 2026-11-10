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

#include "ark/enum.hpp"
#include "ark/macros.hpp"
#include "ark/math/vector3d.hpp"
#include "ark/meta_define.hpp"
#include "ark/string.hpp"

namespace ark {

class data;
class data_list;

// default value of build-in type
const static bool null_boolean = false;
const static int null_int = 0U;
const static int64_t null_int64 = 0L;
const static float null_float = 0.0F;
const static double null_double = 0.0;
const static std::string null_str = "";
const static std::wstring null_widestr = L"";
const static guid_t null_guid = 0;
const static std::nullptr_t null_point = nullptr;
const static vector3d null_vector3d = vector3d(0.0F, 0.0F, 0.0F);

//const static std::string PROC_CONFIG_FILE_PATH = "conf/proc.xml";
//const static std::string BUS_RELATION_CONFIG_FILE_PATH = "conf/bus_relation.xml";
//const static std::string REG_CENTER_CONFIG_FILE_PATH = "conf/reg_center.xml";

/// data define(only support based integer type)
using id_type = uint32_t;

using mask_type = std::bitset<16>;

/// Table callback data
class table_event_data {
public:
    table_event_data() = default;

    uint8_t op_type{0};
    uint32_t row{0};
    uint32_t data_index{0};
    uint32_t table_index{0};
    std::string table_name{null_str};
};

using data_table_event_functor = std::function<int(const guid_t&, const table_event_data&, const data&, const data&)>;

using data_node_event_functor =
    std::function<int(const guid_t&, const std::string&, const uint32_t, const data&, const data&)>;

using container_event_functor =
    std::function<int(const guid_t&, const uint32_t, const container_op_type, const uint32_t, const uint32_t)>;

using class_event_functor =
    std::function<bool(const guid_t&, const std::string&, const entity_event, const data_list&)>;
using scene_event_functor = std::function<bool(const guid_t&, const int, const int)>;
using move_event_functor = std::function<bool(const guid_t&, const vector3d&, const vector3d&)>;
using event_process_functor = std::function<int(const guid_t&, const int, const data_list&)>;
using timer_functor = std::function<void(uint64_t, const guid_t&)>;
using scheduler_functor = std::function<void()>;

} // namespace ark
