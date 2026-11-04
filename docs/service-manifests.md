# Application plugin manifests

An application manifest selects plugin libraries and their configuration files. Pass a TOML path ending in lowercase `.toml` through `-a` / `--app_conf`. XML manifests and other suffixes are rejected before plugin startup; invalid TOML never falls back to another format.

## TOML format

The shipped [timers.app.toml](../config/timers.app.toml) uses TOML 1.0 syntax:

```toml
[application]
plugin_library_dir = "bin/"
plugin_config_dir = "conf/"

[[plugins]]
name = "ark_utility_plugin"
config = ""

[[plugins]]
name = "timers_plugin"
```

| Field | Type and default | Meaning |
| --- | --- | --- |
| `application` | Required table | Application manifest settings. |
| `application.plugin_library_dir` | String, `""` | Prefix for plugin library names. |
| `application.plugin_config_dir` | String, `""` | Prefix for plugin configuration names. |
| `plugins` | Array of tables, empty | Omission and a top-level `plugins = []` both select no plugins. |
| `plugins[].name` | Required nonempty string | Unique library stem, such as `timers_plugin`; platform extensions and the Debug `_d` suffix remain automatic. |
| `plugins[].config` | String, `""` | Configuration filename; omission and `""` retain empty-configuration behavior. |

Unknown fields, incorrect types, duplicate or empty plugin names, invalid TOML/UTF-8 and embedded NUL characters are rejected before plugin startup. Diagnostics contain the path and available line/column, without printing configuration values.

Source positions use one-based lines and Unicode code point columns, including raw NUL diagnostics after UTF-8 text. A leading UTF-8 BOM does not advance the column. File/path errors without a source position return zero from `application_manifest_error::line()` and `column()`.

Directory strings are preserved exactly. Relative paths use the process working directory. The manager appends filenames by string concatenation, so directory prefixes normally need a trailing `/` or platform-appropriate separator. The loader does not add separators or require configuration files for empty `config` values.

The order of `[[plugins]]` entries does **not** define module initialization order: the manager still consumes an unordered map. This format introduces no dependency ordering or live reload mechanism.

## Run timers

Follow the [application build instructions](networking.md) to build `app`, `ark_utility_plugin` and `timers_plugin`. For a build directory named `out`, stage a separate runtime from the repository root:

```sh
mkdir -p run/timers/bin run/timers/conf
cp -R out/bin/. run/timers/bin/
cp config/timers.app.toml run/timers/conf/
cd run/timers
./bin/app --busid=1.0.0.1 --name=timers -a conf/timers.app.toml
```

