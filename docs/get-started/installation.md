# Install and build

ark requires CMake 3.21+, Ninja, a C++20 compiler and the dependencies selected by its vcpkg manifest. Use Visual Studio 2022 Developer PowerShell on Windows, or a compatible Clang/GCC toolchain on macOS/Linux.

<Steps>
<Step title="Clone the repository">

```sh
git clone https://github.com/OpenArkStudio/ark.git
cd ark
```

</Step>
<Step title="Configure the application build">

Install vcpkg and set the `VCPKG_ROOT` environment variable to that checkout. Select Boost for the network dependency.

The public ark targets enable MSVC's standard-conforming preprocessor
(`/Zc:preprocessor`). Custom targets inherit it through `ark::core`; when compiling
ark headers manually, enable this option together with C++20 mode.

<Tabs syncKey="build-platform">

<TabItem label="macOS/Linux">

Use a POSIX shell. The [macOS compiler and SDK example](#macos-compiler-and-sdk) below selects the Apple toolchain explicitly.

```sh
cmake -S . -B app-build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_MANIFEST_FEATURES="application;boost" \
  -DASIO_STANDALONE=OFF -DCMAKE_BUILD_TYPE=Release
```

</TabItem>

<TabItem label="Windows PowerShell">

```powershell
cmake -S . -B app-build -G Ninja `
  "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  "-DVCPKG_MANIFEST_FEATURES=application;boost" `
  -DASIO_STANDALONE=OFF -DCMAKE_BUILD_TYPE=Release
```

</TabItem>

</Tabs>

</Step>
<Step title="Build">

```sh
cmake --build app-build --parallel 2
```

Multi-configuration generators also need `--config Release`. This command writes executables, plugins and static archives to `app-build/bin/`. Use a separate CMake directory for each Release, Debug or sanitizer build. The [quick start](quick-start.md) shows how to copy the selected binaries into a runtime directory. The [build options](../reference/build-options.md) control applications, examples and tests.

</Step>
</Steps>

For a smaller correctness build, follow [network regression tests](../networking.md#build-and-run-the-network-tests). After preparing a separate runtime, use the [launch helpers](../guides/applications.md#run-one-application) under `deployment/scripts/` with its path explicitly selected.

## macOS compiler and SDK

Select the C compiler, C++ compiler and SDK from the same active Xcode or Command Line Tools installation with `xcrun`. When changing the compiler or SDK, use a new build directory so CMake does not retain the previous selection. Run the following commands instead of the configure and build commands above:

```sh
ark_macos_sdk="$(xcrun --sdk macosx --show-sdk-path)"
cmake -S . -B app-build-macos -G Ninja \
  -DCMAKE_C_COMPILER="$(xcrun --sdk macosx --find clang)" \
  -DCMAKE_CXX_COMPILER="$(xcrun --sdk macosx --find clang++)" \
  -DCMAKE_OSX_SYSROOT="$ark_macos_sdk" \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_MANIFEST_FEATURES="application;boost" \
  -DASIO_STANDALONE=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build app-build-macos --parallel 2
```

Use `app-build-macos` in place of `app-build` in the subsequent runtime-copy paths.

<Aside type="caution" title="Homebrew LLVM 22.1.7 with macOS SDK 27.0">

This compiler/SDK combination has a standard-header compatibility issue that depends on include order and can report undeclared `INFINITY` or `DBL_EPSILON`. If these errors occur, use the Apple Clang and SDK selection above in a new build directory.

</Aside>

## Archived plugins

Consul, Etcd Client, Kafka and OSS SDK are [archived](../reference/directory-structure.md#archived-integrations); all four runtime plugins default to off. The installation above and default examples require none of their services. Source, public headers, CMake targets and explicit opt-in options remain available; installing public headers does not establish runtime functionality or external-service acceptance. Select the corresponding `ARK_BUILD_PLUGIN_<NAME>=ON` and supply the existing dependencies only when explicitly choosing a retained integration; Etcd Client also needs the vcpkg `etcd-client` feature.

Existing CMake caches retain earlier values, including Etcd Client's former `ON`. Use a fresh build directory or explicitly pass `-DARK_BUILD_PLUGIN_ETCD_CLIENT=OFF` to adopt the archived default selection. Existing compatibility checks explicitly enabled by CI remain in place.

## Upgrade arknet

The repository's `ARKNET_REVISION` default selects a fixed arknet commit when CMake fetches the library. Updating ark's source does not replace an `ARKNET_REVISION` value already saved in an existing `CMakeCache.txt`.

Use a new build directory and repeat the configure command above to take the repository default. To retain an existing build directory, replace `RELEASE_COMMIT` with the full 40-character commit of the selected [arknet release](https://github.com/OpenArkStudio/arknet/releases), then explicitly update the cached value:

```sh
cmake -S . -B app-build -DARKNET_REVISION=RELEASE_COMMIT
cmake --build app-build --parallel 2
```

The existing build directory retains its other configured options. Confirm `ARKNET_REVISION` in `app-build/CMakeCache.txt`; multi-configuration generators still require `--config Release` when building.

A nonempty `ARKNET_SOURCE_DIR` uses that local checkout instead of fetching `ARKNET_REVISION`. Update the checkout explicitly for development. To return to the pinned fetch, configure with `-DARKNET_SOURCE_DIR=` and the desired `-DARKNET_REVISION=RELEASE_COMMIT`. These remain optional CMake overrides; the CI policy compares the repository defaults, not a developer's cache or local checkout.

## Install and consume the CMake package

A top-level build enables `ARK_INSTALL` by default; an embedded `add_subdirectory` build defaults it to `OFF`. Build the selected outputs before installing:

```sh
cmake --build app-build --parallel 2
cmake --install app-build --prefix "$PWD/ark-prefix"
```

Multi-configuration generators also need `--config Release` for both commands. GNUInstallDirs selects `include/ark/`, the library directory (usually `lib/` or `lib64/`), `bin/` for the selected launcher/runtime plugins, and `share/ark/` for license/readme/configuration samples. Only explicitly mapped public headers are installed. Preserved Kernel and optional plugin implementation/assembly headers remain public; private factory/parser headers stay excluded. Disabled runtime plugins are not built or installed, although their public API targets remain available.

With Protobuf enabled, Windows also installs the shared `AFProto.dll` (`AFProto_d.dll` for Debug) in `bin/` and its import library in the library directory. Deploy this message DLL with the matching host/plugins and compatible Protobuf runtime configuration; rebuild consumers previously linked to static AFProto. `ark::proto` keeps the same target name and supplies generated-message import definitions. Non-Windows builds retain static AFProto. See [protocol linking and migration](../reference/protobuf.md#build-and-include).

arknet remains an independent dependency: install a compatible arknet 0.2.3 or later package separately, using the same Boost/Standalone Asio and SSL settings as the ark build. ark does not copy the fetched/local arknet source into its install tree. Other dependencies are supplied by the consumer's toolchain or `CMAKE_PREFIX_PATH`: spdlog 1.17+, readerwriterqueue and RapidXML for core; Protobuf when generated protocols were built. Header-only toml++ is compiled into the private configuration reader; a build using a compiled toml++ library discovers that package for installed reader consumers. Optional driver headers/libraries still require their integration's dependencies; installing headers does not validate those integrations. Retain dependency licenses when distributing their binaries.

To build against an already installed arknet instead of fetching/building its source, configure with `-DARK_USE_INSTALLED_ARKNET=ON`; this takes precedence over `ARKNET_SOURCE_DIR` and `ARKNET_REVISION`. Existing source builds remain supported.

An independent application's `CMakeLists.txt` uses the same aliases as source consumption:

```cmake
find_package(ark CONFIG REQUIRED)
add_executable(game main.cpp)
target_link_libraries(game PRIVATE ark::core ark::mysql)
```

```sh
cmake -S game -B game-build -DCMAKE_PREFIX_PATH="/path/to/ark-prefix;/path/to/dependencies"
cmake --build game-build
```

Include `<ark/plugin_manager.hpp>` or `<ark/mysql/mysql_module.hpp>`; do not add ark's source/build directories to include paths. For source consumption, use `add_subdirectory(path/to/ark ark-build)` before linking those same targets. Public plugin API targets do not load runtime libraries.

For a plugin-only producer, use `ARK_BUILD_APPLICATIONS=OFF` and `ARK_BUILD_RUNTIME_PLUGINS=ON`, then select `ARK_BUILD_PLUGIN_<NAME>`. This omits the launcher and all seven server libraries from both the build and root install. See the [MySQL/Redis build command](../reference/build-options.md#native-database-plugins). AFProto and the selected plugins retain their existing dependencies; runtime-only builds still require Boost, and MySQL requires SSL. Installation and CPack collect the targets actually built; the public API/header layout is unchanged. Building or installing DSOs does not verify a database service.

A configuration-only consumer can request `find_package(ark CONFIG REQUIRED COMPONENTS config)` and link `ark::config`. It does not discover core or arknet. `COMPONENTS config_headers` with `ark::config_headers` provides only `<ark/application_manifest.hpp>`, including builds with `ARK_BUILD_CONFIG=OFF`. A required unavailable component fails package discovery. Default discovery imports the core/public plugin targets and the compiled configuration reader/protocol target when built.

The prefix can be moved without changing the package files; dependency prefixes must still be discoverable and ABI-compatible. `cmake --build app-build --target check-install` stages and relocates an installation and checks independent installed consumers. The stronger isolated test builds in disposable copies, removes their source/build directories, then compiles and runs the relocated consumers:

```sh
cmake -DARK_SOURCE_DIR="$PWD" -DARK_TEST_WORK=/tmp/ark-install-check \
  '-DARK_TEST_PREFIX_PATH=/path/to/installed/dependencies' \
  -P tests/install/run_isolated.cmake
```

The work directory must not exist. Repeat with a new directory and `-DARK_TEST_BUILD_CONFIG=OFF` for declaration/core-only packaging. Add `-DARK_TEST_MYSQL=ON` with an SSL-enabled arknet dependency to compile the public MySQL interface; this does not run database services. Use `ARK_TEST_TOOLCHAIN` to pass a consumer/producer toolchain when required.

The HTTP consumer also has an opt-in installed-runtime entry:

```sh
cmake -S tests/consumer/http -B http-installed-build \
  -DARK_TEST_HTTP_INSTALLED=ON -DCMAKE_BUILD_TYPE=Debug \
  '-DCMAKE_PREFIX_PATH=/path/to/relocated-prefix;/path/to/dependencies' \
  -DARK_TEST_HTTP_PLUGIN_DIR=/path/to/relocated-prefix/bin
cmake --build http-installed-build --parallel 2 --target check-http-installed
```

| Entry | Coverage and prerequisites |
| --- | --- |
| `check-sdk` in `tests/consumer/http` | Existing build-tree public-header consumer; installed-runtime mode defaults to OFF. |
| `check-http-installed` | Native UNIX only; uses `find_package(ark)` and the installed HTTP DSO, checks its implementation path with `dladdr`, one HTTP 200 response, route-capture destruction, endpoint closure and repeated stop. A 15-second process deadline exits with code 124; the 3-second receive and 5-second cleanup observations remain separate. Requires loopback access and an ABI-compatible HTTP installation built with `ARK_BUILD_PLUGIN_HTTP=ON`; match the producer's Debug/Release configuration and dependency prefixes. Locally validated on macOS Debug; no Windows runtime entry. |
| `tests/install/check_install.cmake` with `ARK_TEST_HTTP=ON` | Adds the same HTTP consumer after staging and relocation. Set `ARK_TEST_CONFIG=Debug` or `Release`; `ARK_TEST_HTTP_BINDIR` defaults to `bin` and may name the producer's custom relative binary directory. Use an application build with the HTTP runtime enabled; the core-only `run_isolated.cmake` producer does not build that DSO. |

To check installed production MySQL and Redis plugins without connecting to a database, use the database entry after installing and moving the prefix:

```sh
cmake -S tests/consumer/database -B database-installed-build \
  -DCMAKE_BUILD_TYPE=Debug \
  '-DCMAKE_PREFIX_PATH=/path/to/relocated-prefix;/path/to/dependencies' \
  -DARK_TEST_DATABASE_PLUGIN_DIR=/path/to/relocated-prefix/bin
cmake --build database-installed-build --parallel 2 --target check-database-installed
```

| Entry | Scope and prerequisites |
| --- | --- |
| `check-database-installed` | Uses `find_package(ark)` with `ark::mysql` / `ark::redis`, public headers and the production `ark_mysql_plugin` / `ark_redis_plugin` DSOs. Checks module lookup, unconfigured initial lifecycle, resource stop, module removal, successful plugin unload and repeated stop. The CMake runner bounds the whole process to 15 seconds; the cooperative cleanup observation is 5 seconds. Native UNIX additionally checks each implementation path with `dladdr`; this probe is excluded elsewhere. Locally validated on macOS Debug only; Windows/Linux runtime acceptance and physical OS image removal are not established. |
| Installation | Enable `ARK_BUILD_APPLICATIONS`, `ARK_BUILD_PLUGIN_MYSQL` and `ARK_BUILD_PLUGIN_REDIS`, build and install the selected outputs, and use an SSL-enabled arknet dependency. Match the producer's Debug/Release configuration and ABI-compatible dependency prefixes. This check never configures a connection or issues a query/command; it does not replace real-service or protocol tests. |
| `tests/install/check_install.cmake` with `ARK_TEST_DATABASE=ON` | Adds the same consumer after installation, prefix relocation and exported-path auditing. Set `ARK_TEST_CONFIG=Debug` or `Release`; `ARK_TEST_DATABASE_BINDIR` defaults to `bin` and supports a custom relative binary directory. The core-only `run_isolated.cmake` producer does not build these runtime plugins. The existing `ARK_TEST_MYSQL` mode remains an interface compilation check. |

## Create an archive

After building, CPack creates standard archives from the install rules:

```sh
cpack --config app-build/CPackConfig.cmake -G TGZ -C Release
cpack --config app-build/CPackConfig.cmake -G ZIP -C Release
```

Archives are written under the build's `packages/` directory. They contain the selected install outputs and samples, not a source checkout, build cache or runtime business data. External runtime libraries, platform compatibility, application manifests, working directory and resources remain deployment responsibilities. The archive is not a dependency-free application installer; signing, system installers and release publication are separate operations.
