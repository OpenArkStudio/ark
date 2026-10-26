# Project layout and public plugin interfaces

ark uses C++20. Core headers and official plugins live in this repository;
application-specific business plugins live with their application. The network
library remains in the independent [arknet repository](https://github.com/OpenArkStudio/arknet).

## Directories

Build artifacts belong to each CMake build directory: `cmake -S . -B out/release` produces executables, plugins and archives under `out/release/bin`. Use another directory for Debug or sanitizer builds to keep their artifacts separate. A build with `-B build` continues to use `build/bin`. Deployment copies the selected build's artifacts into the runtime's `bin/` directory.

| Path | Contents |
| --- | --- |
| `src/core/` | Core headers, implementation and CMake usage requirements |
| `src/application/` | Application entry point and shutdown request |
| `src/config/` | Application manifest declarations, shared compiled file/TOML reader and private diagnostics |
| `src/proto/` | Protocol schemas and CMake rules for generated Protobuf messages |
| `src/server/` | Built-in server applications |
| `src/plugins/<name>/` | All official plugin headers, implementation and assembly |
| `tests/` | Core, network and private database regressions |
| `tests/network/` | Network and logging regressions |
| `tests/consumer/` | An independent project consuming the public plugin interfaces |
| `examples/` | [Capability examples](../guides/examples.md); hpp/cpp files stay adjacent |
| `config/` | Application manifest, plugin and logger configuration samples |
| `resources/` | Business data, workbooks and legacy resources |
| `tools/` | Offline configuration tools and retained legacy tools |
| `deployment/scripts/` | Single-application launch and explicit-PID stop helpers |
| `deployment/` | Container recipes and Kubernetes examples |

Include public headers as `<ark/plugin_manager.hpp>` or `<ark/net/tcp_client.hpp>`.
Do not add implementation directories to an application's include path. Generated
Protobuf headers are placed under the build directory, exposed through `ark::proto`.

The MySQL/Redis interfaces have one source each: `src/plugins/mysql/mysql_module.hpp` and `src/plugins/redis/redis_module.hpp`. Utility public interfaces and retained scheduler helpers live in `src/plugins/utility/`; Net public interfaces, endpoint templates and transport helpers live in `src/plugins/net/`. Net excludes private plugin assembly and the concrete HTTP-client/service-manager definitions from its public mapping. Bus keeps its two public module headers in `src/plugins/bus/`; shared `<ark/bus.hpp>` core value types live in `src/core/bus.hpp`. HTTP maps its public `src/plugins/http/http_server_module.hpp`, excluding the private plugin and concrete class. At configuration time CMake copies ordinary files from explicit lists into each build tree's generated plugin header mapping, removing obsolete mappings only within those controlled directories. MySQL, Redis, Utility, Net, Bus and HTTP keep their private assembly and factories outside the public mapping. Kernel retains all 38 existing headers, including `kernel_plugin.hpp`, without changing concrete-type or assembly visibility. Consume `ark::mysql`, `ark::redis`, `ark::utility`, `ark::net`, `ark::bus`, `ark::http` and `ark::kernel` to retain the logical include paths, including when runtime plugins are disabled. Commands using only handwritten `-I <source>/include` must adopt the target-provided include roots. The mapped copies are generated build artifacts; edit the co-located source headers.

Core has one source copy under `src/core/`. `ark::core` maps its explicit public headers into the selected build tree, preserving `<ark/plugin_manager.hpp>` and nested include paths. Edit source headers rather than generated copies. The `app`/`app_d` executable and manifest format are unchanged.

## CMake targets

| Target | Usage |
| --- | --- |
| `ark::core` | Core headers, C++20 requirements and shared compile definitions |
| `ark::net`, `ark::kernel`, `ark::utility`, etc. | Headers and transitive public API dependencies of a plugin |
| `ark::config_headers` | Application manifest declarations, available with the compiled reader disabled |
| `ark::config` | Shared compiled configuration reader; no networking linkage |
| `ark::proto` | Generated protocol messages and their compiled library; requires `ARK_BUILD_PROTO=ON` |
| `ark::ark` | Aggregate public interface target for applications using multiple official plugins |

The core and plugin API targets carry CMake usage requirements. They do not load
runtime plugins. Shared plugin libraries are built with `ARK_BUILD_APPLICATIONS=ON`
and selected with `ARK_BUILD_PLUGIN_<NAME>`. Existing shared-library filenames and
plugin configuration names are retained; applications must be rebuilt for the new C++ API.

```cmake
add_subdirectory(path/to/ark ark-build)
add_executable(my_service main.cpp)
target_link_libraries(my_service PRIVATE ark::core ark::net)
```

The default runtime plugin selection is `kernel`, `net`, `bus`, `utility` and
`etcd_client`. Other existing plugins are opt-in and require their driver dependencies;
the directory migration alone does not validate those integrations.

## Naming and formatting

- Namespaces, types, functions and filenames use `snake_case`: `ark::plugin_manager`,
  `find_module()`, `plugin_manager.hpp`.
- Private and protected members end in `_`, for example `module_name_`.
- Include system headers first, followed by third-party headers, then ark headers.
- Use clang-format 18 and the checked-in `.clang-format`. Third-party and generated
  files are excluded from project formatting.

See [Code style](../project/code-style.md) for complete naming conventions, local checks and protocol migration details.

## Verify public-header consumption

Install the dependencies described in
[Network builds and regression tests](../networking.md), then build the independent consumer:

```sh
cmake -S tests/consumer -B sdk-build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DASIO_STANDALONE=OFF -DARKNET_ENABLE_SSL=ON \
  -DVCPKG_MANIFEST_FEATURES=database
cmake --build sdk-build --target check-sdk
```

The consumer builds with applications and Protobuf disabled. It verifies public
include paths, inherited C++20 settings, module overrides and exported GUID configuration.
Use `-DARK_USE_GUID_32=ON` to check the 32-bit GUID configuration.

The consumer also checks MySQL/Redis/Utility public-header mapping and rejects visible implementation or private assembly headers. The command enables SSL and the vcpkg `database` feature's OpenSSL dependency to check both interfaces; with SSL disabled, only Redis is compiled. This checks public-header consumption from the selected build tree.

The Net-only consumer enables Protobuf while keeping runtime plugins disabled.
It compiles all 18 Net public headers independently and excludes the private
manager/plugin headers:

```sh
cmake -S tests/consumer/net -B net-sdk-build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_MANIFEST_FEATURES=application -DASIO_STANDALONE=OFF
cmake --build net-sdk-build --target check-sdk --parallel 2
```

The Bus-only consumer also enables Protobuf and compiles the two mapped public
module headers independently, without exposing private implementation, assembly
or parser headers. Shared `<ark/bus.hpp>` core value types keep their source path.

```sh
cmake -S tests/consumer/bus -B bus-sdk-build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_MANIFEST_FEATURES=application -DASIO_STANDALONE=OFF
cmake --build bus-sdk-build --target check-sdk --parallel 2
```

The HTTP-only consumer needs no Protobuf or runtime plugin. It compiles the mapped public module and router API while excluding private assembly/parser headers.

```sh
cmake -S tests/consumer/http -B http-sdk-build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DASIO_STANDALONE=OFF
cmake --build http-sdk-build --target check-sdk --parallel 2
```

## Runtime resources and generated data

Source assets and staged runtimes have separate layouts. `build/` is only an optional CMake build directory; it no longer contains tracked runtime files.

| Source path | Purpose and runtime destination |
| --- | --- |
| `config/` | Configuration samples; copy the selected files into the runtime's `conf/` |
| `resources/excel/` | Workbooks edited offline |
| `resources/meta/`, `resources/server/` | Generated metadata and business data; stage under the runtime's `resource/` |
| `resources/client/`, `resources/mysql/` | Client-output placeholders and database schema resources |
| `resources/code/` | Generated metadata source before copying into the public headers |
| `tools/` | Configuration exporter and retained legacy tools; see the tools guide for status |
| `deployment/scripts/` | Launch/stop helpers; copy into the runtime or pass its directory explicitly |

Runtime packages still use `bin/`, `conf/` and optional `resource/`; logging configuration writes to the runtime's `binlog/`. Source moves do not change business XML, resource formats or runtime path resolution. See [helper migration](../guides/applications.md#migrate-old-helper-commands).

The historical `bin/`, `frame/` and `dep/` tree is not the current source layout. See [configuration tools](../guides/tools.md) for generation and [application launcher](../guides/applications.md) for process startup. Public protocol headers and compatibility boundaries are documented in [Protobuf messages](protobuf.md).

Kernel co-locates its 38 existing headers with its cpp files in `src/plugins/kernel/`. `ark::kernel` preserves every `<ark/kernel/...>` path, including `data_impl.hpp`, `data_list_impl.hpp` and `kernel_plugin.hpp`. The retained public implementation/plugin types use typed construction; rebuild affected consumers together as described in the [Kernel construction migration](kernel.md#typed-kernel-construction). Registration keys, update selection and lifecycle order are preserved. See [Kernel reference](kernel.md) for consumption checks and Protobuf requirements.

Consul, Etcd Client, Kafka, OSS SDK and Physic also co-locate their 22 existing headers in `src/plugins/<name>/`. Explicit mappings preserve every previous `<ark/consul/...>`, `<ark/etcd_client/...>`, `<ark/kafka/...>`, `<ark/osssdk/...>` and `<ark/physic/...>` path, including implementation and assembly headers. Consume the matching `ark::<name>` API target; mapping remains available with runtime plugins disabled. CMake no longer discovers plugin headers under `include/ark/`.

`node --test tests/ci/plugin-layout.test.mjs` checks configure-only mapping with every runtime plugin option off, byte equality of mapped copies, API dependencies and stale-mapping cleanup. It does not compile optional driver integrations. The Etcd Client consumer compiles its five retained headers without Protobuf or runtime plugins:

```sh
cmake -S tests/consumer/etcd_client -B etcd-client-headers -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DASIO_STANDALONE=OFF
cmake --build etcd-client-headers --target check-etcd-client-headers --parallel 2
```

Header mapping does not establish Consul, Kafka or OSS SDK runtime compatibility. Physic and `physics_scene` still require the unsupported legacy PhysX 3 integration and retain their explicit configuration failure; no passing PhysX build is claimed.


## Built-in server source layout

The eight server source families co-locate their interface, implementation and plugin hpp/cpp files in `src/server/<role>/`. Each implementation derives from `module_base<interface_type>`; private factories supply interface-owned `std::unique_ptr` values to `plugin_base`. The nine existing modules retain interface RTTI registration names and disabled frame updates. Each library defines its `DllEntryPlugin` and `DllExitPlugin` functions explicitly.

Server source includes such as `login/interface/login_net_module.hpp` and `login/include/login_plugin.hpp` remain available through build-tree forwarding headers under `src/server/compat-include/`. Internal source uses adjacent headers. The forwarding inventory preserves all 28 previous headers, including implementation and plugin declarations, without adding the private factory headers to that inventory. This compatibility mapping is separate from public `<ark/...>` mappings and is not a new installed server API. Rebuild C++ consumers after changing the concrete inheritance chain.

With `ARK_BUILD_APPLICATIONS=ON`, CMake builds seven server libraries: Game, Login, Proxy, World, Master, Dir and Router. The Simulate sources use the same construction pattern, but their target remains disabled and its explicit unsupported PhysX 3/MessagePack configuration failure is retained. Source colocation does not validate that integration. See [server architecture](../concepts/server-architecture.md#verify-server-construction) for the focused construction checks and their limits.

## Installed layout

[Installation](../get-started/installation.md#install-and-consume-the-cmake-package) preserves logical `<ark/...>` headers under `<prefix>/include/ark/`. CMake package files use the GNUInstallDirs library directory under `cmake/ark/`; selected launcher and runtime plugins install under `bin/`. Configuration samples and licensing are under `share/ark/`. Consumers use `find_package(ark CONFIG REQUIRED)` and `ark::core` / `ark::<plugin>` targets; source consumers use `add_subdirectory` with the same target names. Neither requires a generated mapping path. Source/build directories are absent from installed exports; arknet and other dependencies remain external. `tests/install/` contains independent installed consumers and the disposable-producer relocation test.

## Typed Consul construction

**Breaking ABI/layout change:** `consul_module_impl` now inherits `module_base<consul_module>` and `consul_plugin` inherits `plugin_base<consul_plugin>`. Rebuild Consul and consumers together. All three public headers, their `<ark/consul/...>` paths, public types and request methods remain available through `ark::consul`; the interface itself is unchanged. The C++ plugin boundary does not promise compiler-independent ABI compatibility.

The private `consul_factory.hpp` returns `std::unique_ptr<consul_module>` and is excluded from public mappings and installation. Explicit `DllEntryPlugin` and `DllExitPlugin` retain the existing platform exports and logger setup. Installation registers the single `consul_module` interface RTTI key with `module_update::disabled`; uninstallation removes and destroys that module. The implementation still inherits the default update method. Configuration storage, HTTP-client lookup, TOML loading, request serialization and endpoints are unchanged.

With `ARK_BUILD_CORE_TESTS=ON`, `ARK_BUILD_PROTO=ON` and `ARK_BUILD_CONFIG=ON`, `check-consul-construction` runs the actual factory/assembly doctest cases and a controlled-factory DSO test of the actual entry/exit source. These cover public bases, factory metadata, interface registration, duplicate rejection, disabled updates, configuration handoff, ownership and repeated stop. They require Protobuf, toml++ and the normal core/arknet dependencies, but no runtime plugin or Consul service. For an existing build configured with those options:

```sh
cmake --build build --target check-consul-construction --parallel 2
```

`ARK_BUILD_PLUGIN_CONSUL=ON` also requires `ARK_BUILD_APPLICATIONS=ON` and remains off by default. It links the generated protocol target `ark::proto` directly. With the core tests enabled, `check-consul-plugin` additionally inspects the actual Consul DSO exports in a separate process; `check-core` includes the applicable Consul checks. The controlled factory does not exercise the production HTTP client or a Consul server. These construction checks do not establish live Consul interoperability, protocol acceptance or arbitrary hot-update safety.
