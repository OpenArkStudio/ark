# Application architecture

An ark process loads plugin libraries; each plugin can register several modules that call registered interfaces. Cross-process communication uses the [Bus and network interfaces](../reference/bus.md).

![Application composition and call boundaries](../assets/application-composition.svg)

## From manifest to modules

The launcher receives `--busid`, `--name` and `--app_conf`. The application manifest chooses shared-library names and optional per-plugin configuration filenames. The shipped `config/timers.app.toml` is:

```toml
[application]
plugin_library_dir = "bin/"
plugin_config_dir = "conf/"

[[plugins]]
name = "ark_utility_plugin"
config = ""

[[plugins]]
name = "timers_plugin"
config = ""
```

Built-in application manifests use TOML, except the retained external Freebird example. A lowercase `.toml` suffix selects TOML, while the legacy `*.app.conf` entry selects XML. See [Application manifests](../service-manifests.md) for the full schema and validation rules; per-plugin and business-resource formats are unchanged.

The loader loads each library and calls its exported plugin entry. `timers_plugin::install()` registers `timers_module`; the utility plugin provides services such as timers. A module resolves those services with `find_module<timer_module>()`, rather than creating another copy of the utility plugin.

Manifest names must match runtime libraries such as `ark_utility_plugin` and `timers_plugin`, rather than C++ class names. Debug libraries use the configured `_d` postfix. An empty `config` supplies no filename; the loader does not parse plugin-specific content.

## Paths and a runnable starting point

The existing launcher resolves these paths from its working directory. Library and plugin-configuration prefixes are concatenated with filenames, so retain the trailing separator in `bin/` and `conf/`. The manifest's location does not automatically become the base directory.

After [installation](../get-started/installation.md) and runtime staging, run from `run/timers/`:

```sh
./bin/app --busid=1.0.1.1 --name=timers --app_conf=conf/timers.app.toml
```

The example's `init()` obtains timer and Guid modules; `post_init()` schedules a timer. Its update and shutdown methods demonstrate the module lifecycle. See [timers_module.cpp](https://github.com/OpenArkStudio/ark/blob/main/examples/timers/timers_module.cpp) and the [quick start](../get-started/quick-start.md) for the runtime setup.

## Choosing the composition

| Application need | Include and initialize |
| --- | --- |
| Local timers or identifiers | Utility plugin and the corresponding module interfaces |
| Schema-backed entities | Kernel and its dependencies, metadata/configuration resources, maps and instances |
| Process messages | Bus/network plugins, process identity and endpoint configuration |
| Application behavior | An application-owned plugin that registers its business modules |

Adding a public header or CMake API target only makes an interface available to compile. Runtime configuration still has to load its owning plugin. Modules do not run in a separate process merely because they are in separate libraries.

## Ordering and ownership

The manager installs plugins before running `init()`, `post_init()`, `check_config()` and `pre_update()`. The first `false` return stops startup; a propagated startup exception also reaches the launcher. The launcher reports the failed phase and available module/plugin details, then terminates with exit code `1` before entering the update loop. It does not roll back partially started plugins or run normal shutdown hooks on this failure path. See [startup failure handling](../guides/applications.md#check-startup-and-stop-the-process).

The manager validates declared module provider keys after installation and topologically orders all four startup phases; currently ready independent modules use stable registration-key order. Manifest list order does not define startup order, and the cached graph also controls consumer-first admission-stop hooks. Update and the remaining teardown order are unchanged; official ingress migration is still incomplete. See [startup dependencies](../guides/modules.md#declare-startup-dependencies). Missing-module lookups can return null. Verify required plugins/resources, initialization logs and application-specific readiness before routing traffic; a running process alone does not prove every service initialized successfully.

Pointers returned by `find_module()` belong to the loaded plugin. Stop asynchronous producers before teardown and do not retain those pointers after unload. Keeping connections and state through an in-flight business-plugin replacement is not an automatic consequence of this composition model.

<CardGrid>

<LinkCard title="Framework boundaries" href="architecture.md" description="Responsibilities and in-process calls." />
<LinkCard title="Module implementation" href="../guides/modules.md" description="Interfaces, lifecycle and borrowed pointers." />
<LinkCard title="Kernel data" href="../reference/kernel.md" description="Metadata, entities and data interfaces." />

</CardGrid>