Debug builds use `app_d`. Bus, HTTP and Consul runtime settings use TOML; the application manifest only selects their paths. See the [Bus settings](reference/bus.md#configure-processes-and-relationships) and the HTTP/Consul examples below. Application/plugin runtime settings and logging use TOML. Kernel interface/type definitions and business configuration use JSON; Kernel reads only `meta/entity_schema.json`.

## Embedded logging

Logging is optional. Without `[logging]`, the launcher uses a synchronous console logger at INFO with a timestamp and source location, and creates no log files. It never reads `log.conf`. Add settings to the same application file:

```toml
[logging]
console = true
file = true
level = "info"
pattern = "%^[%Y%m%d %H:%M:%S.%e][%-5l]%v%$"
output_path = "./binlog"
queue_count = 0

[logging.loggers.oss]
output_path = "./binlog/oss"
```

| Field | Default | Contract |
| --- | --- | --- |
| `console` / `file` | `true` / `false` | Boolean output switches; at least one must be enabled. File output is explicit and rotates into date directories and hourly filenames. |
| `level` | `"info"` | `trace`, `debug`, `info`, `warn`, `error`, `critical` or `off`. |
| `pattern` | Pattern shown above | Nonempty spdlog pattern without NUL. |
| `output_path` | `"./binlog"` | Nonempty UTF-8 path without NUL; relative to the process working directory. The app name and Bus ID form the filename. Parent directories are created for file output. |
| `queue_count` | `0` | Integer 0..65535; 0 is synchronous, positive values select a dedicated asynchronous queue. |
| `loggers` | Empty | Named tables inherit the base settings and override supplied fields. `oss` selects the OSS logger; without it OSS uses the default logger. Other names remain available through `create_logger()`. |

The same settings apply in Debug and Release. Keep logger settings in the application TOML or supply `logging_settings` programmatically. Plugin-specific runtime TOML and Kernel/business JSON remain separate.

Logger output paths and filenames use UTF-8 on every platform: `logger_settings::output_path` and the filename passed to `logger::init_logger(settings, filename)`. Framework file sinks convert these paths to the native filesystem representation at directory creation, file open and rotation; Windows uses UTF-16 paths. Chinese characters, spaces and non-BMP characters are supported without enabling spdlog filename macros. File contents retain the supplied UTF-8 bytes. Date directories, hourly filenames, size-rotation retention, log levels, synchronous/asynchronous queues and defaults keep their existing behavior.

**UTF-8 migration:** Windows callers that previously passed active-code-page/ANSI path bytes must convert them to UTF-8 before calling the logger. Keep existing UTF-8 configuration text as UTF-8; do not convert it to ANSI. The retained settings API uses string paths and filenames. Do not change `SPDLOG_WCHAR_FILENAMES` or other third-party ABI macros to perform this migration.

**Breaking migration:** standalone XML logger configuration is removed. `log_conf::load_log_conf()`, its XML-only `get_default()` accessor, and `logger::init_logger(conf_file, filename)` no longer exist; there is no compatibility overload. Move selected legacy XML `debug`/`release` values into the application TOML `[logging]` table or construct `ark::logging_settings` directly. `path` becomes `output_path`, numeric levels 0..6 become the named levels above, and `console` becomes a boolean. Set `file = true` to keep file output and `queue_count = 1024` to keep the old asynchronous queue. Remove `--log_conf` / `-l` from launch commands. Rebuild applications, plugins and consumers after this API removal.

Direct library callers can use the same manifest settings as the launcher:

```cpp
const auto manifest = ark::load_application_manifest("conf/service.app.toml");
ark::logger log;
log.init_logger(manifest.logging, "service.log");
```

Alternatively, pass an `ark::logging_settings` value to the same overload. File output follows `settings.file`, independently of `ARK_OPEN_FILE_LOG`; that build option only controls low-level `log_conf_data` defaults.

Logging schema errors include the application path and field and fail before plugin loading. File initialization failures also exit with code `1`. Asynchronous output retains its existing emergency-exit limits; use `queue_count = 0` when logs must complete inline. Existing startup-failure diagnostics still use their emergency stderr path.

## HTTP and Consul plugin settings

`ark_http_plugin` reads `http.plugin.toml`:

```toml
[http]
host = "0.0.0.0"
port = 9090
threads = 4
```

`host` is a nonempty address or hostname. `port` is an integer from 0 to 65535; zero requests an available port when the server starts. `threads` is an integer from 1 to 255, matching the existing server interface.

The HTTP plugin parses these fields during initialization; start its listener explicitly through the [public HTTP interface](reference/http.md). Route handlers execute on arknet IO workers, and owner-thread `pre_shut()` stops the listener.

`ark_consul_plugin` reads `consul.plugin.toml`:

```toml
[consul]
host = "127.0.0.1"
port = 8500
```

The Consul destination port must be from 1 to 65535. Unknown sections or keys, missing fields, incorrect types and out-of-range values fail plugin initialization. Errors identify the file and field; syntax errors also include a line and column. Configuration values are not printed. The parser does not resolve hosts or start a network connection.

To migrate existing installations, replace the HTTP/Consul XML files with these TOML settings and update each manifest's configuration filename. XML `ip` becomes `host`, and HTTP `thread` or `thread_num` becomes `threads`. Do not merely rename an XML file: these two plugins now require TOML content. Shipped application manifests and the deployment ConfigMap use the new filenames. This change does not add Consul as a mandatory dependency or change the paused service-discovery design.

## XML compatibility

XML application manifests are no longer supported. Convert the contents to the TOML tables above and save the file with a lowercase `.toml` suffix; renaming an XML file does not convert its contents. The reader rejects non-TOML paths before reading them, and manifest failures exit the application with status `1` before `plugin_manager::start()`.

## C++ and build integration

`src/config/` owns the application manifest declarations and the compiled configuration reader. `ark::config` is a position-independent static CMake target with a standard-library-only public header, so a shared plugin can link it. The application and the Bus/HTTP/Consul plugins share file reading, TOML syntax/encoding parsing and sanitized positional diagnostics. Application schema validation stays in `application_manifest.cpp`; each plugin retains its own field and range validation. MySQL and Redis retain their programmatic connection configuration and have no new file schema. The public `load_application_manifest()` reader requires a lowercase `.toml` suffix and parses only TOML. XML and other suffixes produce `application_manifest_error`; XML content inside a `.toml` file fails TOML parsing without fallback.

```cpp
#include <ark/application_manifest.hpp>

auto manifest = ark::load_application_manifest("conf/timers.app.toml");
```

```cmake
target_link_libraries(my_loader PRIVATE ark::config)
```

CMake maps the source `src/config/application_manifest.hpp` to the build tree's `src/config/public-include/ark/application_manifest.hpp`. Link `ark::config_headers` for declarations only; `ark::core` also carries this lightweight target even with `ARK_BUILD_CONFIG=OFF`. This mapping does not load parser or networking dependencies. Use the target-provided include roots instead of a handwritten `-I <source>/include`.

`ARK_BUILD_CONFIG` defaults to `ON` for application or core-test builds, otherwise `OFF`. Applications require it. Network regression builds also create `ark::config` for the HTTP implementation, including when applications and `ARK_BUILD_CONFIG` are off. The target privately uses [toml++](https://github.com/marzer/tomlplusplus) (MIT): CMake prefers an installed `tomlplusplus` 3.4+ package, otherwise fetches v3.4.0 at `30172438cee64926dc41fdd9c11fb3ba5b2ba9de`. The target does not propagate parser headers or macros to `ark::core` / `ark::ark` consumers.

Run `cmake --build out --target check-application-manifest check-plugin-configuration check-bus-configuration` for native regressions. `check-core` includes them when `ARK_BUILD_CONFIG=ON`.

The independent configuration consumer configures no arknet, core or runtime targets. It exercises application and plugin success/failure cases and reads both through a shared-library consumer:

```sh
cmake -S tests/consumer/config -B config-build -G Ninja
cmake --build config-build --target check-config
cmake -S tests/consumer/config -B config-headers-build -G Ninja -DARK_BUILD_CONFIG=OFF
cmake --build config-headers-build --target check-config-headers
```

The declaration-only build does not discover a parser. The compiled build privately uses the same toml++ dependency as `ark::config`. Neither configuration-only consumer requires network dependencies.

## Built-in application preset migration

The 24 built-in application presets use `config/<name>.app.toml`.
Restage the manifests and update launch arguments, mounted ConfigMap keys and
external scripts that reference `config/<name>.app.conf`.
`deployment/scripts/run.sh` and `run.bat` require `conf/<name>.app.toml`.
User-maintained XML manifests must be converted to TOML before passing their
new `.toml` paths to `--app_conf`. The runtime directory remains `conf/`.

| Legacy XML field | TOML field | Retained meaning |
| --- | --- | --- |
| `plugins/@plugin_lib_dir` | `application.plugin_library_dir` | Exact string; no separator is appended |
| `plugins/@plugin_conf_dir` | `application.plugin_config_dir` | Exact string, empty when absent |
| `plugin/@name` | `plugins[].name` | Exact library stem, including case and existing platform suffix rules |
| `plugin/@conf` | `plugins[].config` | Exact configuration filename, empty when absent |

Plugin declaration order is preserved in each file. The manager still uses an
unordered map; declaration order is not a module lifecycle-order guarantee.
The old `world` `plugins/@path` attribute and some manifests' `res` nodes were
ignored by the application reader. Migration assigns no new meaning to them:
`world` retains empty directory prefixes and empty per-plugin configurations.
Kernel reads only `meta/entity_schema.json` for its JSON interface/type definitions and business configuration; migrate former Kernel XML inputs to this JSON schema. Application/plugin runtime settings and logging use TOML.

Corresponding application keys in the ConfigMap use `.app.toml`, including the
three deployment-only `photon.*` manifests. Photon launch arguments select the
renamed keys; external business-plugin stems and plugin configuration names stay
unchanged. Freebird remains outside the supported built-in TOML presets: it
depends on the external `AFFreeBirdPlugin` and an absent `freebird.plugin.conf`.
Its former XML example is unsupported; this migration does not establish that
the external application works.
