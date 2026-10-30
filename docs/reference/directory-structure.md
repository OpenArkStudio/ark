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

| Plugin source | Public mapping and boundary |
| --- | --- |
| `src/plugins/mysql/mysql_module.hpp`, `src/plugins/redis/redis_module.hpp` | One source per interface |
| `src/plugins/utility/` | Public interfaces and retained scheduler helpers |
| `src/plugins/net/` | Public interfaces, endpoint templates and transport helpers; excludes concrete HTTP-client/service-manager definitions |
| `src/plugins/bus/` | Two public module headers; shared `<ark/bus.hpp>` comes from `src/core/bus.hpp` |
| `src/plugins/http/http_server_module.hpp` | Excludes private plugin/concrete class |
| Kernel | Retains all 38 headers, including `kernel_plugin.hpp`, with concrete/assembly visibility unchanged |

Configure-time explicit inventories copy ordinary files and remove stale mappings only within controlled directories. MySQL, Redis, Utility, Net, Bus and HTTP exclude private assembly/factories. `ark::mysql`, `ark::redis`, `ark::utility`, `ark::net`, `ark::bus`, `ark::http`, `ark::kernel` preserve logical paths even with runtime plugins disabled. Handwritten `-I <source>/include` alone is insufficient; use target include roots and edit co-located source headers, not generated copies.

Core has one source copy under `src/core/`. `ark::core` maps its explicit public headers into the selected build tree, preserving `<ark/plugin_manager.hpp>` and nested include paths. Edit source headers rather than generated copies. The `app`/`app_d` executable and manifest format are unchanged.

