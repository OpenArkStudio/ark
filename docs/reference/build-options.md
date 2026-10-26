# Build options

| Option | Default | Purpose |
| --- | --- | --- |
| `ARK_BUILD_APPLICATIONS` | `ON` | Build applications and the selected runtime plugins |
| `ARK_BUILD_CONFIG` | `ON` for applications or core tests, otherwise `OFF` | Build the shared application/plugin configuration reader; network regressions also build it for HTTP |
| `ARK_BUILD_PROTO` | Value of `ARK_BUILD_APPLICATIONS` | Generate and build protocol messages; applications require it |
| `ARK_BUILD_NETWORK_TESTS` | `OFF` | Build focused C++ network/logging regressions |
| `ARK_BUILD_TESTS` | `ON` | Build legacy test programs |
| `ARK_BUILD_CORE_TESTS` | Value of `ARK_BUILD_TESTS` | Build C++20 core and optional Protobuf regressions |
| `ARK_BUILD_EXAMPLES` | `ON` | Build enabled examples |
| `ARK_BUILD_HTTP_EXAMPLE_TESTS` | `ON` for core tests with Boost and HTTP/Net/Etcd Client API targets, otherwise `OFF` | Compile and run actual HTTP example DSOs with isolated service doubles; explicit `OFF` is respected |
| `ARK_BUILD_PLUGIN_MYSQL` | `OFF` | Build the native MySQL coroutine plugin; requires `ARKNET_ENABLE_SSL=ON` |
| `ARK_BUILD_PLUGIN_REDIS` | `OFF` | Build the native Redis coroutine plugin; enable SSL for TLS pools |
| `ARK_BUILD_DATABASE_TESTS` | `OFF` | Build native database and public plugin regressions; requires an explicit `ARKNET_SOURCE_DIR` |
| `ARK_BUILD_BENCHMARKS` | `OFF` | Build standalone C++ benchmark programs |
| `ARK_USE_GUID_32` | `OFF` | Select the 32-bit GUID configuration; consumers inherit the setting through public interface targets |
| `ARKNET_SOURCE_DIR` | Empty | Use an independent local arknet checkout |
| `ARKNET_REVISION` | Repository-pinned commit | Select the fetched arknet revision; an existing CMake cache retains its value |
| `ASIO_STANDALONE` | `OFF` | `OFF` selects Boost; `ON` selects Standalone Asio |

The project requires C++20 through CMake and disables compiler extensions. Applications and network tests require Boost.Asio and Boost.Beast. Standalone Asio is available for core-only and native database development builds; explicitly disable applications and network tests when selecting it. CMake rejects unsupported combinations before fetching dependencies. A local arknet checkout is a development override, not a copy inside ark.

