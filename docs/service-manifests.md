# Application plugin manifests

An application manifest selects plugin libraries and their configuration files. Pass a `.toml` manifest or an existing `.app.conf` XML manifest through the `-a` / `--app_conf` argument. A TOML error never falls back to XML.

## TOML format

The shipped [timers.app.toml](../config/timers.app.toml) uses TOML 1.0 syntax. Its original XML manifest is retained in parser regression fixtures:

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

Debug builds use `app_d`. Bus, HTTP and Consul runtime settings use TOML; the application manifest only selects their paths. See the [Bus settings](reference/bus.md#configure-processes-and-relationships) and the HTTP/Consul examples below. Logging uses the same application manifest; Kernel and exported business data retain their XML formats.

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
| `output_path` | `"./binlog"` | Nonempty path without NUL; relative to the process working directory. The app name and Bus ID form the filename. Parent directories are created for file output. |
| `queue_count` | `0` | Integer 0..65535; 0 is synchronous, positive values select a dedicated asynchronous queue. |
| `loggers` | Empty | Named tables inherit the base settings and override supplied fields. `oss` selects the OSS logger; without it OSS uses the default logger. Other names remain available through `create_logger()`. |

The same settings apply in Debug and Release. XML application manifests may embed `<logging console="true" file="false" level="info" queue_count="0"><logger name="oss" level="warn"/></logging>` alongside `<plugins>`, with the same attribute names and defaults. XML booleans also accept `1` and `0`. Plugin-specific service and business configuration stays separate.

**Breaking migration:** remove `--log_conf` / `-l` from launch commands; explicitly supplying either now fails with exit code `1`. Move the selected legacy XML `debug`/`release` settings into `[logging]`: `path` becomes `output_path`, numeric levels 0..6 become the named levels above, and `console` becomes a boolean. Set `file = true` to keep file output and `queue_count = 1024` to keep the old asynchronous queue. File output in this new API follows the runtime setting, independently of `ARK_OPEN_FILE_LOG`; that build option still controls legacy library defaults. Rebuild applications, plugins and consumers because `application_manifest` has a new public layout. The direct-library XML `logger::init_logger(path, filename)` overload remains available; the launcher does not use it.

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

The existing wrapper and attributes remain valid:

```xml
<xml>
    <plugins plugin_lib_dir="bin/" plugin_conf_dir="conf/">
        <plugin name="ark_utility_plugin" conf=""/>
        <plugin name="timers_plugin"/>
    </plugins>
</xml>
```

The application loader requires a root containing a `plugins` child; a direct `<plugins>` root is not an application manifest. Missing directory attributes and `conf` remain empty strings, and unknown XML attributes remain ignored. XML duplicate/empty names and embedded NUL are now rejected during manifest loading instead of reaching plugin loading with ambiguous or truncated data. A manifest error exits the application with a nonzero status before `plugin_manager::start()`.

## C++ and build integration

`src/config/` owns the application manifest declarations and the compiled configuration reader. `ark::config` is a position-independent static CMake target with a standard-library-only public header, so a shared plugin can link it. The application and the Bus/HTTP/Consul plugins share file reading, TOML syntax/encoding parsing and sanitized positional diagnostics. Application schema validation stays in `application_manifest.cpp`; each plugin retains its own field and range validation. MySQL and Redis retain their programmatic connection configuration and have no new file schema. The public `load_application_manifest()` reader selects TOML for a `.toml` suffix and XML for other paths; the application's `-a` argument retains the manifest path restrictions described above.

```cpp
#include <ark/application_manifest.hpp>

auto manifest = ark::load_application_manifest("conf/timers.app.toml");
```

```cmake
target_link_libraries(my_loader PRIVATE ark::config)
```

CMake maps the source `src/config/application_manifest.hpp` to the build tree's `src/config/public-include/ark/application_manifest.hpp`. Link `ark::config_headers` for declarations only; `ark::core` also carries this lightweight target even with `ARK_BUILD_CONFIG=OFF`. This mapping does not load parser or networking dependencies. Use the target-provided include roots instead of a handwritten `-I <source>/include`.

`ARK_BUILD_CONFIG` defaults to `ON` for application or core-test builds, otherwise `OFF`. Applications require it. Network regression builds also create `ark::config` for the HTTP implementation, including when applications and `ARK_BUILD_CONFIG` are off. The target privately uses [toml++](https://github.com/marzer/tomlplusplus) (MIT): CMake prefers an installed `tomlplusplus` 3.4+ package, otherwise fetches v3.4.0 at `30172438cee64926dc41fdd9c11fb3ba5b2ba9de`. The target does not propagate parser headers or macros to `ark::core` / `ark::ark` consumers. RapidXML remains the XML dependency.

Run `cmake --build out --target check-application-manifest check-plugin-configuration check-bus-configuration` for native regressions. `check-core` includes them when `ARK_BUILD_CONFIG=ON`.

The independent configuration consumer configures no arknet, core or runtime targets. It exercises application and plugin success/failure cases and reads both through a shared-library consumer:

```sh
cmake -S tests/consumer/config -B config-build -G Ninja
cmake --build config-build --target check-config
cmake -S tests/consumer/config -B config-headers-build -G Ninja -DARK_BUILD_CONFIG=OFF
cmake --build config-headers-build --target check-config-headers
```

The declaration-only build does not discover toml++ or RapidXML. The compiled build requires the same parser and RapidXML dependencies as `ark::config`. Neither configuration-only consumer requires network dependencies.

## Built-in application preset migration

The 24 parseable built-in application presets move from
`config/<name>.app.conf` to `config/<name>.app.toml`. This is a filename
compatibility change: restage the manifests and update launch arguments, mounted
ConfigMap keys and external scripts. `deployment/scripts/run.sh` and `run.bat`
now require `conf/<name>.app.toml` without automatic XML fallback. To keep a
user-maintained XML manifest, pass its existing path directly to the launcher's
`--app_conf`. The runtime directory remains `conf/`.

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
Kernel/business data XML and unknown plugin-specific parsers keep their formats.

Corresponding application keys in the ConfigMap use `.app.toml`, including the
three deployment-only `photon.*` manifests. Photon launch arguments select the
renamed keys; external business-plugin stems and plugin configuration names stay
unchanged. Freebird is explicitly excluded: it depends on the external
`AFFreeBirdPlugin` and an absent `freebird.plugin.conf`, and its direct `plugins`
root is rejected by the current application reader. Its ConfigMap example also
has a mismatched closing tag. The legacy examples remain as they were; migration
does not establish that this external application works.
