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
#include "ark/bus.hpp"
#include "ark/enum.hpp"
#include "ark/module.hpp"

namespace ark {

class bus_module : public module {
public:
    // get the app whole name, like app-bus_id
    virtual const std::string get_app_whole_name(bus_id_t bus_id) = 0;
    // just get the app name
    virtual const std::string& get_app_name(const ark::app_type& app_type) = 0;
    // get app type
    virtual app_type get_app_type(const std::string& name) = 0;

    //// get a process info which act a server
    virtual const proc_config& get_self_proc() = 0;

    virtual app_type get_self_app_type() = 0;
    virtual bus_id_t get_self_bus_id() = 0;
    virtual const std::string get_self_bus_name() = 0;

    virtual int combine_bus_id(const app_type app_type, const uint8_t inst_id) = 0;
    virtual const reg_center& get_reg_center() = 0;

    //virtual uint16_t CalcProcPort(bus_id_t bus_id) = 0;

    virtual bool get_target_bus_relations(std::vector<app_type>& target_list) = 0;

    virtual bool get_other_proc(bus_id_t bus_id, proc_config& proc) const = 0;

    virtual int get_other_proc(const app_type app_type, std::vector<const proc_config*>& proc_list) const = 0;
};

} // namespace ark