When upgrading arknet, use a new build directory or explicitly change the cached revision. A nonempty `ARKNET_SOURCE_DIR` takes precedence over fetching the pin; see [Upgrade arknet](../get-started/installation.md#upgrade-arknet).

Official plugin options use `ARK_BUILD_PLUGIN_<NAME>`. The default runtime selection is `kernel`, `net`, `bus`, `utility` and `etcd_client`; enable the optional MySQL/Redis targets to build native coroutine plugins. Protocol and plugin-load tests do not replace real-server deployment acceptance. See [project layout](directory-structure.md), [protocol generation](protobuf.md) and [network builds](../networking.md).

With `ARK_BUILD_EXAMPLES` enabled, the [coroutine task example](../guides/tasks.md) also builds with applications disabled. Enabled application plugin examples still require application builds. HTTP example construction tests can run separately with core tests; see [HTTP examples](http.md#legacy-http-example-construction).

See [MySQL](mysql.md) and [Redis](redis.md) for plugin configuration and coroutine APIs.

## Native database plugins

With the dependencies from the installation guide available, build both optional runtime plugins:

```sh
cmake -S . -B build -DARK_BUILD_APPLICATIONS=ON \
  -DARK_BUILD_PLUGIN_MYSQL=ON -DARK_BUILD_PLUGIN_REDIS=ON -DARKNET_ENABLE_SSL=ON
cmake --build build --target ark_mysql_plugin ark_redis_plugin
```

All official plugin public headers are copied from explicit lists at configuration time into each build tree's generated plugin header mapping. Each `ark::<name>` API target carries that include root even with runtime plugins disabled. MySQL, Redis, Utility, Net, Bus and HTTP exclude private assembly and factories. Kernel retains all 38 existing headers, including `kernel_plugin.hpp`; Consul, Etcd Client, Kafka, OSS SDK and Physic retain all 22 existing public headers, including implementation and assembly headers. Registration keys, public type visibility and optional plugin policies are retained; [Kernel typed construction](kernel.md#typed-kernel-construction), [Consul typed construction](directory-structure.md#typed-consul-construction), [Etcd Client typed construction](directory-structure.md#typed-etcd-client-construction) and [Kafka typed construction](directory-structure.md#typed-kafka-construction) change public class layouts and require rebuilding consumers. Their private factory headers are excluded from mapping and installation; header mapping does not validate optional driver integrations. Each public header has one co-located source; its generated include mapping belongs to the selected build tree, rather than a second source layout.

Applications CI explicitly enables Consul and Etcd Client on Linux, macOS and Windows. Its `check-core` run includes their construction, DLL lifecycle and production export checks; these checks require no Consul or Etcd service and do not establish service interoperability. Etcd Client keeps its existing default `ON` selection and can be disabled with `ARK_BUILD_PLUGIN_ETCD_CLIENT=OFF`. Building it requires the existing `nlohmann_json` package (`etcd-client` vcpkg feature); disabling it does not add that JSON dependency to core tests.

Link a consumer to `ark::mysql` and/or `ark::redis` for the public headers and usage requirements. These public interface targets do not load a runtime library. Deploy the selected libraries with the launcher and add their stems to the application's manifest:

```toml
[application]
plugin_library_dir = "bin/"
plugin_config_dir = "conf/"

[[plugins]]
name = "ark_mysql_plugin"
config = ""

[[plugins]]
name = "ark_redis_plugin"
config = ""
```

Include only the providers the application needs. The empty `config` is intentional: consumers configure named connections through the public modules, rather than legacy database XML files. Follow [runtime library selection](../guides/plugins.md#select-libraries-at-runtime) for platform extensions, Debug suffixes and compatible public interface settings. Resolve the module during consumer initialization and start its operations through a [managed task group](../guides/tasks.md).

Core headers live under `src/core/`; `ark::core` supplies public header usage requirements, retaining `<ark/...>`. The launcher source is `src/application/`, with unchanged `app` and `app_d` output names.

## Installation and archives

`ARK_INSTALL` defaults to `ON` for the top-level project and `OFF` for embedded builds. It enables explicit public-header installation, relocatable CMake exports, `check-install` and TGZ/ZIP CPack archives. `ARK_USE_INSTALLED_ARKNET=ON` resolves a separately installed arknet package and takes precedence over the source/revision overrides. It defaults to `OFF`, preserving source dependency builds. See [installation and package consumption](../get-started/installation.md#install-and-consume-the-cmake-package). Packaging does not enable optional runtime plugins or validate their external drivers.

Net's 18 public headers exclude `http_client_module_impl.hpp`; use `<ark/net/http_client_module.hpp>` and public interface lookup. This removal changes source/ABI compatibility; see [Net migration](network.md#api-and-private-construction). Private factory declarations are not installed.

Kafka remains off by default (`ARK_BUILD_PLUGIN_KAFKA=OFF`). With the existing driver dependencies, applications, Protobuf and core tests enabled, `ARK_BUILD_PLUGIN_KAFKA=ON` adds `check-kafka-construction` and `check-kafka-plugin` to `check-core`. The first exercises actual factory/assembly plus controlled DSO lifecycle and enabled updates; the second also checks production DSO exports. No broker is contacted. Disabling Kafka avoids Kafka dependency discovery in core tests. See [Kafka construction migration](directory-structure.md#typed-kafka-construction) for rebuild requirements and verification boundaries.
