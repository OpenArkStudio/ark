// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace ark::test {

class metadata_files
{
public:
    metadata_files()
        : previous_(std::filesystem::current_path())
        , directory_(
              std::filesystem::temp_directory_path() /
              ("ark-module-init-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
        std::filesystem::create_directories(directory_ / "resource" / "meta");
        write("entity_schema.json",
            R"({"format":"ark.entity-schema","version":1,"classes":[{"name":"Player","fields":[],"tables":[],"containers":[]},{"name":"attribute","fields":[{"name":"id","index":1,"type":"uint32"},{"name":"level","index":2,"type":"int32"}],"tables":[],"containers":[]}],"configs":[{"class":"attribute","rows":[{"id":42,"level":7}]}]})");
        std::filesystem::current_path(directory_);
    }

    ~metadata_files() {
        std::error_code ignored;
        std::filesystem::current_path(previous_, ignored);
        std::filesystem::remove_all(directory_, ignored);
    }

    void write(const char* name, const std::string& contents) {
        std::ofstream output(directory_ / "resource" / "meta" / name);
        output.exceptions(std::ios::badbit | std::ios::failbit);
        output << contents;
    }

private:
    std::filesystem::path previous_;
    std::filesystem::path directory_;
};

} // namespace ark::test
