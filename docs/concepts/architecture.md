# Applications, plugins and modules

ark composes one application process from plugin libraries. Each plugin registers modules; modules expose interfaces and receive lifecycle calls from `plugin_manager`. Use this page to place responsibilities before following the [application example](application-architecture.md).

## Responsibility boundaries

| Component | Owns | Does not provide automatically |
| --- | --- | --- |
| Application launcher | Process identity, manifest path, plugin selection and the update loop | A cluster scheduler or dependency resolver |
| Plugin | Installation and removal of a group of modules | Safe replacement of code while callbacks still execute |
| Module | A focused interface, its state and lifecycle work | Concurrent access safety for arbitrary business state |
| Kernel plugin | Metadata, static configuration, runtime entities and their events | Automatic persistence or client synchronization merely from declaring a field |
| Network plugin | ark message headers, session identifiers and delivery to the application update thread | Transport algorithms or a separate thread per connection |
| HTTP plugin | HTTP routing and listener shutdown through arknet | Automatic dispatch of route handlers to the application update thread |
| arknet | IO contexts, worker execution and transport operations | Ownership of ark's business entities or application lifecycle |

[Application composition](application-architecture.md), [data-driven design](data-driven.md) and [reference server topology](server-architecture.md) describe these boundaries at different scales. The server topology is a deployment reference, not a prerequisite for using modules.

## Calls within a process

A plugin's `install()` registers an interface through `plugin_base::register_module()` or the retained `ARK_REGISTER_MODULE(interface_type, implementation_type)` macro. The manager records that interface, and application code obtains it through typed lookup:

```cpp
#include "ark/plugin_manager.hpp"
#include "ark/utility/timer_module.hpp"

ark::timer_module* find_timers(ark::plugin_manager& manager) {
    return manager.find_module<ark::timer_module>();
}
```

The returned pointer is borrowed; keep the owning plugin loaded while using it. A missing module triggers an assertion when assertions are enabled, and returns null when they are disabled, so typed lookup is for a required, registered dependency rather than optional-service probing. A header include or CMake dependency does not install a runtime module. See [create a module](../guides/modules.md) and [create a plugin](../guides/plugins.md).

## Lifecycle and execution

Startup runs `init()` for modules, then `post_init()`, `check_config()` and `pre_update()`. The first `false` result stops startup; the launcher reports the failure and terminates with exit code `1`, without rolling back partially started plugins. The launcher repeatedly calls the manager's `update()`; only registered update modules participate in that loop. Shutdown first stops managed task admission and waits for accepted tasks and module readiness, then runs `pre_shut()` and `shut()` before unloading plugin libraries. See the [application guide](../guides/applications.md) for timeout and resource-retention behavior.

Use `init()` to obtain dependencies and initialize local resources; use later stages for work that requires other modules' initialization. Stop timers, subscriptions and asynchronous producers before releasing the state their callbacks reference. Network business delivery is advanced through the application's update calls; network IO still runs on arknet workers.

Current limits matter when composing modules:

- Required module provider keys are validated after installation; all four startup phases run in provider-first order, with stable registration-key ordering for independent ready nodes. Declare same-phase requirements through [module dependencies](../guides/modules.md#declare-startup-dependencies). Manifest order, update order and shutdown order are separate contracts.
- Startup failure propagation is immediate; transactional rollback of partially started plugins is not provided.
- Lifecycle stages do not establish that arbitrary business tasks have drained. Compatible hot replacement and plugin-unload coordination remain separate design work.

The source of these rules is [plugin_manager.hpp](https://github.com/OpenArkStudio/ark/blob/main/src/core/plugin_manager.hpp) and the [launcher](https://github.com/OpenArkStudio/ark/blob/main/src/application/main.cpp).

## Header paths and CMake targets

Core headers live in `src/core/` and the launcher in `src/application/`. Application manifests and shared configuration reading live in `src/config/`; all official plugin headers and implementations live in their own `src/plugins/<name>/` directories. Kernel keeps `src/plugins/kernel/entity.hpp` beside its implementation and retains `<ark/kernel/entity.hpp>`. MySQL, Redis, Utility, Net, Bus, HTTP and Kernel co-locate their public headers and implementation in `src/plugins/<name>/`; CMake maps only explicitly listed public headers into the build tree's `src/plugins/sdk-include/ark/<name>/`. Consume `ark::mysql`, `ark::redis`, `ark::utility`, `ark::net`, `ark::bus`, `ark::http` or `ark::kernel` to retain logical `<ark/...>` include paths through explicit mappings. Kernel retains its existing assembly header and concrete value/template types; this move changes no registration or visibility. The core target maps its explicit public headers into its own generated include directory. `ark::core` supplies core headers; individual plugin targets remain optional, and `ark::ark` aggregates available API targets.

Public plugin/module hpp and cpp files share their functional source directory. Header mapping creates only a generated include view in the selected build tree; it does not define another source layer.

`ark/` is a filesystem include prefix. It does not automatically create the C++ namespace: each header explicitly declares `namespace ark`. Linking an API interface target propagates compilation requirements; it does not select or load a plugin shared library.

Some plugins retain public mappings for existing implementation headers. Net retains the public HTTP client and transport helper types documented in its [API guide](../reference/network.md); the manager implementation is private. Kernel retains existing `data_impl` and `data_list_impl` business consumption. For the remaining plugins, treat `*_impl` types as implementation details, and do not interpret the current paths as a completed public/private separation. The diagrams describe responsibilities rather than prescribing a physical directory layout. See [directory structure](../reference/directory-structure.md) for the current paths and targets.
