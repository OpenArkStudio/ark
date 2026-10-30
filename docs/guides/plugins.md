# Create a plugin

A plugin assembles modules and exports the shared-library entries called by the launcher. Official plugins belong in ark; business plugins belong with their application. Co-locate the [module example’s](modules.md#define-the-interface-and-implementation) `sample_module.hpp`, `sample_factory.hpp`, `sample_module.cpp` with this page’s `sample_plugin.cpp` and `CMakeLists.txt`.

## Declare the plugin

Keep the concrete `sample_plugin` in cpp below; no public plugin header is needed. `plugin_base<derived_type>` supplies version `0`, the `typeid(derived_type).name()` identity, manager access and `get_plugin_conf()` / `set_plugin_conf()`. Configuration is shared by instances of one derived type and isolated between types. Set the manager before installation; do not change it while modules are registered.

## Pair registration and unregistration

Save as `sample_plugin.cpp`. Factories return exclusively owned interfaces; installation selects updates explicitly and uninstallation removes the consumer before its provider:

```cpp
#include <stdexcept>

#include <ark/plugin_base.hpp>

#include "sample_factory.hpp"

namespace ark {
namespace {

class sample_plugin final : public plugin_base<sample_plugin>
{
public:
    void install() override {
        if (!register_module<counter_module>(make_counter(), module_update::disabled))
        {
            throw std::runtime_error("counter registration rejected");
        }
        if (!register_module<sample_module>(make_sample(), module_update::enabled))
        {
            throw std::runtime_error("sample registration rejected");
        }
    }

    void uninstall() override {
        if (!unregister_module<sample_module>())
        {
            throw std::runtime_error("sample unregistration rejected");
        }
        if (!unregister_module<counter_module>())
        {
            throw std::runtime_error("counter unregistration rejected");
        }
    }
};

} // namespace

ARK_EXPORT_FUNC void DllEntryPlugin(plugin_manager* manager,
    const std::string& stem, logger* log) {
    logger::init(log);
    manager->register_plugin<sample_plugin>(stem);
}

ARK_EXPORT_FUNC void DllExitPlugin(plugin_manager* manager) {
    manager->unregister_plugin<sample_plugin>();
}

} // namespace ark
```

| Operation | Contract |
| --- | --- |
| `register_module<interface_type>(unique_ptr, module_update)` | Consumes the exclusive candidate, preserves its interface address and registers `typeid(interface_type).name()`; omitting the template argument infers the interface from the factory return type |
| `module_update::enabled` / `disabled` | Mandatory choice; `disabled` excludes even an `update()` override. No default or override detection |
| `unregister_module<interface_type>()` | Pair every successful registration; success removes records and deletes the object once. Implement `uninstall()` in required module order; plugin destruction alone does not uninstall modules |

<Aside type="note" title="One entry-point pair per library">

Define `DllEntryPlugin` / `DllExitPlugin` in exactly one translation unit per library, using the necessary `ARK_EXPORT_FUNC` platform visibility macro. Duplicate entries cause symbol conflicts. Entries still pass C++ types: no pure C, cross-compiler or cross-version ABI is promised. The local removal order does not establish cross-plugin teardown order.

</Aside>

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

add_library(sample_plugin SHARED sample_plugin.cpp sample_module.cpp)
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
| Entry point cannot be found | Explicit entry functions are defined once in the library and exported symbols are visible |
| Module lookup asserts, or returns `nullptr` with assertions disabled | The runtime manifest includes the provider library, `install()` registers `T`, and consumer/provider use compatible interface definitions |
| Includes compile but no service is available | An interface target supplies headers; it does not install a runtime module |

The manager does not make plugin unloading safe for arbitrary outstanding callbacks. Finish the service-specific cancellation and lifetime work described in [Create a module](modules.md). This example has no worker tasks, event subscriptions or hot-update support.

## Use typed registration and explicit entries

The [complete example above](#pair-registration-and-unregistration) is recommended. Existing macros/plugins remain supported. Preserve interface keys, update selection, lifecycle, drain and unload contracts during migration:

| Legacy spelling | Recommended replacement |
| --- | --- |
| `ARK_DECLARE_MODULE_FUNCTIONS` | Derive from `module_base<interface_type, providers...>` in cpp; publish only the interface |
| `ARK_DECLARE_PLUGIN` | Derive from `plugin_base<derived_type>` in cpp; retain manager/configuration semantics |
| `ARK_REGISTER_MODULE` / `ARK_UNREGISTER_MODULE` | Private factory returning `std::unique_ptr<interface_type>`; replace legacy override-based update detection with an explicit switch, and pair removal in reverse order |
| `ARK_DECLARE_PLUGIN_DLL_FUNCTION` | Explicit entry/exit functions, retaining `ARK_EXPORT_FUNC` |

### Registration failures and resource retention

| Boundary | Behavior and caller responsibility |
| --- | --- |
| Candidate ownership | Object is unpublished and exclusively owned by `unique_ptr`; remove legacy borrowed observers before transfer |
| Rejected registration | Null candidate, duplicate module identity, occupied update identity or registered address returns false; destroys the candidate once, leaving no borrowed pointer |
| Registration exception | Metadata/allocation exceptions and owner-thread, callback-phase and shutdown guards propagate; no undo of earlier successful registration or cleanup hooks |
| Rejected removal | Missing owned identity or registry mismatch returns false; active callbacks/pending managed work invoke existing guards and retain the module and records |
| Later installation failure | Earlier registered resources remain under startup-failure process exit, without same-process rollback/retry; the [launcher](applications.md#check-startup-and-stop-the-process) reports failure and exits `1` |
| Plugin allocation/metadata | `register_plugin<T>()` retains `ARK_NEW` / `ARK_DELETE`, including nullable nothrow allocation. Null skips installation, records `allocate_plugin: <library stem>` and makes `start()` return false before later libraries/module phases. Metadata exceptions before installation destroy the candidate once; after installation starts, process-exit policy applies |
| Library wrappers | Scoped until published to the manager; failed native loads retire the wrapper even if logging throws. Loaded handles retain explicit unload policy; no caller migration required |

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

| Operation | Ownership and lifetime |
| --- | --- |
| `logger::init(log)` | Borrows the host logger; its creator keeps it alive while plugins can log and destroys it |
| `logger::init(nullptr)` | Lazy singleton creation; repeated calls retain the current logger |
| `singleton<T>::set_instance(pointer)` | Changes only the active pointer, never ownership. Lazy-created objects remain owned until `shutdown()`, even during borrowed replacement |
| Borrowed replacement | Restore the saved pointer or detach with `set_instance(nullptr)` before destroying the borrowed object; never delete the internally owned object or restore it after shutdown |
| `shutdown()` | Clears active state, destroys only the internally owned object once and prevents lazy creation, even before first `instance()`. No automatic process-exit cleanup |
| Access after shutdown | `instance()` returns null unless explicitly injected; `get_instance()` requires a valid active instance |
| `set_instance(nullptr)` | Detaches without stopping lazy initialization that has not run |
| Failure / concurrency | Constructor exceptions propagate and permit retry; allocation failure retains null-return behavior. Lazy first access is synchronized; externally serialize injection/shutdown against access |

With `ARK_BUILD_CORE_TESTS=ON`, `check-core` includes `check-singleton`, which runs
the ownership, injection, construction-retry, concurrent initialization, shutdown
and logger cases in a separate process. A 30-second process guard reports failure
with flushed case/last-completed-assertion diagnostics; the exception case also
identifies each constructor call before entering it. An isolated 5-second
`--probe-call-once` process first records the standard-library exception-retry
control, including timeout or failure; that diagnostic never substitutes for
the strict singleton suite.
