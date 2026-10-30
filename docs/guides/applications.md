# Application launcher

The `app` executable selects a process identity, reads a TOML or XML application manifest and loads its plugin libraries. Start with the runnable [Timers quick start](../get-started/quick-start.md); this page explains configuration paths, library names and startup checks.

## Command-line arguments

| Argument | Required | Meaning |
| --- | --- | --- |
| `--busid` / `-b` | Yes | Logical process Bus address, such as `1.0.1.1`; this is not a socket bind address |
| `--name` / `-n` | Yes | Application name, also used in the log filename |
| `--app_conf` / `-a` | Yes | TOML application manifest with the lowercase `.toml` extension, or a legacy XML `*.app.conf` file |
| `--log_conf` / `-l` | No | Logger configuration; defaults to `./conf/log.conf` |
| `--help` / `-h` | No | Print the launcher's argument reference |

```sh
./bin/app --busid=1.0.1.1 --name=timers \
  --app_conf=conf/timers.app.toml --log_conf=conf/log.conf
```

`busid`, `name` and `app_conf` must be supplied explicitly. The [Bus reference](../reference/bus.md) describes the address fields. Use the executable's `--help` output for additional platform-specific flags.

## Resolve configuration paths

Relative paths resolve from the process working directory. Moving the executable or placing the manifest in another folder does not change that base directory.

![Configuration paths resolved from the working directory](../assets/launcher-paths.svg)

| Input | How it is selected | Example from the runtime directory |
| --- | --- | --- |
| Application manifest | `--app_conf` | `conf/timers.app.toml` |
| Logger settings | `--log_conf`, or its default | `conf/log.conf` |
| Plugin libraries | TOML `plugin_library_dir` or XML `plugin_lib_dir`, plus the library filename | `bin/timers_plugin.dylib` on macOS Release |
| Per-plugin settings | TOML `plugin_config_dir` + `config`, or XML `plugin_conf_dir` + `conf` | `conf/my-plugin.conf` when the filename is `my-plugin.conf` |
| Kernel metadata and configuration | The Kernel's `resource/` directory | `resource/meta/entity_class.config`, `resource/server/item.config` |

The launcher passes the per-plugin path to the plugin manager; each module decides how to read its own configuration. Kernel resource paths are separate from the application manifest and are needed only by applications that use those Kernel facilities. See [Data-driven configuration](../concepts/data-driven.md) and [Configuration tools](tools.md).

## Select plugins

