// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <iostream>

#include <ark/macros.hpp>

namespace ark {
class plugin_manager;
}

namespace {

struct library_probe {
    library_probe() {
        std::cerr << "app-startup-test:library_loaded\n";
    }
};

library_probe loaded_library;

} // namespace

// A loadable library deliberately omitting DllEntryPlugin.
ARK_EXPORT_FUNC void DllExitPlugin(ark::plugin_manager*) {
    std::cerr << "app-startup-test:uninstall\n";
}
