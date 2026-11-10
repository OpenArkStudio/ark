#pragma once

#include <functional>
#include <list>
#include <string>

#include <physX/include/PxPhysicsAPI.h>

#include "ark/macros.hpp"
#include "ark/math/vector3d.hpp"
#include "ark/platform.hpp"

namespace ark {

const static std::string physic_scene_path = "physic/scene";
const static std::string physic_config_path = "physic/physic.toml";
const static std::string physic_slash = "/";
const static std::string physic_mesh_file_end = ".obj";
const static std::string physic_collider_file_end = ".json";
const static std::string physic_cooked_path = "physic/cache";
const static physx::PxVec3 zero_vec3 = physx::PxVec3();
const static physx::PxReal zero_real = 0.0F;
const static physx::PxU8 zero_u8 = 0U;
const static physx::PxReal default_density = 10.0F;

using physx_net_id = uint64_t;
using physx_scene_id = std::string;
using physx_scene_index = int32_t;

namespace physic {

inline physx::PxI32 open_file(FILE** file, const char* name, const char* mode) {
#ifdef ARK_PLATFORM_WIN
    static const physx::PxU32 max_length = 300;
    char buf[max_length + 1];

    physx::PxU32 i;
    for (i = 0; i < max_length && name[i]; i++) {
        buf[i] = name[i] == '/' ? '\\' : name[i];
    }
    buf[i] = 0;

    return i == max_length ? -1 : ::fopen_s(file, buf, mode);
#elif defined(ARK_PLATFORM_LINUX) || defined(ARK_PLATFORM_DARWIN)
    FILE* fp = ::fopen(name, mode);
    if (fp) {
        *file = fp;
        return 0;
    }
    return -1;
#endif

    return -1;
};

} // namespace physic

struct physic_config {
    bool simulate{false};
    physx::PxU32 dispatcher_threads{0};
    physx::PxReal step{0.0333F};
    std::string pvd_host{""};
    int32_t pvd_port{0};
    uint32_t pvd_timeout{0};
};

struct physic_mesh_file {
    std::string path{""};
    std::string cooked_path{""};
};

class collider {
public:
    virtual ~collider() = default;

    virtual bool is_box_collider() const {
        return false;
    }

    virtual bool is_capsule_collider() const {
        return false;
    }

    virtual bool is_sphere_collider() const {
        return false;
    }

    virtual bool is_mesh_collider() const {
        return false;
    }

    virtual bool is_terrain_collider() const {
        return false;
    }

    bool is_trigger() const {
        return trigger;
    }

public:
    bool trigger{false};
};

class box_collider : public collider {
public:
    bool is_box_collider() const override {
        return true;
    }

public:
    physx::PxVec3 position{zero_vec3};
    physx::PxVec3 center{zero_vec3};
    physx::PxVec3 size{zero_vec3};
};

class capsule_collider : public collider {
public:
    bool is_capsule_collider() const override {
        return true;
    }

public:
    physx::PxVec3 position{zero_vec3};
    physx::PxVec3 center{zero_vec3};
    physx::PxReal radius{zero_real};
    physx::PxReal height{zero_real};
};

class sphere_collider : public collider {
public:
    bool is_sphere_collider() const override {
        return true;
    }

public:
    physx::PxVec3 position{zero_vec3};
    physx::PxVec3 center{zero_vec3};
    physx::PxReal radius{zero_real};
};

class mesh_collider : public collider {
public:
    ~mesh_collider() {
        ARK_DELETE_ARRAY(physx::PxVec3, vertices);
        ARK_DELETE_ARRAY(physx::PxU32, triangles);
    }

    bool is_mesh_collider() const override {
        return true;
    }

public:
    bool convex{false};
    physx::PxVec3 position{zero_vec3};
    uint32_t vertex_count{0};
    uint32_t triangle_count{0};
    physx::PxVec3* vertices{nullptr};
    physx::PxU32* triangles{nullptr};

    physic_mesh_file mesh_file;
};

class physic_scene {
public:
    ~physic_scene() {
        for (auto iter : colliders) {
            if (iter) {
                ARK_DELETE(iter);
            }
        }
    }

public:
    std::string scene_id{""};
    physic_mesh_file mesh_file;

    physx::PxVec3 position{zero_vec3};

    using collider_list = std::list<collider*>;
    collider_list colliders;
};

using physic_scene_list = std::unordered_map<std::string, physic_scene>;

} // namespace ark
