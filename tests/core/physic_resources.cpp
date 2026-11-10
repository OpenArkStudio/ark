// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "../network/doctest.h"
#include <nlohmann/json.hpp>

#include "../../src/plugins/physic/physic_resources.hpp"

namespace {

struct resource_directory {
    std::filesystem::path path;

    resource_directory() {
        const auto prefix =
            "ark-physic-resources-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        for (unsigned i = 0;; ++i) {
            path = std::filesystem::temp_directory_path() / (prefix + "-" + std::to_string(i));
            if (std::filesystem::create_directory(path)) {
                break;
            }
        }
    }

    ~resource_directory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }

    std::string write(const char* filename, const std::string& contents) const {
        const auto file = path / filename;
        std::ofstream output(file, std::ios::binary);
        REQUIRE(output.is_open());
        output << contents;
        output.close();
        REQUIRE(output.good());
        return file.string();
    }
};

std::string settings() {
    return "simulate = true\ndispatcher_threads = 2\nfrequency_hz = 10\n"
           "[debugger]\nhost = '127.0.0.1'\nport = 5425\ntimeout_ms = 3600000\n";
}

} // namespace

TEST_CASE("Physic resources read the shipped TOML settings without a PhysX SDK") {
    const auto value = ark::physic_resources::read_settings(std::string(ARK_TEST_RESOURCE_DIR) + "/physic/physic.toml");
    CHECK(value.simulate);
    CHECK(value.dispatcher_threads == 2);
    CHECK(value.frequency_hz == 10);
    CHECK(value.debugger_host == "127.0.0.1");
    CHECK(value.debugger_port == 5425);
    CHECK(value.debugger_timeout_ms == 3600000);
}

TEST_CASE("Physic resources reject invalid TOML values and XML contents") {
    resource_directory files;
    auto document = settings();
    SUBCASE("zero frequency") {
        document.replace(document.find("frequency_hz = 10"), 17, "frequency_hz = 0");
    }
    SUBCASE("negative worker count") {
        document.replace(document.find("dispatcher_threads = 2"), 22, "dispatcher_threads = -1");
    }
    SUBCASE("port exceeds native range") {
        document.replace(document.find("port = 5425"), 11, "port = 65536");
    }
    SUBCASE("legacy XML") {
        document = "<root><node SimulateOn='1'/></root>";
    }
    const auto path = files.write("physic.toml", document);
    try {
        (void)ark::physic_resources::read_settings(path);
        FAIL("Invalid physics settings must not load");
    } catch (const std::runtime_error& error) {
        CHECK(std::string(error.what()).find("physic.toml") != std::string::npos);
    }
}

TEST_CASE("Physic resources load all shipped collider types with zero-based mesh indices") {
    using type = ark::physic_resources::collider_type;
    const auto colliders =
        ark::physic_resources::read_colliders(std::string(ARK_TEST_RESOURCE_DIR) + "/physic/scene/town/colliders.json");
    REQUIRE(colliders.size() == 6);
    CHECK(colliders[0].type == type::capsule);
    CHECK(colliders[1].type == type::box);
    CHECK(colliders[2].type == type::mesh);
    CHECK(colliders[3].type == type::terrain);
    CHECK(colliders[4].type == type::mesh);
    CHECK(colliders[5].type == type::sphere);
    CHECK(colliders[0].position[0] == 10);
    CHECK(colliders[0].position[2] == 10);
    CHECK(colliders[0].center[0] == 0);
    CHECK(colliders[0].radius == 0.5F);
    CHECK(colliders[0].height == 2);
    CHECK(colliders[1].size[0] == 2);
    CHECK(colliders[1].trigger);
    CHECK(colliders[3].position[0] == -250);
    CHECK(colliders[2].convex);
    CHECK_FALSE(colliders[4].convex);
    CHECK(colliders[5].radius == 2);
    for (const auto index : {2U, 4U}) {
        const auto& mesh = colliders[index];
        REQUIRE(mesh.vertices.size() == 88);
        REQUIRE(mesh.triangles.size() == 240);
        CHECK(*std::min_element(mesh.triangles.begin(), mesh.triangles.end()) == 0);
        CHECK(*std::max_element(mesh.triangles.begin(), mesh.triangles.end()) == 87);
    }
}

TEST_CASE("Physic resources reject malformed collider geometry before creating SDK objects") {
    resource_directory files;
    auto document = nlohmann::json::parse(R"({"version":1,"colliders":[{"type":"mesh","position":[0,0,0],
        "trigger":false,"convex":true,"vertices":[[0,0,0],[1,0,0],[0,1,0]],"triangles":[0,1,2]}]})");
    auto& collider = document["colliders"][0];
    SUBCASE("index past the last vertex") {
        collider["triangles"][2] = 3;
    }
    SUBCASE("negative index") {
        collider["triangles"][0] = -1;
    }
    SUBCASE("fractional index") {
        collider["triangles"][0] = 0.5;
    }
    SUBCASE("incomplete triangle") {
        collider["triangles"] = {0, 1};
    }
    SUBCASE("invalid position length") {
        collider["position"] = {0, 1};
    }
    SUBCASE("coordinate outside float range") {
        collider["position"][0] = 1e100;
    }
    SUBCASE("unknown collider") {
        collider["type"] = "unknown";
    }
    SUBCASE("unsupported format version") {
        document["version"] = 2;
    }
    const auto path = files.write("colliders.json", document.dump());
    try {
        (void)ark::physic_resources::read_colliders(path);
        FAIL("Invalid collider data must not load");
    } catch (const std::runtime_error& error) {
        CHECK(std::string(error.what()).find("colliders.json") != std::string::npos);
    }
}
