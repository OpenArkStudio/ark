# Examples by capability

The bundled examples use capability names. Configure with applications and examples enabled; this naming change does not change the enabled set or update participation.

| Directory / application name | Plugin stem | Purpose and supported status |
| --- | --- | --- |
| `examples/timers` / `timers` | `timers_plugin` | Utility timers and module lifecycle; enabled, participates in updates |
| `examples/module_lifecycle` / `module_lifecycle` | `module_lifecycle_plugin` | Lifecycle and a small data-list demonstration; enabled, no update override. The callback demonstration remains commented out |
| `examples/entity_operations` / `entity_operations` | `entity_operations_plugin` | Kernel entity/data/table operations; enabled, no update override. Startup performs a bounded functional demonstration; the [old timed workload](../../benchmarks/ENTITY_OPERATIONS.md) runs only through an explicit benchmark command |
| `examples/guest_room` / `guest_room` | `guest_room_plugin` | Optional 2D guest lobby with JSON-defined Kernel Entity fields/configuration, authoritative movement and HP/MP actions, and the real Net transport; enable `ARK_BUILD_GUEST_ROOM_EXAMPLE`. [Build, ARK2 protocol and native verification](../../examples/guest_room/README.md); official [C# Godot client](https://github.com/OpenArkStudio/ark-client-godot) |
| `examples/mysql_sample` / `mysql_sample` | `mysql_sample_plugin` | Native MySQL coroutine example; disabled in the application example list, assembly checked by database tests with SSL enabled |
| `examples/redis_sample` / `redis_sample` | `redis_sample_plugin` | Native Redis coroutine example; disabled in the application example list, assembly checked by database tests |
| `examples/physics_scene` / `physics_scene` | `physics_scene_plugin` | Disabled; requires unsupported legacy PhysX 3. The checked-in sources and manifest do not establish a runnable integration |
| `examples/http_server` / `http_server` | `http_server_plugin` | Disabled as an application example; [typed construction and controlled DSO regression](../reference/http.md#legacy-http-example-construction) |
| `examples/http_client` / `http_client_sample` | `http_client_sample_plugin` | Disabled as an application example; future/callback POST and etcd lease demonstration with fixed external endpoints |

hpp/cpp files remain adjacent in each directory. Start with the [timers quick start](../get-started/quick-start.md). The generic launcher helpers select `conf/<name>.app.toml`, so use the capability name for both the process and application manifest. Kernel examples still require their business metadata.

## Migrate numbered example names

| Old directory, application and identifier prefix | New prefix |
| --- | --- |
| `sample1` | `timers` |
| `sample2` | `module_lifecycle` |
| `sample3` | `entity_operations` |
| `sample4` | `physics_scene` |
| `sample5` | `http_server` |

Rebuild modules and their consumers together: the C++ module/plugin type names and RTTI registration keys change. Replace old library stems such as `sample1_plugin` with `timers_plugin`, including platform extensions and Debug `_d`. Replace staged `conf/sample1.app.toml` with `conf/timers.app.toml` and update `--name`, `--app_conf`, launcher arguments and deployment ConfigMaps consistently for every row. The old targets, library aliases and configuration filenames are not emitted. An existing runtime directory is not migrated automatically; stage a fresh matching executable, plugin set and configuration before switching it over.

The byte-preserved historical documentation archive retains original names and commands. It is not the current naming guide. This page records names and supported status; the HTTP example fixture uses public service doubles and does not establish external-service or performance acceptance.

## Run entity operations

Stage the matching build, `config/entity_operations.app.toml`, `config/kernel.plugin.conf` and `config/log.conf` under a separate runtime, and copy `examples/entity_operations/resource` into that runtime. Start with `--name=entity_operations --app_conf=conf/entity_operations.app.toml`; see the [example instructions](../../examples/entity_operations/README.md). Startup creates four players and demonstrates data/table/custom values without timed loops. The real-DSO regression uses this same manifest and metadata. A separate benchmark requires an explicit `--iterations N`; its eight-iteration smoke disables timing and is not performance evidence.

## Database sample construction

MySQL/Redis samples use `module_base<module>`, `plugin_base<derived_type>`, plugin-private owning factories and explicit `DllEntryPlugin`/`DllExitPlugin` entry points. Each retains its concrete sample module RTTI registration key, inherited default `update()` and `module_update::disabled` registration. hpp/cpp files remain adjacent; coroutine operations, connection names and configuration are unchanged. No other retained examples are enabled.

[Database regressions](../../tests/database/README.md#sample-plugin-assembly) compile both module and plugin source files into actual DSOs and exercise entry, registration, database-interface lookup and exit. A local provider rejects configuration before any coroutine or network work. These assembly checks do not run the sample business workflow or establish live-service deployment acceptance.
