// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <type_traits>

#include <ark/application_manifest.hpp>

static_assert(std::is_base_of_v<std::runtime_error, ark::application_manifest_error>);
static_assert(
    std::is_same_v<decltype(&ark::load_application_manifest), ark::application_manifest (*)(const std::string&)>);

int main() {
    const ark::application_manifest manifest;
    return manifest.plugins.empty() && manifest.logging.console && !manifest.logging.file &&
                   manifest.logging.level == "info" && manifest.logging.queue_count == 0
               ? 0
               : 1;
}
