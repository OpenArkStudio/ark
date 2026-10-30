// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
#pragma once

#include <utility>

#include "ark/net/net_server_service.hpp"
#include "ark/plugin_manager.hpp"

namespace ark {

// Opt in only callbacks that begin new external business workflows. Admission
// occurs at callback entry, not when bytes arrive or enter the receive queue.
// Both handlers are required; rejection policy belongs to the application.
// Register and dispatch on the manager's owner thread. The manager and every
// borrowed receiver must outlive the service's callback registration. This helper
// neither unregisters callbacks nor guarantees independent module/DLL removal.
// Message pointers are borrowed for the invocation. Avoid a strong service
// capture that would create a service -> callback -> service ownership cycle.
inline bool reg_external_root_callback(net_server_service& service, plugin_manager& manager, msg_id_t id,
    net_msg_functor on_accept, net_msg_functor on_reject) {
    if (!on_accept || !on_reject)
    {
        return false;
    }
    return service.reg_msg_callback(id, [&manager, on_accept = std::move(on_accept), on_reject = std::move(on_reject)](
                                            const net_msg* message, conv_id_t session) {
        if (manager.get_shutdown_state() == plugin_manager::shutdown_state::running)
        {
            on_accept(message, session);
        }
        else
        {
            on_reject(message, session);
        }
    });
}

} // namespace ark
