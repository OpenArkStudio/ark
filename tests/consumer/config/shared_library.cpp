// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "../../network/doctest.h"

#include <ark/application_manifest.hpp>
#include "shared.hpp"

TEST_CASE("Application and plugin readers link and diagnose from a shared library") {
    const auto path =
        std::filesystem::temp_directory_path() /
        ("ark-shared-config-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".toml");

    struct cleanup {
        std::filesystem::path path;

        ~cleanup() {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }
    } remove_file{path};

    const auto write = [&](const std::string& content) {
        std::ofstream output(path, std::ios::binary);
        output.exceptions(std::ios::badbit | std::ios::failbit);
        output << content;
    };
    write("[application]\nplugin_library_dir='shared/'\n");
    CHECK(read_config_from_shared_library(path.string(), true));
    write("[http]\nhost='localhost'\nport=0\nthreads=1\n");
    CHECK(read_config_from_shared_library(path.string(), false));
    write("[broken\nsecret='SECRET_SENTINEL'\n");
    for (const auto application : {true, false}) {
        try {
            static_cast<void>(read_config_from_shared_library(path.string(), application));
            FAIL("malformed configuration accepted by shared library");
        } catch (const std::runtime_error& error) {
            CHECK(std::string(error.what()).find(path.string() + ":1:") != std::string::npos);
            CHECK(std::string(error.what()).find("SECRET_SENTINEL") == std::string::npos);
        }
    }
    std::filesystem::remove(path);
    CHECK_THROWS_AS(read_config_from_shared_library(path.string(), true), ark::application_manifest_error);
    CHECK_THROWS_AS(read_config_from_shared_library(path.string(), false), std::runtime_error);
}
