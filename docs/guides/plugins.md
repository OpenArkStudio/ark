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

Existing macros and plugins remain supported. Export the entry functions
explicitly with the existing platform visibility macro. The plugin boundary
still passes C++ types; this API does not promise a pure C ABI or cross-version
ABI stability. Lifecycle phases, draining and library unload rules are unchanged.

Net constructs its HTTP client through a private factory returning `std::unique_ptr<http_client_module>` and registers it with updates enabled. Applications consume `<ark/net/http_client_module.hpp>` and typed manager lookup; the old `http_client_module_impl.hpp` is removed from the public/install interfaces. Rebuild affected consumers together; see [Net migration](../reference/network.md#api-and-private-construction).

Kernel also uses typed construction while retaining its public `*_impl` and `kernel_plugin` types and header paths. Its private factory header is not installed. Rebuild Kernel and consumers together because those public class layouts change; the [Kernel migration table](../reference/kernel.md#typed-kernel-construction) records unchanged interface keys, update selection and registration order.

Consul follows the same construction pattern while retaining all three public headers and types. Its private factory is not installed; the module keeps its interface RTTI key and disabled updates. Rebuild Consul and consumers together because its public implementation/plugin layouts change. See [Consul construction migration](../project-layout.md#typed-consul-construction) for test entry points and dependency conditions.
