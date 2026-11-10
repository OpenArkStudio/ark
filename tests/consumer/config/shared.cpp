// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <ark/application_manifest.hpp>
#include "detail/plugin_configuration.hpp"
#include "shared.hpp"

bool read_config_from_shared_library(const std::string& path, bool application) {
    if (application) {
        return ark::load_application_manifest(path).plugin_library_dir == "shared/";
    }
    return ark::plugin_config_detail::load_http(path).host == "localhost";
}
