#pragma once

#include "ark/define.hpp"
#include "ark/platform.hpp"
#include "simulate_msg_define.hpp"

namespace ark {

using simulate_net_id = int64_t;

using simulate_trigger_cb_function =
    std::function<void(const std::string& scene_id, const int32_t index, const simulate::trigger_pair& pair)>;

class simulate_scene {
public:
    simulate_scene() {
    }

    simulate_scene(const guid_t& scene_obj_id)
        : scene_obj_id_(scene_obj_id) {
    }

    const guid_t& get_scene_obj_id() {
        return scene_obj_id_;
    }

    bool exist(const simulate_net_id obj_id) {
        auto iter = net_id_actor_list_.find(obj_id);
        return iter != net_id_actor_list_.end();
    }

    void insert_obj_actor(const simulate_net_id obj_id, const guid_t& actor_obj_id) {
        net_id_actor_list_.insert(std::make_pair(obj_id, actor_obj_id));
        actor_net_id_list_.insert(std::make_pair(actor_obj_id, obj_id));
    }

    const guid_t& find_actor(const simulate_net_id obj_id) {
        auto iter = net_id_actor_list_.find(obj_id);
        if (iter == net_id_actor_list_.end()) {
            return null_guid;
        }

        return iter->second;
    }

    simulate_net_id find_obj(const guid_t& actor_obj_id) {
        auto iter = actor_net_id_list_.find(actor_obj_id);
        if (iter == actor_net_id_list_.end()) {
            return 0;
        }

        return iter->second;
    }

    void remove_obj(const simulate_net_id obj_id) {
        auto iter = net_id_actor_list_.find(obj_id);
        if (iter == net_id_actor_list_.end()) {
            return;
        }

        auto actor = iter->second;
        net_id_actor_list_.erase(iter);
        actor_net_id_list_.erase(actor);
    }

    void remove_actor(const guid_t& actor_obj_id) {
        auto iter = actor_net_id_list_.find(actor_obj_id);
        if (iter == actor_net_id_list_.end()) {
            return;
        }

        auto obj_id = iter->second;
        net_id_actor_list_.erase(obj_id);
        actor_net_id_list_.erase(iter);
    }

private:
    using net_actor_map = std::map<simulate_net_id, guid_t>;
    using actor_net_map = std::map<guid_t, simulate_net_id>;

    guid_t scene_obj_id_{0U};
    net_actor_map net_id_actor_list_;
    actor_net_map actor_net_id_list_;
};

using index_scene_map = std::map<int32_t, simulate_scene>;
using scene_index_scene_map = std::unordered_map<std::string, index_scene_map>;

using scene_index_pair = std::pair<std::string, int32_t>;
using scene_index_list = std::map<guid_t, scene_index_pair>;

} // namespace ark
