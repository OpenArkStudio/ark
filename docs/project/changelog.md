# changelogs

## Unreleased

### Features

- Net, Bus messaging, Kernel/configuration and built-in server roles, including Game, validate declared providers before startup hooks. Startup follows provider order; the Map/Kernel runtime cycle stays outside declared dependencies. See [module dependencies](../guides/modules.md#declare-startup-dependencies).

- Application and logger settings share one manifest, with console INFO defaults and optional file output, formatting and named loggers in the same file.

### Improvements

- Process shutdown requests managed-task cancellation, cleans up and destroys modules consumer-first, then closes all libraries last. Applications/operators complete announcements, ingress closure, player removal and saving before stopping the process.

- Core type dispatch and Kernel callback adapters use standard C++20 facilities while preserving existing trait aliases, generated random sequences and callback argument behavior.
- Zstandard dictionary and context handles have explicit RAII ownership; moving a populated compressor transfers its resources safely.

### Fixes

- Logger output directories, filenames and legacy XML configuration paths support UTF-8, including Windows native UTF-16 file operations, while preserving existing rotation and output settings.
- Common Entity callbacks remain callable for every registered class instead of leaving later classes with moved-from functions.
- XXTEA rejects malformed buffers before reading them and releases temporary allocations on string exceptions, preserving existing ciphertext.

### Upgrade notes

- **BREAKING CHANGE:** Logger string paths now uniformly use UTF-8. Windows callers passing ANSI/active-code-page bytes must convert them before calling the logger. Public types/signatures remain unchanged; rebuild applications/plugins that include framework file sinks. See [UTF-8 migration](../service-manifests.md#embedded-logging).
- **BREAKING CHANGE:** Remove `module::stop_accepting()` overrides and Net external-root helper registrations; HTTP no longer uses manager-state business admission or continuation exceptions. Move business shutdown into application procedures and rebuild the launcher, all plugins and consumers together. Resource waiting remains unlimited by default, with optional `--shutdown_timeout_ms`. See [lifecycle migration](lifecycle-closeout.md#migration-and-remaining-boundaries).

- `zstd_compress` is move-only and its object layout changes. Replace copying with moving, borrowing or constructing an independent instance, and rebuild consumers/plugins together. See [compression ownership](../reference/utility.md#compression-buffers-and-concurrency).

- Required-provider declarations change the concrete bases of `config_module_impl`, `kernel_module_impl`, `game_net_module_impl` and the Login/Proxy/Master/World/Dir/Router implementations. Update references to their former `module_base` specializations and rebuild affected consumers/plugins together; interface keys and wire formats remain unchanged. See [Kernel migration](../reference/kernel.md) and [server migration](../concepts/server-architecture.md).

- Remove launcher flags `--log_conf` / `-l` and migrate legacy logger settings into `[logging]` in the application manifest; explicit old flags now fail with exit code `1`. Public `application_manifest` and logger type layouts change: rebuild applications, plugins and consumers together. See [migration](../service-manifests.md#embedded-logging).

- Source runtime assets move out of `build/`: configuration samples to `config/`, business resources to `resources/`, offline tools to `tools/`, and the four launch/stop helpers to `deployment/scripts/`. Update external scripts and copy sources; runtime packages retain `bin/`, `conf/` and `resource/`. `build/` is only a user-selected CMake build directory; existing build outputs need not move.

- Container recipes and Kubernetes examples now live in `deployment/`. Use
  `docker build -f deployment/Dockerfile .` from the repository root, and update
  external references to the former `docker/` and `kubernetes/` directories.
- Runtime helpers now launch one foreground application with explicit build mode,
  name and Bus ID. Replace `rund.*` and sample wrappers with `run.* Debug ...`;
  stop helpers require one explicit PID. See [runtime migration](../guides/applications.md#migrate-old-helper-commands)
  for the removed five-service sequence and Visual Studio cleanup command.

## version 0.9

- Fix `HeartBeatManager` remove mistake(Thanks @sks)
- Add `DateTime`
- Fix sonar bad smells
- Replace `AFBitValue` with `std::bitset`
- Add `random` functions
- Replace `easylogging++` with `spdlog`
- Add BusPlugin(use `busid` as server id and communicate, for now not totally finished)
- Update `Proto` library to `ProtoPlugin`
- Update `Net` library to `NetPlugin`
- Update `brynet` to the newest version
- Refactoring the net plugin
- Add DataTable callback when `DataTable.Add/Set/Del(data)`
- Modify table row index
- Modify module register order and fix config's bug
- Optimise - Don't load DataNode in AFConfigModule when the node value is the default
- Add AFCMsgModule(not finished yet)

## version 0.8

- Replace `evpp` with `brynet` for reducing dependence
- Add `AFCronScheduleModule`, using `cron` syntax
- Add `WebSocket` support
- Replace `easylogging++` with `spdlog`
- Add `SonarCloud` check
- Add `Codecov` test
- Fix some warnings
- Fix some code smells of SonarCloud
- Fix GUID bug
- Fix some known bugs

## version 0.7

- Restructure log module
- Update to VS2017 as default IDE in windows
- Add function  `strlcpy`
- Use a new memory pool
- Fix `AFTimer` bugs
- Reduce `.git` disk usage
- Fix Net bugs
- Fix parameters warnings
- Fix tools, config id can be a number
- Rename Example files

## version 0.6

- Change Mem allocation
- Add Timer Module(Using time wheel)
- Modify Plugin DLL entry and exit functions by using macro `ARK_DLL_PLUGIN_ENTRY(Plugin Class)`
- Add `ARK_STATIC_ASSERT`
- Add launch parameters for launching multi-processes
- Use `std::is_base_of` to instead self derived class check

## version 0.5

- Fix some memory bugs
- Add memory pool and allocation
- Add wiki(Unfinished)
- Restructure file directories
- Add a new file processing tool

## version 0.4

- Unified concept, Property -> DataNode Record -> DataTable
- Unified configurations
- Update Google.Protobuffer 3.x
- Fix some bugs

## version 0.3

- Fix msg head bugs
- Rename some files

## version 0.2

- Modify base data structure
- Modify the performance of the network
- Modify directories structure
- Add CMake support(Linux platform is testing)
- Modify the compiling way of **Dep**
- Modify GUID to 128 bits

## version 0.1

- First release
