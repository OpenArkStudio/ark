# Network builds and regression tests

English | [简体中文](networking_CN.md)

ARK requires C++20 and uses [arknet](https://github.com/OpenArkStudio/arknet)
directly for TCP, WebSocket, HTTP and RDP. RDP uses arknet's RUDP endpoints with
the legacy ARQ wire format, MTU 1400 and FEC disabled. Application message headers
and Update-thread callback dispatch remain the same. No coroutine API migration
is included.

## Dependencies

- CMake 3.21+ and a C++20 compiler: GCC 12+, Clang with C++20 support, or VS 2022.
- Boost 1.90+ for applications and network tests, using Boost.Asio and Boost.Beast.
- Standalone Asio 1.38.0+ is optional for core-only and native database development builds.
- spdlog/fmt and readerwriterqueue from the pinned vcpkg baseline.
- Protobuf, Zstandard and zlib when building applications and plugins.

See [dependency selection](dependencies.md) for optional features and license notices.
The vcpkg manifest pins the package baseline. CMake retrieves arknet from its
pinned revision in the independent repository. To validate a development checkout,
set `ARKNET_SOURCE_DIR` to its absolute path.

Applications and the current network tests use HTTP/WebSocket, which require
Boost.Beast. CMake rejects `ASIO_STANDALONE=ON` with either
`ARK_BUILD_APPLICATIONS=ON` or `ARK_BUILD_NETWORK_TESTS=ON` before retrieving
dependencies. It does not silently change the selected Asio library.

Use Protobuf, Abseil and utf8_range from the same installation prefix. When
setting `Protobuf_DIR` manually, select matching `absl_DIR` and `utf8_range_DIR`.

## Build and run the network tests

```sh
cmake -S . -B network-build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DARK_BUILD_APPLICATIONS=OFF -DARK_BUILD_NETWORK_TESTS=ON \
  -DASIO_STANDALONE=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build network-build --parallel 2
cmake --build network-build --target check-network
```

Boost is the default dependency. The tests are permanent C++ doctest cases under
`tests/network/`; the binary can also run directly or select cases with
`--test-case=RUDP*`. Python and Ruby are not required.

Coverage includes framing, binary payloads, reconnection, limits and timeouts,
Update-thread dispatch, reentrant callbacks, HTTP status/headers/query data,
shutdown cancellation, owner release inside receive callbacks, malformed input and log formatting/directory creation/rotation.
CI checks Boost on Linux, macOS and Windows, plus memory/undefined-behavior and
thread sanitizers on Linux. Application jobs also run `check-core` with Protobuf
enabled, including kernel initialization-order regressions.

Local validation on 2026-10-15 used an Apple M4 Pro (14 CPU cores, 48 GiB RAM),
macOS 27.0.1 arm64 and Apple Clang 21.0.0. Local results:

| Configuration | Core cases / assertions | Network cases / assertions |
| --- | ---: | ---: |
| Release, Boost 1.90, 128-bit GUID, Protobuf, TLS enabled | 22 / 1,961 | 36 / 953 |
| Debug, Boost, 128-bit GUID, ASan/UBSan, network only | — | 36 / 953 |
| Release, Boost, ASan/UBSan, kernel initialization only | 1 / 176 | — |
| Debug, Standalone Asio 1.38.2, core only, Protobuf/TLS disabled | 14 / 1,734 | — |

Protobuf cases cover valid messages, truncated input, actor identity, binary messages containing NUL,
existing wire payloads, external JSON field names and the new public descriptor/enum names.

The core total includes four kernel/config initialization-order combinations
for both resource-relative paths and legacy paths with a leading separator.
The full Release application, default plugins and examples built. An isolated
process loaded Bus, Net, Utility and Kernel, entered its main loop and exited
normally on SIGINT. HTTP requests use the library-owned IO pool; pending futures
and update-thread callbacks are covered by the network suite.

macOS ASan ran without leak detection; Linux CI enables LeakSanitizer. These are
correctness checks, not capacity measurements. The TLS-enabled consumer build
has no ark-specific handshake or certificate test; arknet covers those separately.

See [core regressions](../tests/core/README.md) for independent builds and filtering.
When `ARK_BUILD_CORE_TESTS=ON`, `check-network` also runs the core cases.

## Build applications

Enable the `application` manifest feature to install Protobuf:

```sh
cmake -S . -B app-build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  "-DVCPKG_MANIFEST_FEATURES=application;boost;etcd-client" \
  -DASIO_STANDALONE=OFF
cmake --build app-build --parallel 2
cmake --build app-build --target check-core
```

## Standalone Asio builds

Standalone Asio remains available for core-only builds and optional native
database development tests. Disable applications and network tests explicitly:

```sh
cmake -S . -B core-build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_MANIFEST_NO_DEFAULT_FEATURES=ON -DVCPKG_MANIFEST_FEATURES=config \
  -DARK_BUILD_APPLICATIONS=OFF -DARK_BUILD_PROTO=OFF \
  -DARK_BUILD_NETWORK_TESTS=OFF -DARK_BUILD_CORE_TESTS=ON \
  -DASIO_STANDALONE=ON
cmake --build core-build --target check-core
```

## Deployment acceptance

Loopback regression is not a 100,000–200,000 PCU capacity result. Cross-machine
load, endurance and the application's complete business flows require separate
acceptance before production rollout. ARQ/FEC performance measurements are in
[arknet's reports](https://openarkstudio.github.io/arknet/performance/).
