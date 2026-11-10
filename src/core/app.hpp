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

enum class app_type : uint8_t {
    ark_app_default, // none
    ark_app_master,  // master //cluster level start
    ark_app_router,  // router, world & cluster middle layer
    //ARK_APP_OSS,              // oss
    //ARK_APP_DIR,              // dir
    //ARK_APP_LOG,              // log
    //ARK_APP_CLUTER_RANK,      //cluster rank
    //ARK_APP_CLUSTER_MAIL,     //cluster mail
    //ARK_PROC_CLUSTER_PUB,     //cluster public
    ark_app_cluster_max = 29, // max of cluster
    ark_app_world,            // world // zone level start
    ark_app_game,             // game
    ark_app_login,            // login
    ark_app_proxy,            // proxy
    ark_app_db,               // db-proxy
    //ARK_APP_RANK,             // rank
    //ARK_APP_PUB,              // public
    //ARK_APP_CS_PROXY,         // cs_proxy, produce cross-server things
    ark_app_world_max = 199, // max of world
    ark_app_redis,           // Redis server // others start
    ark_app_mysql,           // MySQL server
    ark_app_max = 255,       // max of all processes
};

} // namespace ark
