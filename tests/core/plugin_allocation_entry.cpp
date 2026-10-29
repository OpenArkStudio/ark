// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#include <ark/plugin_manager.hpp>

namespace {

using entry_callback = void (*)(ark::plugin_manager*, const std::string&);
using exit_callback = void (*)(ark::plugin_manager*);
entry_callback entry{};
exit_callback exit_entry{};

} // namespace

ARK_EXPORT_FUNC void allocation_probe_set_entry(entry_callback callback) {
    entry = callback;
}

ARK_EXPORT_FUNC void allocation_probe_set_exit(exit_callback callback) {
    exit_entry = callback;
}

ARK_EXPORT_FUNC void DllEntryPlugin(ark::plugin_manager* manager, const std::string& name, ark::logger* shared_logger) {
    ark::logger::init(shared_logger);
    entry(manager, name);
}

ARK_EXPORT_FUNC void DllExitPlugin(ark::plugin_manager* manager) {
    if (exit_entry)
    {
        exit_entry(manager);
    }
}
