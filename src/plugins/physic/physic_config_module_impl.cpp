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

#include <algorithm>
#include <memory>
#include <utility>

#include "ark/physic/physic_config_module_impl.hpp"
#include "ark/plugin_manager.hpp"
#include "physic_resources.hpp"

#ifdef ARK_PLATFORM_WIN
#include <direct.h>
#include <io.h>
#elif defined(ARK_PLATFORM_LINUX) || defined(ARK_PLATFORM_DARWIN)
#include <sys/stat.h>
#include <unistd.h>

#include <dirent.h>
#endif

namespace ark {

bool physic_config_module_impl::init() {
    if (!load_config()) {
        return false;
    }

    if (!load_scene()) {
        return false;
    }

    return true;
}

bool physic_config_module_impl::shut() {
    return true;
}

bool physic_config_module_impl::get_physic_scene(const std::string& scene_id, physic_scene& scene) const {
    auto iter = scene_list_.find(scene_id);
    if (iter == scene_list_.end()) {
        ARK_LOG_ERROR("do not have this scene scene_id={}", scene_id);
        return false;
    }

    scene = iter->second;
    return true;
}

const physic_scene_list& physic_config_module_impl::get_physic_scene_list() const {
    return scene_list_;
}

const physic_config& physic_config_module_impl::get_physic_config() const {
    return config_;
}

bool physic_config_module_impl::load_scene() {
    std::string file_path = get_plugin_manager()->GetResPath() + physic_scene_path;

    std::multimap<std::string, std::string> sub_path_list;
    if (!get_scene_file_list("", file_path, sub_path_list)) {
        return false;
    }

    ///read scene
    for (auto& iter : sub_path_list) {
        auto scene_id = iter.first;
        auto file_path = iter.second;

        ///find scene
        auto iter_scene = scene_list_.find(scene_id);
        if (iter_scene == scene_list_.end()) {
            physic_scene scene;
            scene.scene_id = scene_id;
            iter_scene = scene_list_.insert(std::make_pair(scene_id, scene)).first;
        }

        if (iter_scene == scene_list_.end()) {
            ARK_LOG_ERROR("do not have this scene scene_id={}", scene_id);
            return false;
        }
        auto& scene = iter_scene->second;

        ///load file
        if (file_path.ends_with(physic_mesh_file_end)) {
            if (!load_terrain(scene, file_path)) {
                return false;
            }
        } else if (file_path.ends_with(physic_collider_file_end)) {
            if (!load_collider(scene, file_path)) {
                return false;
            }
        }
    }

    return true;
}

bool physic_config_module_impl::load_terrain(physic_scene& scene, const std::string& file_path) {
    scene.mesh_file.path = file_path;
    scene.mesh_file.cooked_path = gen_cooked_file_path(file_path);

    return true;
}

bool physic_config_module_impl::load_collider(physic_scene& scene, const std::string& file_path) {
    const auto resources = physic_resources::read_colliders(file_path);
    const auto native_vector = [](const std::array<float, 3>& value) {
        return physx::PxVec3(value[0], value[1], value[2]);
    };
    for (const auto& resource : resources) {
        std::unique_ptr<ark::collider> object;
        switch (resource.type) {
        case physic_resources::collider_type::box: {
            auto box = std::make_unique<box_collider>();
            box->position = native_vector(resource.position);
            box->center = native_vector(resource.center);
            box->size = native_vector(resource.size);
            object = std::move(box);
            break;
        }
        case physic_resources::collider_type::capsule: {
            auto capsule = std::make_unique<capsule_collider>();
            capsule->position = native_vector(resource.position);
            capsule->center = native_vector(resource.center);
            capsule->radius = resource.radius;
            capsule->height = resource.height;
            object = std::move(capsule);
            break;
        }
        case physic_resources::collider_type::sphere: {
            auto sphere = std::make_unique<sphere_collider>();
            sphere->position = native_vector(resource.position);
            sphere->center = native_vector(resource.center);
            sphere->radius = resource.radius;
            object = std::move(sphere);
            break;
        }
        case physic_resources::collider_type::mesh: {
            auto mesh = std::make_unique<mesh_collider>();
            mesh->position = native_vector(resource.position);
            mesh->convex = resource.convex;
            mesh->vertex_count = static_cast<std::uint32_t>(resource.vertices.size());
            mesh->vertices = new physx::PxVec3[mesh->vertex_count];
            for (std::size_t i = 0; i < resource.vertices.size(); ++i) {
                mesh->vertices[i] = native_vector(resource.vertices[i]);
            }
            mesh->triangle_count = static_cast<std::uint32_t>(resource.triangles.size());
            mesh->triangles = new physx::PxU32[mesh->triangle_count];
            std::copy(resource.triangles.begin(), resource.triangles.end(), mesh->triangles);
            mesh->mesh_file.cooked_path = gen_cooked_file_path(file_path);
            object = std::move(mesh);
            break;
        }
        case physic_resources::collider_type::terrain:
            scene.position = native_vector(resource.position);
            continue;
        }
        object->trigger = resource.trigger;
        scene.colliders.push_back(object.get());
        object.release();
    }
    return true;
}

bool physic_config_module_impl::get_scene_file_list(
    const std::string& father_path, const std::string& path, std::multimap<std::string, std::string>& sub_path_list) {
#ifdef ARK_PLATFORM_WIN
    std::string search_path = path + "/*.*";
    intptr_t handle;
    _finddata_t find_data;
    handle = _findfirst(search_path.c_str(), &find_data);

    if (handle == -1) {
        ARK_LOG_ERROR("can not open path={}", path);
        return false;
    }

    do {
        if (find_data.attrib & _A_SUBDIR) {
            /// is sub dir
            if (strcmp(find_data.name, ".") == 0 || strcmp(find_data.name, "..") == 0) {
                continue;
            }

            auto new_dir = path + physic_slash + find_data.name;
            if (!get_scene_file_list(path, new_dir, sub_path_list)) {
                ARK_LOG_ERROR("read path failed path={}", new_dir);
                return false;
            }
        } else {
            /// not sub dir
            if (father_path.empty()) {
                ARK_LOG_ERROR("not in a scene path={} file = {}", path, find_data.name);
                return false;
            }

            ///cut to get scene_id
            auto off = path.find(father_path);
            auto scene_id = path.substr(off + father_path.length() + 1, path.length());
            auto file_path = path + physic_slash + find_data.name;
            sub_path_list.insert(std::make_pair(scene_id, file_path));
        }
    } while (_findnext(handle, &find_data) == 0);

    _findclose(handle);
    return true;

#elif defined(ARK_PLATFORM_LINUX) || defined(ARK_PLATFORM_DARWIN)
    DIR* dir = opendir(path.c_str());
    if (dir == NULL) {
        ARK_LOG_ERROR("can not open path={}", path);
        return false;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR) {
            ///is sub dir
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }

            string new_dir = path + physic_slash + entry->d_name;
            if (!get_scene_file_list(path, new_dir, sub_path_list)) {
                ARK_LOG_ERROR("read path failed path={}", new_dir);
                return false;
            }
        } else {
            /// not sub dir
            if (father_path.empty()) {
                ARK_LOG_ERROR("not in a scene path={} file = {}", path, entry->d_name);
                return false;
            }

            ///cut to get scene_id
            auto off = path.find(father_path);
            auto scene_id = path.substr(off + father_path.length() + 1, path.length());
            auto file_path = path + physic_slash + entry->d_name;
            sub_path_list.insert(std::make_pair(scene_id, file_path));
        }
    }

    closedir(dir);
    return true;
