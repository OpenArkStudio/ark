# Quick start

Run Timers to check that the application can load plugins and enter the module lifecycle. It uses `ark_utility_plugin` and `timers_plugin`; it does not require Consul, a database service or Excel configuration generation.

<Steps>

<Step>

## Build the application and plugins

Complete [Install and build](installation.md) first. The commands below use its Release output directory, `app-build/bin/`, from the repository root. Keep the executable and plugin files from the same build together.

</Step>

<Step>

## Prepare a runtime directory

Stage binaries, the manifest and logger settings in a runtime directory separate from source configuration.

<Tabs syncKey="timers-platform">

<TabItem label="macOS/Linux">

```sh
mkdir -p run/timers/bin run/timers/conf
cp -R app-build/bin/. run/timers/bin/
cp config/timers.app.toml config/log.conf run/timers/conf/
```

</TabItem>

<TabItem label="Windows PowerShell">

```powershell
New-Item -ItemType Directory -Path run/timers/bin, run/timers/conf -Force | Out-Null
Copy-Item app-build/bin/* run/timers/bin/ -Recurse -Force
Copy-Item config/timers.app.toml, config/log.conf run/timers/conf/
```

</TabItem>

</Tabs>

The required runtime files are:

```text
run/timers/
├── bin/       app + ark_utility_plugin + timers_plugin from the same build
└── conf/
    ├── timers.app.toml
    └── log.conf
```

The copy includes other build artifacts; `timers.app.toml` selects only the two named libraries. No separate `timers.conf` or `resource/` is required.

</Step>

<Step>

## Start Timers

Run from `run/timers/`: the manifest's `bin/` and `conf/` paths are relative to the working directory.

<Tabs syncKey="timers-platform">

<TabItem label="macOS/Linux">

```sh
cd run/timers
./bin/app --busid=1.0.1.1 --name=timers \
  --app_conf=conf/timers.app.toml --log_conf=conf/log.conf
```

</TabItem>

<TabItem label="Windows PowerShell">

```powershell
cd run/timers
.\bin\app.exe --busid=1.0.1.1 --name=timers `
  --app_conf=conf/timers.app.toml --log_conf=conf/log.conf
```

</TabItem>

</Tabs>

Debug builds use `app_d` or `app_d.exe` and plugin filenames with `_d` before the platform suffix. Use the complete Debug output in its own runtime directory; the XML plugin names stay unchanged.

</Step>

<Step>

## Check the result

The console should show a `timers_module` log with `PostInit`. The sample registers a timer that emits `timer log` at ten-second intervals, five times. The supplied logger settings also write beneath `binlog/` in the runtime directory.

Stop with Ctrl+C on macOS/Linux, or enter `exit` in the Windows application console. Timers should log `PreShut`, then `Shut`.

<Aside type="caution" title="Handle startup failures">

A failed plugin load or module startup callback stops later startup stages and updates. The launcher reports the failing stage and exits with code `1`. Use [Application launcher](../guides/applications.md#troubleshoot-startup) to diagnose configuration and library errors.

</Aside>

</Step>

</Steps>

## Continue with your application

- [Create a module](../guides/modules.md) and [Create a plugin](../guides/plugins.md) explain how to register behavior.
- [Application launcher](../guides/applications.md) explains Bus identity, plugin selection and configuration paths. Applications that load Kernel data also need their generated `resource/` files.
- [Configuration tools](../guides/tools.md) shows the separate Excel-to-resource workflow. Its Python requirements do not apply to this sample or to the C++ network tests.
- [Network regression tests](../networking.md) verify network behavior independently. Applications that select Consul or database plugins need those services and their own settings.

## Read the typed sample construction

`examples/timers`, `examples/module_lifecycle` and `examples/entity_operations` use
[`module_base<interface_type>`](../guides/modules.md#use-a-base-class-with-a-private-implementation)
and [`plugin_base<derived_type>`](../guides/plugins.md#use-typed-registration-and-explicit-entries).
The sample classes remain in their plugin's adjacent headers. They do
not introduce a separate service interface and retain their capability-specific module RTTI keys. Timers now inherits `module_base<module, timer_module, guid_module>` to declare its actual required Utility providers; the other two samples retain `module_base<module>`. Dependency validation precedes startup hooks, and each startup phase initializes providers first. See [startup dependencies and rebuild requirements](../guides/modules.md#declare-startup-dependencies).

`timers_plugin.hpp`, for example, declares a factory returning
`std::unique_ptr<timers_module>`; `timers_module.cpp` defines it. The plugin
registers that owned result and explicitly exports `DllEntryPlugin` /
`DllExitPlugin`, including the existing logger initialization. Successful
registration still requires explicit unregister/delete during `uninstall()`.

| Sample | Update registration | Runtime dependencies |
| --- | --- | --- |
| Timers | `module_update::enabled`, preserving its `update()` override | Utility plugin; no external business metadata |
| `module_lifecycle` | `module_update::disabled`, preserving its inherited default `update()` | Kernel and Utility plugins; business metadata under `resource/meta/` |
| `entity_operations` | `module_update::disabled`, preserving its inherited default `update()` | Kernel and Utility plugins; bundled small example metadata |

Timers and module_lifecycle retain their business bodies; entity operations now starts a lightweight data demonstration, with its old timed workload moved to an explicit benchmark. Capability names change module RTTI keys and manifest library stems.
`module_lifecycle`'s metadata is external business data; this construction change does not
generate or convert it. When applications, examples, configuration and core tests
are enabled with the required Kernel/Utility plugins, run
`cmake --build app-build --target check-typed-examples -j2` to load the actual
Timers/module_lifecycle/entity_operations DSOs, execute five updates on the manager thread and verify ordered startup and
shutdown. This test supplies a small test dataset for `module_lifecycle`; it does not
validate an application's exported business dataset.

## Use installed outputs

After [installing](installation.md#install-and-consume-the-cmake-package), replace `app-build/bin/` in the copy commands with the selected prefix's `bin/`, and copy configuration samples from `<prefix>/share/ark/config/`. The install tree itself is not the Timers working directory: prepare `run/timers/conf/` as above, then launch from `run/timers/`. A relocated package still needs compatible external runtime libraries. Installed-consumer checks verify CMake/link consumption; they do not establish application deployment acceptance.
