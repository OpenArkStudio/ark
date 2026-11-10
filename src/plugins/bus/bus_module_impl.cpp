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

#include "ark/bus/bus_module.hpp"
#include "ark/define.hpp"
#include "ark/enum.hpp"
#include "ark/module_base.hpp"
#include "ark/plugin_manager.hpp"
#include "bus_configuration.hpp"
#include "bus_plugin.hpp"

namespace ark {

class bus_module_impl final : public module_base<bus_module> {
public:
    bool init() override;

    const std::string get_app_whole_name(bus_id_t bus_id) override;
    const std::string& get_app_name(const ark::app_type& app_type) override;
    app_type get_app_type(const std::string& name) override;

    const proc_config& get_self_proc() override;

    app_type get_self_app_type() override;
    int get_self_bus_id() override;
    const std::string get_self_bus_name() override;

    int combine_bus_id(const app_type app_type, const uint8_t inst_id) override;

    //uint16_t CalcProcPort(bus_id_t bus_id) override;

    const reg_center& get_reg_center() override {
        return app_config_.reg_center;
    }

    bool get_target_bus_relations(std::vector<app_type>& target_list) override;

    bool get_other_proc(bus_id_t bus_id, proc_config& proc) const override;

    int get_other_proc(const app_type app_type, std::vector<const proc_config*>& proc_list) const override;

private:
    app_config app_config_;
};

bool bus_module_impl::init() {
    app_config_ = bus_config_detail::load(bus_plugin::get_plugin_conf(), get_self_bus_id());
    return true;
}

const std::string& bus_module_impl::get_app_name(const ark::app_type& app_type) {
    auto iter = app_config_.type2names.find(app_type);
    return ((iter != app_config_.type2names.end()) ? iter->second : null_str);
}

app_type bus_module_impl::get_app_type(const std::string& name) {
    auto iter = app_config_.name2types.find(name);
    return ((iter != app_config_.name2types.end()) ? iter->second : ark::app_type::ark_app_default);
}

//uint16_t AFCBusModule::CalcProcPort(bus_id_t bus_id)
//{
//    AFBusAddr bus_addr(bus_id);
//    const ARK_APP_TYPE& app_type = ARK_APP_TYPE(bus_addr.app_type);
//    if (app_type <= ARK_APP_TYPE::ARK_APP_DEFAULT || app_type >= ARK_APP_TYPE::ARK_APP_WORLD_MAX)
//    {
//        ARK_ASSERT_NO_EFFECT(0);
//        return 0;
//    }
//
//    // need check if valid
//    if (app_type > ARK_APP_TYPE::ARK_APP_DEFAULT && app_type < ARK_APP_TYPE::ARK_APP_CLUSTER_MAX)
//    {
//        return (10000 + bus_addr.app_type * 100 + bus_addr.inst_id);
//    }
//    else
//    {
//        return (20000 + bus_addr.app_type * 100 + bus_addr.inst_id);
//    }
//}

const proc_config& bus_module_impl::get_self_proc() {
    return app_config_.self_proc;
}

bus_id_t bus_module_impl::get_self_bus_id() {
    return get_plugin_manager()->get_bus_id();
}

const std::string bus_module_impl::get_self_bus_name() {
    bus_addr bus_id(get_self_bus_id());
    return bus_id.to_string();
}

app_type bus_module_impl::get_self_app_type() {
    bus_addr bus_id(get_self_bus_id());
    return app_type(bus_id.app_type);
}

const std::string bus_module_impl::get_app_whole_name(bus_id_t bus_id) {
    bus_addr addr(bus_id);
    std::string name = get_app_name(app_type(addr.app_type));
    return (name + "-" + addr.to_string());
}

int bus_module_impl::combine_bus_id(const app_type app_type, const uint8_t inst_id) {
    ARK_ASSERT_RET_VAL(app_type > ark::app_type::ark_app_default && app_type < ark::app_type::ark_app_max, 0);

    int self_bus = get_self_bus_id();
    bus_addr self_bus_addr(self_bus);
    bus_addr target_bus_addr(self_bus_addr.channel_id, self_bus_addr.zone_id, static_cast<uint8_t>(app_type), inst_id);
    return target_bus_addr.bus_id;
}

bool bus_module_impl::get_target_bus_relations(std::vector<app_type>& target_list) {
    bus_addr bus_addr(get_self_bus_id());
    auto iter = app_config_.connection_relations.find(static_cast<ark::app_type>(bus_addr.app_type));
    if (iter != app_config_.connection_relations.end()) {
        target_list = iter->second;
    }

    return true;
}

bool bus_module_impl::get_other_proc(bus_id_t bus_id, proc_config& proc) const {
    auto iter = app_config_.other_proc_list.find(bus_id);
    if (iter == app_config_.other_proc_list.end()) {
        return false;
    }

    proc = iter->second;
    return true;
}

int bus_module_impl::get_other_proc(const app_type app_type, std::vector<const proc_config*>& proc_list) const {
    for (auto& iter : app_config_.other_proc_list) {
        auto& proc = iter.second;
        bus_addr bus_id(proc.bus_id);
        if (static_cast<ark::app_type>(bus_id.app_type) != app_type) {
            continue;
        }

        proc_list.push_back(&proc);
    }

    return proc_list.size();
}

namespace detail {

std::unique_ptr<bus_module> make_bus_module() {
    return std::make_unique<bus_module_impl>();
}

} // namespace detail

} // namespace ark
