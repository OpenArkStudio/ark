/*
 * This source file is part of ARK
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

#include "ark/define.hpp"
#include "ark/platform.hpp"

namespace ark {

class client_connect {
public:
    using ret_type = std::pair<bool, conv_id_t>;

private:
    /// player open id session list
    std::unordered_map<std::string, conv_id_t> user_sesssion_list_;
    std::map<conv_id_t, std::string> sesssion_user_list_;

public:
    ///user session manager
    ret_type add_session(const std::string& open_id, const conv_id_t& sesssion_id) {
        ret_type ret(false, 0U);
        if (open_id.empty() || sesssion_id == 0) {
            return ret;
        }

        auto iter_open_id = user_sesssion_list_.find(open_id);
        if (iter_open_id != user_sesssion_list_.end()) {
            ret.second = iter_open_id->second;
            remove_session(iter_open_id->second);
        }

        user_sesssion_list_.insert(std::make_pair(open_id, sesssion_id));
        sesssion_user_list_.insert(std::make_pair(sesssion_id, open_id));

        ret.first = true;
        return ret;
    }

    bool remove_session(const conv_id_t& sesssion_id) {
        auto iter_session_id = sesssion_user_list_.find(sesssion_id);
        if (iter_session_id == sesssion_user_list_.end()) {
            return false;
        }

        auto& open_id = iter_session_id->second;
        auto iter_open_id = user_sesssion_list_.find(open_id);
        if (iter_open_id != user_sesssion_list_.end()) {
            user_sesssion_list_.erase(iter_open_id);
        }

        sesssion_user_list_.erase(iter_session_id);
        return true;
    }

    const std::string& get_opend_id(const conv_id_t& sesssion_id) {
        auto iter_session_id = sesssion_user_list_.find(sesssion_id);
        if (iter_session_id == sesssion_user_list_.end()) {
            return null_str;
        }

        return iter_session_id->second;
    }

    conv_id_t get_session_id(const std::string& open_id) {
        auto iter_open_id = user_sesssion_list_.find(open_id);
        if (iter_open_id == user_sesssion_list_.end()) {
            return null_int64;
        }

        return iter_open_id->second;
    }
};

} // namespace ark
