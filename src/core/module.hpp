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

#include <string>
#include <vector>

#include "ark/macros.hpp"
#include "ark/platform.hpp"

namespace ark {

class plugin_manager;

class module
{
public:
    module() = default;
    virtual ~module() = default;

    // Required provider registration keys. Keep these stable while registered.
    // Startup and the first ingress-stop phase use a validated snapshot.
    virtual std::vector<std::string> get_dependencies() const {
        return {};
    }

    virtual bool init() {
        return true;
    }

    virtual bool post_init() {
        return true;
    }

    virtual bool check_config() {
        return true;
    }

    virtual bool pre_update() {
        return true;
    }

    virtual bool update() {
        return true;
    }

    // Close only new external business roots. Accepted tasks must keep access
    // to provider operations and completion delivery. Called once by the owner.
    virtual bool stop_accepting() {
        return true;
    }

    // Called after managed tasks and active callbacks drain, before any module
    // tears down. Return false while native work still needs owner-side updates.
    virtual bool ready_to_shut() const {
        return true;
    }

    virtual bool pre_shut() {
        return true;
    }

    virtual bool shut() {
        return true;
    }

    virtual plugin_manager* get_plugin_manager() const {
        return nullptr;
    }

    virtual void set_plugin_manager(plugin_manager* p) {
        // Do nothing in the module interface
    }

    virtual const std::string& get_name() const {
        static const std::string null_str = "";
        return null_str;
    }

    virtual void set_name(const std::string& value) {
        // Do nothing in the module interface
    }
};

} // namespace ark
