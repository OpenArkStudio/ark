# Create a plugin

A plugin installs and uninstalls a group of modules. Its shared library exports the entry points used by the ark launcher; its modules expose the interfaces used by application code. Core and official plugins live in ark, while business plugins live with their application.

This guide builds a business plugin containing the counter and consumer from [Create a module](modules.md). Keep `sample_module.hpp`, `sample_plugin.hpp`, `sample_plugin.cpp` and `CMakeLists.txt` together for this example.

## Declare the plugin

Save as `sample_plugin.hpp`:

```cpp
#pragma once

#include <ark/plugin.hpp>
#include <ark/plugin_manager.hpp>

namespace ark {

ARK_DECLARE_PLUGIN(sample_plugin)

} // namespace ark
```

`ARK_DECLARE_PLUGIN` supplies the plugin class, manager/configuration accessors and the declarations of `install()` and `uninstall()`. The macros below use ark's unqualified framework types, so the example declares them inside `namespace ark`.

## Pair registration and unregistration

Save as `sample_plugin.cpp`:

```cpp
#include "sample_module.hpp"
#include "sample_plugin.hpp"

namespace ark {

ARK_DECLARE_PLUGIN_DLL_FUNCTION(sample_plugin)

void sample_plugin::install() {
    ARK_REGISTER_MODULE(counter_module, counter_module_impl);
    ARK_REGISTER_MODULE(sample_module, sample_module);
}

void sample_plugin::uninstall() {
    ARK_UNREGISTER_MODULE(sample_module, sample_module);
    ARK_UNREGISTER_MODULE(counter_module, counter_module_impl);
}

} // namespace ark
```

| Operation | Meaning |
| --- | --- |
| `ARK_REGISTER_MODULE(counter_module, counter_module_impl)` | Allocate the implementation, set its manager/name and register it under the public interface |
| `ARK_REGISTER_MODULE(sample_module, sample_module)` | Register a simple module using the same type for interface and implementation |
| `ARK_UNREGISTER_MODULE(...)` | Remove the corresponding module/update entries and delete the module object |
| `ARK_DECLARE_PLUGIN_DLL_FUNCTION(sample_plugin)` | Define the exported `DllEntryPlugin` / `DllExitPlugin` functions and plugin configuration storage |

<Aside type="note" title="One entry-point pair per library">

Place the DLL macro in exactly one translation unit per shared library. It generates fixed C entry-point names; adding it to every module source or exporting two independently declared plugin entry points from one library causes duplicate definitions. The C entry points still pass C++ types, so they do not establish a compiler-independent ABI.

</Aside>

Keep `install()` focused on registration. Resolve cross-module dependencies during the lifecycle stages described in [Create a module](modules.md). Pair every registered interface/implementation with an unregistration operation. The example removes the consumer before its counter; that local order does not establish a dependency order between separate plugins.

## Build the shared library

Install ark's [build prerequisites](../get-started/installation.md), and save the following as `CMakeLists.txt` in the business-plugin directory:

```cmake
cmake_minimum_required(VERSION 3.21)
project(sample_business LANGUAGES CXX)

set(ARK_SOURCE_DIR "" CACHE PATH "ark source checkout")
set(ARK_BUILD_APPLICATIONS OFF CACHE BOOL "Build ark applications" FORCE)
set(ARK_BUILD_PROTO OFF CACHE BOOL "Build protocol messages" FORCE)
set(ARK_BUILD_TESTS OFF CACHE BOOL "Build ark tests" FORCE)
set(ARK_BUILD_CORE_TESTS OFF CACHE BOOL "Build core tests" FORCE)
set(ARK_BUILD_EXAMPLES OFF CACHE BOOL "Build ark examples" FORCE)
add_subdirectory("${ARK_SOURCE_DIR}" ark)

add_library(sample_plugin SHARED sample_plugin.cpp)
target_link_libraries(sample_plugin PRIVATE ark::core)
set_target_properties(sample_plugin PROPERTIES
    PREFIX ""
    DEBUG_POSTFIX "_d"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
```

Configure with the path of the ark checkout. These commands work with a single-configuration generator or a multi-configuration generator such as Visual Studio:

```sh
cmake -S . -B out -DARK_SOURCE_DIR="path/to/ark" -DCMAKE_BUILD_TYPE=Release
cmake --build out --config Release --target sample_plugin
```

The plugin build disables ark's bundled applications/examples; it does not disable this external `sample_plugin` target. `ark::core` carries the public headers, C++20 settings and transitive dependencies. If an interface includes official plugin APIs, add their interface targets, such as `ark::net`. See [Project layout and public plugin interfaces](../reference/directory-structure.md) for the full target list.

## Select libraries at runtime

Build requirements and runtime selection are separate:

| Setting | Effect |
| --- | --- |
| `target_link_libraries(sample_plugin PRIVATE ark::net)` | Makes the network API headers and usage requirements available at build time; does not load a network plugin |
| `ARK_BUILD_APPLICATIONS=ON` and `ARK_BUILD_PLUGIN_NET=ON` in an ark application build | Builds the selected official runtime library |
| A plugin entry in the application manifest | Requests that shared library when the launcher starts |