Built-in application presets use TOML. The checked-in [timers.app.toml](https://github.com/OpenArkStudio/ark/blob/main/config/timers.app.toml) contains:

```toml
[application]
plugin_library_dir = "bin/"
plugin_config_dir = "conf/"

[[plugins]]
name = "ark_utility_plugin"

[[plugins]]
name = "timers_plugin"
```

Pass `--app_conf=conf/timers.app.toml` to select it. The application table is required; directory fields are optional strings. Each plugin requires a unique, nonempty `name`; its optional `config` string selects a per-plugin file. See [Application manifests](../service-manifests.md) for validation rules and format boundaries. Bus, HTTP and Consul plugin settings use TOML; see the [Bus reference](../reference/bus.md#configure-processes-and-relationships) for its fields. Logger settings, Kernel metadata and exported business data retain their XML formats.

### Plugin library names

Plugin targets and library stems use lowercase `snake_case`: `ark_utility_plugin`,
`ark_mysql_plugin` and `timers_plugin`. Manifest names must match these stems;
the loader adds the platform extension and the Debug `_d` suffix automatically.
Update existing manifests and build commands that use `AFUtilityPlugin`,
`AFMysqlPlugin` or `Sample1Plugin`; the old library filenames are no longer emitted.

### Existing XML manifests

The launcher still accepts user-maintained legacy XML manifests, for example:

```xml
<xml>
    <plugins plugin_lib_dir="bin/" plugin_conf_dir="conf/">
        <plugin name="ark_utility_plugin" conf="" />
        <plugin name="timers_plugin" conf="" />
    </plugins>
</xml>
```

- Keep the outer `xml` element: the launcher looks for its `plugins` child.
- Keep the trailing separator in directory prefixes such as `bin/` and `conf/`; the current loader concatenates strings.
- Use the library stem in `name`, without a platform suffix or a `lib` prefix. The loader appends `.dll`, `.so` or `.dylib`; Debug builds also append `_d` before the suffix.
- An empty `conf` value supplies no per-plugin filename. Timers does not need a separate `timers.conf` file.

Copy the executable and selected plugin libraries from the same build. Keep Debug and Release artifacts in separate runtime directories. The plugin list is not a dependency-order contract: modules declare required provider keys through the [module API](modules.md#declare-startup-dependencies). The manager validates these after installation and orders all four startup phases by dependency, and uses reverse cached order for admission-stop hooks. The remaining teardown order is unchanged. Rebuild the launcher and every plugin together for the lifecycle virtual methods and changed manager layout; older binaries are incompatible.

## Check startup and stop the process

For Timers, look for a log entry containing `timers_module` and `PostInit`. This identifies entry into the sample's post-initialization callback, which registers its timer.

On macOS/Linux, Ctrl+C or `SIGTERM` requests shutdown through the application's main loop. On Windows, enter `exit` in the application console. The launcher first seals managed roots, then runs each module's `stop_accepting()` once in reverse cached dependency order on an outer owner frame. A deferred call is observable as `stopping_acceptance`; success enters `draining` and continues module updates plus the shared owner-thread poll until accepted tasks finish. Their services and bridges remain available so those tasks can complete later awaits.

After task completion and the active callback stack returns, the manager waits for every module to report `ready_to_shut()`, then synchronously invokes shutdown hooks and unloading. A module must use its owner-frame readiness check to stop/drain native work and confirm native thread exit before allowing unload. When all hooks succeed, Timers logs `PreShut` and then `Shut`, and the launcher exits. A hook may block; the handler-count budget is not a shutdown-duration bound.

An admission-hook false/exception is a hard failure with `stop_accepting: <module>` context: already closed gates stay closed, pending frames/resources are retained, and updates/remaining hooks stop without retry. The official HTTP server closes per-request admission; other Net business entry points still need migration; see [the admission contract](modules.md#stop-new-business-roots).

If a shutdown hook returns `false` or throws, the launcher reports its phase, module and available exception details once, keeps the process/resources alive, and does not continue unloading. A failed native library close similarly reports `unload: <plugin>`, retains that library handle and wrapper, and stops before closing later libraries. Repeated `stop()` stays failed without repeating plugin exits or close calls. Earlier shutdown work and successful library closes are not rolled back. A successful `FreeLibrary` or `dlclose` call does not prove that the OS library image was unmapped or that arbitrary hot update is safe. `plugin_manager::update()` returning `false` or throwing during drain also locks the launcher into retention; owner updates continue where possible, but later completion cannot resume unloading.

`--shutdown_timeout_ms=N` configures a non-negative int64 wait in milliseconds; the default `0` waits indefinitely. A positive limit supplies a monotonic deadline to `plugin_manager::stop_until()`. The manager checks time after admission/readiness hooks return and before entering `pre_shut()`, so a successful slow hook cannot skip retention merely because no tasks remain. Expiry reports elapsed time, pending task count and the phase/module once. Before cleanup, sticky `timed_out` retains resources and permits owner updates; later completion or an unlimited `stop()` cannot resume teardown.

This limit is cooperative: it cannot interrupt a blocking user `update()`, readiness check, shutdown hook or unload call. After cleanup has begun, checks at hook/close return boundaries enter `cleanup_timed_out`, stop further cleanup and disable module updates. Actions already performed by `pre_shut()`, `shut()`, plugin exits or native closes cannot be rolled back; the in-progress call may have released resources before returning. Neither timeout state permits successful process exit, forced cancellation or destroying pending frames. Managed-task drain does not prove that arbitrary callbacks, native pool workers or background cleanup have finished; see [module task integration](tasks.md#shared-business-executor-for-modules). Timers's logs alone are not a readiness or shutdown guarantee for every plugin combination.

<Aside type="caution" title="Do not use process survival as the readiness check">

Manifest loading or validation failures return exit code `1` before plugin startup. If `plugin_manager::start()` returns `false` or a module/plugin startup exception propagates, the launcher flushes a synchronous diagnostic with the failing phase, module or plugin and available exception details, then terminates with exit code `1` before entering the update loop or starting the Windows command thread. This failure path bypasses C++ object/static destruction so partially started IO pools cannot hold the process open. Other buffered or asynchronous logs may be lost. The launcher does not roll back partial startup, run normal shutdown hooks or retry in the same process. Deployment tools should start a fresh process after this failure exit. Missing libraries or `DllEntryPlugin` entrypoints also report `load_plugin: <plugin>` and terminate with exit code `1`. An already loaded library missing its entrypoint is retained until process exit; it is not automatically unloaded. Argument parsing failures, including help handling, still return `0`. Check the expected module's initialization log and application-specific readiness before routing traffic to a process. A running PID or a zero exit status alone is insufficient.

</Aside>

The launcher retains startup diagnostics in a 1024-byte stack buffer and writes native stderr, truncating long text and continuing to exit under persistent C++ allocation failure. Only an independent owning capsule explicitly prepared in the single cleanup slot may run one failure action first; exit remains `1` with no normal shutdown or unload. Shared Utility/IO are not enrolled; see [startup-failure cleanup](../project/startup-failure-cleanup.md).

Failure-path checks: `check-app-business-runtime` covers the original 13 drain/readiness/retention scenarios plus `drain_admission` and three admission-hook failure cases (17 total) using the real launcher and a controlled test plugin; `check-native-unload` verifies plugin exit exceptions and retained library handles. These tests do not provide partial-startup rollback or dependency-ordered teardown. The [startup and shutdown closeout proposal](../project/lifecycle-closeout.md) records resource acquisition boundaries, current native-retirement limits and the selected route and stages that remain before adding those guarantees.

## Run one application

The shell and Windows helpers share one interface:

```text
run.sh  <Release|Debug> <name> <busid> [runtime-directory]
run.bat <Release|Debug> <name> <busid> [runtime-directory]
```

The default runtime directory is the script's directory. When invoking the checkout's `deployment/scripts/` helpers, pass the staged runtime explicitly; alternatively copy a helper into the runtime root to use that default. An explicit runtime path
is relative to the caller's working directory. The helper selects `bin/app`
(`app.exe` on Windows) for Release or `bin/app_d` (`app_d.exe`) for Debug, reads
`conf/<name>.app.toml` and `conf/log.conf`, and runs in the foreground. The name
also becomes the application's `--name` value. Use the executable directly for
legacy XML manifests, a custom manifest path, a different process name or other launcher flags.

After preparing the [Timers runtime](../get-started/quick-start.md), run from
the repository root:

```sh
sh deployment/scripts/run.sh Release timers 1.0.1.1 run/timers
```

On Windows, use `deployment\scripts\run.bat Release timers 1.0.1.1 run\timers`.
Check the application's initialization log before using it; the helper's path
checks do not validate plugins, business configuration or readiness.

## Stop one application

Prefer Ctrl+C on macOS/Linux or `exit` in the Windows application console.
For a process whose PID has been confirmed, `kill.sh <pid>` requests `SIGTERM`;
`kill.bat <pid>` forcibly terminates that PID. Both helpers reject other executable
names and require exactly one explicit PID. Recheck the PID's executable and
command line immediately before use; a PID can be reused after a process exits.
There is no process-name wildcard or machine-wide stop command.

On successful shutdown, the launcher writes `ARK Server stop ...` to standard
output, flushes it and exits with status `0`. This final lifecycle message uses
synchronous output.

## Migrate old helper commands

- Replace `rund.sh` / `rund.bat` with `run.sh Debug ...` / `run.bat Debug ...`.
- Replace `sample1.sh`, `mysql_sample.sh` and `sample5http_test.sh` with
  `run.sh Debug <name> <busid>`, using `timers`, `mysql_sample` or `http_server`. The name
  mapping does not enable omitted example targets. MySQL/Redis examples remain
  disabled in the default build; their manifests do not provide database
  connection options. Configure the public database modules explicitly.
- The old no-argument five-service launch sequence used missing `plugin_conf/`
  paths and inconsistent Release/Debug Bus IDs. Supply each application's name
  and Bus ID explicitly, and arrange service startup separately for the target
  environment. The helper no longer daemonizes applications.
- Replace `vc_clear.bat` with `cmake --build <cmake-build-directory> --target clean`
  to clean a selected build directory.
- Container recipes and cluster examples now live under [deployment/](../../deployment/README.md).

The byte-preserved historical articles under `docs/site/legacy-source/` still show
their original `bin/rund.*` commands. Use the current commands above instead.

## Troubleshoot startup

| Symptom | Check |
| --- | --- |
| Usage text or `Application parameter is invalid` | Supply all three required arguments; check spelling with `--help` |
| Manifest loading fails | Check the working directory, explicit path and format; a lowercase `.toml` suffix selects TOML, while legacy `*.app.conf` files use XML |
| Logger configuration cannot be read | Check the working directory and `--log_conf` path |
| `Load shared library[...] failed` | Check TOML `plugin_library_dir` or XML `plugin_lib_dir`, platform suffix, Debug suffix and the library's dependent shared libraries |
| Plugin loads but the expected module is unavailable | Check registration, enabled targets and required plugins; see [Create a plugin](plugins.md) |
| Built-in server role fails at `init:` | Load Bus and Net; Game also requires Kernel before callback registration. See [role startup errors](../concepts/server-architecture.md#role-startup-errors) for the checked scope |
| Built-in server role fails at `post_init:` | For Game, check Player metadata and callback-registration diagnostics. Check the listener endpoint and port occupancy; failed registration, listener creation or a missing service reaches launcher exit code `1` |
| Kernel metadata or configuration is missing | Stage the generated `resource/meta/` and `resource/server/` files in the process working directory |

The launcher implementation is in [main.cpp](https://github.com/OpenArkStudio/ark/blob/main/src/application/main.cpp).
