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

#include <cstdint>

namespace ark {

enum class error_code : std::uint32_t {
    success = 0,

    kcp_server_disconnect = 1000,   // kcp server disconnect when dtor
    kcp_time_out_disconnect = 1001, // kcp timeout disconnect
    kcp_force_disconnect = 1002,    // force disconnect
    kcp_connect_timeout = 1003,     // connect timeout
    peer_disconnect = 1004,         // peer(client/server) disconnect

    // will add error code
    logic_error_code_start = 10000,
};
} // namespace ark