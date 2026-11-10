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

#include "ark/platform.hpp"

namespace ark {

enum class data_type : uint32_t {
    dt_empty,    // unknown
    dt_boolean,  // bool
    dt_int32,    // int32
    dt_uint32,   // uint32
    dt_int64,    // int64
    dt_uint64,   // uint64
    dt_float,    // float
    dt_double,   // double
    dt_string,   // string
    dt_wstring,  // wstring
    dt_vector3d, // vector3d
    dt_array,    // array
    dt_table,    // DataTable
    dt_guid,     // guid
    dt_pointer,  // pointer(void*)
    dt_userdata, // user data(char*)
};

enum class connection_type : uint32_t {
    brt_unknown,     // unknown
    brt_direct,      // directly
    brt_wait_notify, // wait notify
    brt_record,      // do not connect, but need record info
};

enum class entity_event : uint32_t {
    entity_evt_none = 0,
    entity_evt_destroy,
    entity_evt_pre_destroy,
    entity_evt_pre_load_data,
    entity_evt_load_data,
    entity_evt_pre_effect_data,
    entity_evt_effect_data,
    entity_evt_post_effect_data,
    entity_evt_data_finished,
    entity_evt_all_finished, // Call it by yourself when create entity finished
};

enum class data_op_type : uint16_t {
    dot_unknown = 0,
    dot_inc, // +
    dot_dec, // -
    dot_mul, // *
    dot_div, // /
    dot_set, // =
    dot_and, // &
    dot_or,  // |
};

enum class table_op_type : uint16_t // table operate type
{
    table_add = 0,  // add whole row data
    table_delete,   // delete whole row data
    table_update,   // update row & col cell data
    table_coverage, // coverage whole row data
    table_swap,     // swap two whole row data
    table_clear,    // clear all rows
};

enum class data_mask : uint16_t {
    pf_sync_view = 0,  // send to others
    pf_sync_self = 1,  // send to self
    pf_real_time = 2,  // send real-time when changed
    pf_save = 3,       // save to database
    pf_sync_team = 4,  // sync to team member
    pf_sync_guild = 5, // sync to guild member
    pf_sync_map = 6,   // sync to all player in same map
    pf_log = 7,        // log when changed
};

enum class container_op_type : uint32_t {
    op_create = 0,  // new entity
    op_place = 1,   // place another entity
    op_remove = 2,  // remove en entity but do not destroy
    op_swap = 3,    // swap two entities
    op_destroy = 4, // destroy an entity
};

enum class http_method : uint8_t {
    http_method_head,
    http_method_get,
    http_method_post,
    http_method_put,
    http_method_delete,
    http_method_max
};

enum class compress_type : uint8_t {
    compress_type_none,
    compress_type_zlib,
    compress_type_zstd
};

} // namespace ark
