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
    explicit metadata_files(bool leading_separator, bool duplicate_id = false)
        : previous_(std::filesystem::current_path())
        , directory_(
              std::filesystem::temp_directory_path() /
              ("ark-module-init-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
        std::filesystem::create_directories(directory_ / "resource" / "meta");
        const std::string meta = leading_separator ? "/meta/" : "meta/";
        write("config_class.config", "<xml><config id=\"attribute\" meta=\"" + meta + "attribute.config\" res=\"" +
                                         meta + "attribute.data\"/></xml>");
        write("entity_class.config", "<xml><config meta=\"" + meta + "player.config\"/></xml>");
        write("attribute.config", R"(<xml><meta field="id" type="uint64"/><meta field="level" type="int"/></xml>)");
        write("attribute.data", duplicate_id ? R"(<xml><data id="42" level="7"/><data id="42" level="9"/></xml>)"
                                             : R"(<xml><data id="42" level="7"/></xml>)");
        write("player.config", R"(<xml><meta class="Player" type="class"/></xml>)");
        std::filesystem::current_path(directory_);
    }

    ~metadata_files() {
        std::error_code ignored;
        std::filesystem::current_path(previous_, ignored);
        std::filesystem::remove_all(directory_, ignored);
    }

private:
    void write(const char* name, const std::string& contents) {
        std::ofstream output(directory_ / "resource" / "meta" / name);
        output.exceptions(std::ios::badbit | std::ios::failbit);
        output << contents;
    }

    std::filesystem::path previous_;
    std::filesystem::path directory_;
};

} // namespace ark::test
