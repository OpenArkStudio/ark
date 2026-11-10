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

#include <cstddef>

#include "ark/macros.hpp"

// open kcp console log
// #define KCP_OPEN_LOG

struct ikcpcb;
using ikcpcb = struct ikcpcb;

namespace ark {

class net;
class net_msg;
class net_event;

inline constexpr std::size_t default_initial_receive_buffer_size = 4 * 1024;

static constexpr int ark_socket_send_size = 32 * 1024; // 32K
static constexpr int ark_socket_recv_size = 32 * 1024; // 32K

static constexpr int ark_tcp_recv_buffer_size = 1024 * 1024;                         // 1M
static constexpr int ark_http_recv_buffer_size = 32 * 1024;                          // 32K
static constexpr std::chrono::seconds ark_connect_timeout = std::chrono::seconds(5); // 5s
static constexpr std::chrono::seconds ark_net_heart_time = std::chrono::seconds(30); // 30s
static constexpr std::chrono::seconds ark_reconnect_time = std::chrono::seconds(5);  // 5s
static constexpr int ark_process_net_msg_count_once = 100;
static constexpr int ark_msg_max_length = 4 * 1024;              // 4K
static constexpr int ark_conv_start_id = 1000;                   // start conv_id
static constexpr int ark_kcp_connection_timeout_time = 5 * 1000; // default is 5 seconds

static constexpr int ark_kcp_recv_buffer_size = 4 * 1024;        // 4K
static constexpr int ark_kcp_update_interval_time = 10;          // 10 ms
static constexpr int ark_kcp_resend_connect_msg_interval = 3000; // milliseconds
static constexpr int ark_kcp_connect_timeout_time = 60000;       // milliseconds

enum class net_event_type : uint8_t {
    none = 0,
    connect,
    disconnect,
    recv_data,
    connect_failed,
    lag_notify,
};

enum class kcp_protocal_type : uint8_t {
    none = 0,
    syn, // connect
    ack, // connected
    fin, // disconnect
    msg, // msg
};

enum class kcp_connect_status : uint8_t {
    status_none = 0,
    status_connecting,
    status_connected,
};

} // namespace ark