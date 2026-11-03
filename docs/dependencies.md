# Dependencies

[简体中文](dependencies_CN.md)

The [vcpkg manifest](../vcpkg.json) pins one registry baseline. CMake consumes
installed packages through imported targets; there is no bundled `3rdparty/`
source tree or dependency submodule initialization.

| Build scope | vcpkg feature | Package and CMake target |
| --- | --- | --- |
| Core | always installed | `spdlog` → `spdlog::spdlog_header_only`; `readerwriterqueue` → `readerwriterqueue::readerwriterqueue`; `rapidxml` headers |
| Boost networking | `boost` (default) | `boost-asio`, `boost-beast` |
| Manifest reader | `config` (default) | `tomlplusplus` → `tomlplusplus::tomlplusplus` |
| Applications | `application` | `args` → `taywee::args`, Protobuf, zlib, Zstandard |
| Compression regressions | `compression-tests` | zlib, Zstandard, LZ4; `ARK_BUILD_COMPRESSION_TESTS=ON` |
| Archived etcd client plugin | `etcd-client` | `nlohmann-json` → `nlohmann_json::nlohmann_json` |
| Native database development | `database` | OpenSSL |

spdlog's imported target propagates its external fmt dependency and compile
definitions. Consumers of `ark::core` inherit both packages without manual include
paths or definitions. `args` and JSON are found only when the application or etcd
plugin is enabled, respectively; JSON is private to the etcd plugin.

Consul, Etcd Client, Kafka and OSS SDK are [archived](reference/directory-structure.md#archived-integrations), with runtime plugins off by default. The application build uses
`-DVCPKG_MANIFEST_FEATURES="application;boost"`; archived Etcd Client remains explicitly selectable with `-DARK_BUILD_PLUGIN_ETCD_CLIENT=ON` and the additional `etcd-client` feature.
Manifest features select packages; the corresponding `ARK_BUILD_*` options select
CMake targets. They do not automatically enable each other.

For a minimal core build without TOML configuration, disable default manifest
features with `-DVCPKG_MANIFEST_NO_DEFAULT_FEATURES=ON`, set
`-DARK_BUILD_CONFIG=OFF`, and select only the needed networking feature. For a
Standalone core build with configuration, explicitly select
`-DVCPKG_MANIFEST_FEATURES=config` alongside `-DASIO_STANDALONE=ON`.

The manifest reader first uses an installed toml++ 3.4+ package. For builds without
that package, its existing fallback remains pinned to commit
`30172438cee64926dc41fdd9c11fb3ba5b2ba9de`. The fallback is used only when the package
was not found; a vcpkg configuration build does not install a second copy.
arknet uses [0.2.3](https://github.com/OpenArkStudio/arknet/releases/tag/v0.2.3), pinned to its immutable release commit in the root CMake file and the database CI checkout. This version fixes RUDP send-queue state after allocation failure without changing public APIs; rebuild consumers to use the fix. Installed-package consumption requires a compatible arknet 0.2.3 or later.

For local development, supply `ARKNET_SOURCE_DIR`. Existing build directories retain their cached `ARKNET_REVISION`; follow the [installation instructions](get-started/installation.md#upgrade-arknet) to update it explicitly, or use a fresh build directory. Existing performance and database reports continue to describe their stated tested revisions.

## Windows Debug compression dependencies

The LZ4 finder accepts `lz4` and `lz4d` basenames and searches each configured
directory before moving to the next. This preserves the toolchain's configuration
preference: a Debug `debug/lib/lz4d.lib` takes precedence over a later release
`lib/lz4.lib`; Release still prefers its release directory. Zstandard discovery
and the compiler settings are unchanged.

If a Windows compression regression exits with `0xc0000135` before doctest starts,
inspect `LZ4_LIBRARY` in `CMakeCache.txt` and the selected import library's DLL
dependencies. A release LZ4 import library selected in Debug can require a DLL
outside the configured `debug/bin` runtime directory. After updating this finder,
clear the cached selection with `cmake -U LZ4_LIBRARY -S . -B <build-dir>` and repeat
the original configure options, or use a fresh build directory. The CI runtime
helper continues to export only the matching configuration directory; do not mix
release DLLs into the Debug runtime path. Finder fixtures verify search selection
without compiling fake libraries; Windows execution remains a separate check.

## Optional legacy integrations

The physic plugin and physics_scene depend on PhysX 3 and have no validated SDK
integration in this build. They fail with an explicit diagnostic if selected;
they do not silently substitute modern PhysX packages. The legacy simulate server
also lacks a validated PhysX 3/MessagePack integration and remains disabled.
The unused BeastHttp include directory has been removed from the disabled http_server;
this does not establish that example's runtime acceptance.

The old cpp-bredis submodule had no active references and has been removed. The
native Redis client comes from arknet. License notices from the removed bundled
sources are retained in [Third-party notices](reference/third-party-notices.md).
Installed packages also supply their copyright files.

See [installation](get-started/installation.md) for platform commands and
[network builds](networking.md) for regression targets.