For this Release example, copy the built shared library from `out/bin/Release/` into the deployment's `bin/` and save this manifest as `conf/sample.app.toml`. Debug artifacts are kept separately in `out/bin/Debug/`:

```toml
[application]
plugin_library_dir = "bin/"
plugin_config_dir = "conf/"

[[plugins]]
name = "sample_plugin"
config = ""
```

Use the [application launcher](applications.md) to select this manifest. The library name `sample_plugin` is the filename stem, not the RTTI name of the C++ plugin class or a registered module. The loader appends `.dll`, `.so` or `.dylib`; Debug also appends `_d` before the extension. The CMake target deliberately has no `lib` prefix and sets the matching Debug postfix. Use the same build configuration and compatible public plugin interfaces and ABI settings for the launcher and plugin.

Paths are resolved from the process working directory. The current loader concatenates `plugin_library_dir` with the filename and `plugin_config_dir` with `config`; keep the trailing separators. An empty `config` in this example means the plugin has no configuration file to read. It still receives the configuration directory prefix; it does not receive an empty resolved path. A supplied plugin configuration path can be read through `sample_plugin::get_plugin_conf()`.

On Windows, library paths are UTF-8. The loader converts them to absolute wide-character paths with native separators before `LoadLibraryExW(..., LOAD_WITH_ALTERED_SEARCH_PATH)`. This supports dependencies beside the loaded plugin when the executable and working directory are elsewhere, while retaining the existing altered-search policy and PATH handling. It does not set a process-wide DLL directory or copy dependencies beside individual executables. Native load failures retain `GetLastError()` for the manager diagnostic; error 126 can identify a missing dependency as well as the requested DLL. Non-Windows loading is unchanged. Deploy the matching `AFProto` DLL and other dependencies using the [protocol deployment contract](../reference/protobuf.md#build-and-include).

## Diagnose a missing module

| Symptom | Check |
| --- | --- |
| Library cannot be opened | Deployment working directory, filename/prefix/extension, Debug postfix and shared-library dependencies |
| Entry point cannot be found | DLL macro is defined once in the library and exported symbols are visible |
| Module lookup asserts, or returns `nullptr` with assertions disabled | The runtime manifest includes the provider library, `install()` registers `T`, and consumer/provider use compatible interface definitions |
| Includes compile but no service is available | An interface target supplies headers; it does not install a runtime module |

The manager does not make plugin unloading safe for arbitrary outstanding callbacks. Finish the service-specific cancellation and lifetime work described in [Create a module](modules.md). This example has no worker tasks, event subscriptions or hot-update support.

## Use typed registration and explicit entries

`<ark/plugin_base.hpp>` provides `ark::plugin_base<derived_type>` as an alternative to
`ARK_DECLARE_PLUGIN`. It supplies version `0`, the existing `typeid(derived_type).name()`
identity, manager accessors, and `get_plugin_conf()` / `set_plugin_conf()`.
Configuration is shared by instances of the same derived plugin and isolated
between different derived types. Set the manager before installation and keep it
unchanged while modules are registered.

A private factory can return an interface without exposing its concrete class
([module example](modules.md#use-a-base-class-with-a-private-implementation)):

```cpp
#include <stdexcept>
#include <ark/plugin_base.hpp>
#include "counter_factory.hpp"

class counter_plugin final : public ark::plugin_base<counter_plugin>
{
public:
    void install() override {
        if (!register_module(make_counter(), ark::module_update::disabled)) {
            throw std::runtime_error("counter registration rejected");
        }
    }
    void uninstall() override {
        if (!unregister_module<ark::counter_module>()) {
            throw std::runtime_error("counter unregistration rejected");
        }
    }
};

ARK_EXPORT_FUNC void DllEntryPlugin(ark::plugin_manager* manager,
    const std::string& stem, ark::logger* log) {
    ark::logger::init(log);
    manager->register_plugin<counter_plugin>(stem);
}

ARK_EXPORT_FUNC void DllExitPlugin(ark::plugin_manager* manager) {
    manager->unregister_plugin<counter_plugin>();
}
```

Both helpers are protected templates. `register_module<interface_type>(candidate,
module_update::enabled)` opts into update; `disabled` excludes it even when the
implementation overrides `update()`. There is no default or override detection.
The factory return type determines the interface when the template argument is
omitted. Successful registration preserves the factory's interface address and
uses `typeid(interface_type).name()` for lookup.

The candidate must exclusively own an object not yet published to the registry;
remove any legacy borrowed observers before transferring it. Registration consumes
the `unique_ptr`. A null candidate, duplicate module
identity, occupied update identity, or already registered address returns false.
A rejected candidate is destroyed once. Metadata/allocation exceptions and the
existing owner-thread, callback-phase and shutdown guards propagate; an incomplete
registry insertion leaves no borrowed pointer to the rejected candidate. These
helpers do not undo earlier successful registrations or run cleanup hooks.

After success, ownership follows the explicit unregister/delete path used by the
legacy macros. `unregister_module<interface_type>()` returns false for a missing owned
identity or a registry identity mismatch. Active callbacks or pending managed
work prevent removal through the existing guards, retaining the module and its
records. A successful removal deletes the module once. Implement `uninstall()`
in the required module order; destruction alone does not uninstall or delete
registered modules. A later installation failure retains earlier resources under
the existing startup-failure process-exit policy, without same-process rollback
or retry.

`plugin_manager::register_plugin<T>()` keeps `ARK_NEW`/`ARK_DELETE` allocation
hooks, including nullable nothrow allocation. A null allocation returns without
installation and records `allocate_plugin: <library stem>` as the startup failure.
The manager then returns false from `start()` before later libraries or module phases.
Before `install()` starts, a metadata exception destroys the plugin
candidate once; once installation starts, the existing process-exit policy above
applies. Dynamic-library wrappers also have scoped ownership until published to
the manager. Failed native loads retire the wrapper even if logging throws;
loaded handles keep the existing explicit unload policy. No caller migration is
required.

Existing macros and plugins remain supported. Export the entry functions
explicitly with the existing platform visibility macro. The plugin boundary
still passes C++ types; this API does not promise a pure C ABI or cross-version
ABI stability. Lifecycle phases, draining and library unload rules are unchanged.

These plugins use typed construction. Rebuild plugins with changed public layouts and their consumers together; Net's public implementation-header removal also requires rebuilding affected consumers.

| Plugin | Public surface and construction | Preserved behavior and migration limits |
| --- | --- | --- |
| [Net](../reference/network.md#api-and-private-construction) | Private factory returns `std::unique_ptr<http_client_module>`; applications use `<ark/net/http_client_module.hpp>` and typed lookup | Updates enabled; old `http_client_module_impl.hpp` removed from public/install interfaces |
| [Kernel](../reference/kernel.md#typed-kernel-construction) | Public `*_impl`, `kernel_plugin` types and header paths retained; private factory header not installed | Public class layouts change; interface keys, update selection and registration order unchanged |
| [Consul](../reference/directory-structure.md#typed-consul-construction) | All three public headers/types retained; private factory not installed | Public implementation/plugin layouts change; interface RTTI key and disabled updates unchanged; linked guide covers test entry points and dependencies |
| [Etcd Client](../reference/directory-structure.md#typed-etcd-client-construction) | All five public headers/types retained; typed bases, private interface-returning factory and explicit entry/exit functions | Public implementation/plugin layouts change; interface RTTI key and disabled updates unchanged; linked guide covers optional JSON dependencies and tests without an Etcd service |
| [OSS SDK](../reference/directory-structure.md#typed-oss-sdk-construction) | All three public headers/types retained; typed bases and private interface-returning factory | Public layouts change; interface RTTI key and disabled updates unchanged; typed unregistration replaces the undefined legacy call; still a TODO scaffold; linked guide covers build/test limits |
| [Kafka](../reference/directory-structure.md#typed-kafka-construction) | All four public headers and both RdKafka callback bases retained; typed construction and explicit entries | Public layouts change; interface key and enabled frame updates unchanged; runtime plugin off by default; linked guide covers driver requirements and tests without a broker connection |

## Logger and singleton lifetime

`logger::init(log)` borrows the host logger. Keep it alive while any plugin can
log, and let its creator destroy it. `logger::init(nullptr)` uses the logger
singleton's lazy creation path; repeated calls keep the current instance.

For `singleton<T>`, `set_instance(pointer)` only changes the active pointer and
never takes ownership. A lazy-created instance remains owned until explicit
`shutdown()`, even while a borrowed pointer temporarily replaces it. Restore the
saved pointer or detach it with `set_instance(nullptr)` before a borrowed object
is destroyed. `shutdown()` clears the active pointer and destroys only the
internally owned object, once. It also prevents automatic creation, including
when called before the first `instance()`. Later `instance()` returns null unless
an object is explicitly injected; `get_instance()` requires a valid active
instance. `set_instance(nullptr)` detaches without stopping lazy initialization
that has not yet run. Constructor exceptions still propagate and allow another
initialization attempt. Allocation failure retains the existing null-return
behavior. Concurrent lazy first access is synchronized; injection and shutdown
require external serialization against access. There is no automatic process-exit
cleanup. Do not delete the internally owned object yourself or restore its pointer after shutdown.

With `ARK_BUILD_CORE_TESTS=ON`, `check-core` includes `check-singleton`, which runs
the ownership, injection, construction-retry, concurrent initialization, shutdown
and logger cases in a separate process. A 30-second process guard reports failure
with flushed case/last-completed-assertion diagnostics; the exception case also
identifies each constructor call before entering it. An isolated 5-second
`--probe-call-once` process first records the standard-library exception-retry
control, including timeout or failure; that diagnostic never substitutes for
the strict singleton suite.