#endif

    return false;
}

bool physic_config_module_impl::load_config() {
    const auto path = get_plugin_manager()->GetResPath() + physic_config_path;
    const auto settings = physic_resources::read_settings(path);
    config_.simulate = settings.simulate;
    config_.dispatcher_threads = settings.dispatcher_threads;
    config_.step = 1.0F / static_cast<float>(settings.frequency_hz);
    config_.pvd_host = settings.debugger_host;
    config_.pvd_port = settings.debugger_port;
    config_.pvd_timeout = settings.debugger_timeout_ms;
    return true;
}

std::string physic_config_module_impl::gen_cooked_file_path(const std::string& file_path) {
    auto file_path_cooked = file_path;
    string_utils::replace(file_path_cooked, physic_scene_path.c_str(), physic_cooked_path.c_str());

    ///create file in path
    create_file_path(file_path_cooked);

    return file_path_cooked;
}

void physic_config_module_impl::create_file_path(const std::string& file_path) {
#ifdef ARK_PLATFORM_WIN
    int len = file_path.length();
    char temp_path[256] = {0};
    for (int i = 0; i < len; i++) {
        temp_path[i] = file_path[i];
        if (temp_path[i] != '/') {
            continue;
        }

        if (_access(temp_path, 0) != -1) {
            continue;
        }

        int ret = _mkdir(temp_path);
        if (ret == -1) {
            return;
        }
    }
#elif defined(ARK_PLATFORM_LINUX) || defined(ARK_PLATFORM_DARWIN)
    int len = file_path.length();
    char temp_path[256] = {0};
    for (int i = 0; i < len; i++) {
        temp_path[i] = file_path[i];
        if (temp_path[i] != '/') {
            continue;
        }

        if (access(temp_path, F_OK) != -1) {
            continue;
        }

        mkdir(temp_path, 0755);
    }
#endif
}

} // namespace ark