`<ark/buffer.hpp>` retains its public path with move-only ownership; rebuild affected Core/Net consumers for the source/layout change. See [receive-buffer ownership](network.md#receive-buffer-ownership) for migration, allocation-hook compatibility and focused tests.

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
It compiles all 19 Net public headers independently and excludes the private
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

OSS SDK retains its three public headers and types through typed construction; rebuild the plugin and consumers together because public class layouts change. See [OSS SDK construction migration](directory-structure.md#typed-oss-sdk-construction) for optional build conditions and construction/export checks. The implementation remains a TODO scaffold.

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

## Typed Etcd Client construction

**Breaking ABI/layout change:** `etcd_client_module_impl` now inherits `module_base<etcd_client_module>` and `etcd_client_plugin` inherits `plugin_base<etcd_client_plugin>`. Rebuild Etcd Client and consumers together. All five public headers, their `<ark/etcd_client/...>` paths and public types remain available through `ark::etcd_client`; the `etcd_client_module` and `etcd_client` interfaces are unchanged. C++ plugin boundaries do not promise compiler-independent ABI compatibility.

The private `etcd_client_factory.hpp` returns `std::unique_ptr<etcd_client_module>` and is excluded from public mappings and installation. Explicit `DllEntryPlugin` / `DllExitPlugin` retain the existing platform exports and logger setup. Registration uses the original interface RTTI key and `module_update::disabled`; unregistration destroys the owned module. HTTP-client lookup, endpoint handling, timeout argument order, lease requests and callbacks are unchanged. The existing `keepalive` stub remains incomplete; this construction change adds no protocol or Registry capability.

Etcd Client remains selected by default for application builds, with `ARK_BUILD_PLUGIN_ETCD_CLIENT=OFF` available to disable it. Its existing `nlohmann_json` package is required only when building the plugin (`etcd-client` vcpkg feature). With `ARK_BUILD_APPLICATIONS=ON`, `ARK_BUILD_PLUGIN_ETCD_CLIENT=ON`, `ARK_BUILD_PROTO=ON` and `ARK_BUILD_CORE_TESTS=ON`, run:

```sh
cmake --build build --target check-etcd-client-plugin --parallel 1
```

This target includes `check-etcd-client-construction`: actual factory/assembly doctest cases, plus a controlled-factory DSO using the production entry/exit source to check configuration handoff, interface metadata, disabled updates, exactly-once ownership and repeated stop. A separate process loads the actual production plugin, checks its entry/exit exports and unloads it. `check-core` includes both checks when the runtime plugin is selected; disabled-plugin core builds do not acquire the JSON dependency. Applications CI explicitly enables the plugin and runs these checks through its existing `check-core` job.

These tests do not run an Etcd service or exercise production HTTP/Etcd interoperability. They establish construction and DSO boundaries, not a completed Registry or arbitrary hot-update safety.

## Typed OSS SDK construction

**Breaking ABI/layout change:** `oss_sdk_module_impl` now inherits `module_base<oss_sdk_module>` and `oss_sdk_plugin` inherits `plugin_base<oss_sdk_plugin>`. Rebuild OSS SDK and consumers together. All three public headers, their `<ark/osssdk/...>` paths and public types remain available through `ark::osssdk`; the `oss_sdk_module` interface is unchanged. The C++ plugin boundary does not promise compiler-independent ABI compatibility.

The private `oss_sdk_factory.hpp` returns `std::unique_ptr<oss_sdk_module>` and is excluded from public mappings and installation. Explicit `DllEntryPlugin` / `DllExitPlugin` retain platform exports and logger initialization. Installation retains the original `oss_sdk_module` interface RTTI key and disabled updates. Typed unregistration replaces the undefined `ARK_DEREGISTER_MODULE` invocation and destroys the owned module. Configuration storage and the inherited default update method are preserved. The implementation remains a TODO scaffold: no OSS client, request API or object-storage protocol is added.

With `ARK_BUILD_CORE_TESTS=ON`, `check-oss-sdk-construction` compiles the real factory and assembly into doctest cases and uses the production entry/exit source with a counted controlled factory in a DSO. Checks cover retained public bases/types, factory metadata, interface registration, duplicate rejection, disabled updates, configuration handoff, exactly-once destruction and repeated stop. This target requires only the normal core/arknet dependencies; it needs no runtime plugin, Protobuf, JSON package or object-storage service:

```sh
cmake --build build --target check-oss-sdk-construction --parallel 1
```

The runtime plugin remains off by default. Enable `ARK_BUILD_APPLICATIONS=ON`, `ARK_BUILD_PLUGIN_OSSSDK=ON`, `ARK_BUILD_PROTO=ON` and `ARK_BUILD_CORE_TESTS=ON` to additionally build the actual `ark_oss_sdk_plugin` DSO and inspect its entry/exit exports in a separate process:

```sh
cmake --build build --target check-oss-sdk-plugin --parallel 1
```

Application builds retain their existing Protobuf, configuration and Boost.Asio requirements. `check-core` includes the applicable OSS SDK construction/export checks. No external OSS SDK dependency is introduced. These tests establish construction and DSO boundaries only; they do not establish OSS client functionality, service interoperability or arbitrary hot-update safety.

## Typed Kafka construction

**Breaking ABI/layout change:** `kafka_module_impl` now inherits `module_base<kafka_module>` and `kafka_plugin` inherits `plugin_base<kafka_plugin>`. Rebuild Kafka and consumers together. All four public headers, their `<ark/kafka/...>` paths and public types remain available through `ark::kafka`. The module interface, producer/consumer wrappers and both `RdKafka::DeliveryReportCb` / `RdKafka::EventCb` bases are retained. C++ plugin boundaries do not promise compiler-independent ABI compatibility.

The private `kafka_factory.hpp` returns `std::unique_ptr<kafka_module>` and is excluded from public mappings and installation. Explicit `DllEntryPlugin` / `DllExitPlugin` retain platform exports and logger setup. Assembly retains the interface RTTI registration key and explicitly selects `module_update::enabled`, preserving periodic producer polling. Unregistration destroys the owned module. Producer storage and ownership, producer creation, delivery/event callbacks and message behavior are unchanged.

Kafka remains optional and **off by default**. Its existing dependency discovery and linking are unchanged: enable `ARK_BUILD_APPLICATIONS=ON`, `ARK_BUILD_PROTO=ON`, `ARK_BUILD_CORE_TESTS=ON` and `ARK_BUILD_PLUGIN_KAFKA=ON`, with the platform's Kafka driver dependencies available. On Unix this includes librdkafka C/C++, OpenSSL, Zstandard, LZ4 and zlib. With that build configured, run:

```sh
cmake --build build --target check-kafka-plugin --parallel 1
```

`check-kafka-plugin` includes `check-kafka-construction`: actual factory/assembly doctest cases and a controlled-factory DSO using the production entry/exit source. They check typed bases, interface metadata, duplicate registration, enabled updates, configuration handoff, exactly-once ownership and repeated stop. A separate process loads the actual production DSO, checks its entry/exit exports and unloads it. `check-core` includes these checks only when the Kafka runtime target is selected; disabled-plugin core builds do not discover Kafka dependencies.

Callback logs explicitly convert RdKafka error, severity and event-type enums to integers for current fmt compatibility, retaining their numeric log values. This does not change producer behavior, callback ordering or dependency discovery.

These checks create no producer and require no broker connection. They establish construction and DSO boundaries, not Kafka delivery, flush behavior, service interoperability or arbitrary hot-update safety. The optional driver build still requires independent platform acceptance.
