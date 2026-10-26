# Project layout and public plugin interfaces

English | [简体中文](project-layout_CN.md)

ark uses C++20. Core headers and official plugins live in this repository;
application-specific business plugins live with their application. The network
library remains in the independent [arknet repository](https://github.com/OpenArkStudio/arknet).

## Directories

Build artifacts belong to each CMake build directory: `cmake -S . -B out/release`
produces executables, plugins and archives under `out/release/bin`. Use another
directory for Debug or sanitizer builds to keep their artifacts separate. A build
with `-B build` continues to use `build/bin`. Deployment copies the selected
build's artifacts into the runtime's `bin/` directory.

| Path | Contents |
| --- | --- |
| `src/core/` | Core headers, implementation and CMake usage requirements |
| `src/application/` | Application entry point and shutdown request |
| `src/proto/` | Protocol schemas and codec implementation |
| `src/server/` | Built-in server applications |
| `src/plugins/<name>/` | All official plugin headers, implementation and assembly |
| `tests/` | Core, network and private database regressions |
| `tests/network/` | Network and logging regressions |
| `tests/consumer/` | An independent project consuming the public plugin interfaces |
| `examples/` | Application examples |
| `config/` | Application manifest, plugin and logger configuration samples |
| `resources/` | Business data, workbooks and legacy resources |
| `tools/` | Offline configuration tools and retained legacy tools |
| `deployment/scripts/` | Single-application launch and explicit-PID stop helpers |
| `deployment/` | Container recipes and Kubernetes examples |

Include public headers as `<ark/plugin_manager.hpp>` or `<ark/net/tcp_client.hpp>`.
Do not add implementation directories to an application's include path. Generated
Protobuf headers are placed under the build directory, exposed through `ark::proto`.

Core has one source copy under `src/core/`. `ark::core` maps its explicit public headers into the selected build tree’s `src/core/public-include/ark/`, preserving `<ark/plugin_manager.hpp>` and nested include paths. Edit source headers rather than generated copies. The `app`/`app_d` executable and manifest format are unchanged.

## CMake targets

| Target | Usage |
| --- | --- |
| `ark::core` | Core headers, C++20 requirements and shared compile definitions |
| `ark::net`, `ark::kernel`, `ark::utility`, etc. | Headers and transitive public API dependencies of a plugin |
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

See [Code style](code-style.md) for the complete naming conventions, local checks
and protocol migration details.

## Verify public-header consumption

Install the dependencies described in
[Network builds and regression tests](networking.md), then build the independent consumer:

```sh
cmake -S tests/consumer -B sdk-build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DASIO_STANDALONE=OFF
cmake --build sdk-build --target check-sdk
```

The consumer builds with applications and Protobuf disabled. It verifies public
include paths, inherited C++20 settings, module overrides and exported GUID configuration.
Use `-DARK_USE_GUID_32=ON` to check the 32-bit GUID configuration.

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

Kernel co-locates its 38 existing headers with its cpp files in `src/plugins/kernel/`. `ark::kernel` preserves every `<ark/kernel/...>` path, including `data_impl.hpp`, `data_list_impl.hpp` and `kernel_plugin.hpp`. Typed construction retains the public types and registration keys; rebuild Kernel and consumers together as described in the [construction migration](reference/kernel.md#typed-kernel-construction). See [Kernel reference](reference/kernel.md) for consumption checks and Protobuf requirements.

Consul, Etcd Client, Kafka, OSS SDK and Physic also co-locate their 22 existing headers in `src/plugins/<name>/`. Explicit mappings preserve every previous `<ark/consul/...>`, `<ark/etcd_client/...>`, `<ark/kafka/...>`, `<ark/osssdk/...>` and `<ark/physic/...>` path, including implementation and assembly headers. Consume the matching `ark::<name>` API target; mapping remains available with runtime plugins disabled. CMake no longer discovers plugin headers under `include/ark/`.

`node --test tests/ci/plugin-layout.test.mjs` checks configure-only mapping with every runtime plugin option off, byte equality of mapped copies, API dependencies and stale-mapping cleanup. It does not compile optional driver integrations. The Etcd Client consumer compiles its five retained headers without Protobuf or runtime plugins:

```sh
cmake -S tests/consumer/etcd_client -B etcd-client-headers -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DASIO_STANDALONE=OFF
cmake --build etcd-client-headers --target check-etcd-client-headers --parallel 2
```

Header mapping does not establish Consul, Kafka or OSS SDK runtime compatibility. Physic and `physics_scene` still require the unsupported legacy PhysX 3 integration and retain their explicit configuration failure; no passing PhysX build is claimed.
