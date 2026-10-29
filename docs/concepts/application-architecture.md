# Application architecture

An ark application is a process assembled from selected plugin libraries. A plugin can register several modules, and modules call each other through registered interfaces. Composition happens within the process; communication with another process uses the [Bus and network interfaces](../reference/bus.md).

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

The manifest's names are runtime library names. Existing `ark_utility_plugin` / `timers_plugin` names do not follow the C++ class spelling and must match the built artifacts. Debug libraries use the configured `_d` postfix. An empty `config` supplies no configuration filename; the loader itself does not parse plugin-specific content.

## Paths and a runnable starting point

The existing launcher resolves these paths from its working directory. Library and plugin-configuration prefixes are concatenated with filenames, so retain the trailing separator in `bin/` and `conf/`. The manifest's location does not automatically become the base directory.

After following [installation](../get-started/installation.md) and preparing the runtime directory, run from `build/`:

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

The manager validates declared module provider keys after installation and topologically orders all four startup phases; currently ready independent modules use stable registration-key order. Manifest list order does not define startup order, and this graph does not change update or shutdown order. See [startup dependencies](../guides/modules.md#declare-startup-dependencies). Missing-module lookups can return null. Verify required plugins/resources, initialization logs and application-specific readiness before routing traffic; a running process alone does not prove every service initialized successfully.

Pointers returned by `find_module()` belong to the loaded plugin. Stop asynchronous producers before teardown and do not retain those pointers after unload. Keeping connections and state through an in-flight business-plugin replacement is not an automatic consequence of this composition model.

Continue with [framework boundaries](architecture.md), [module implementation](../guides/modules.md), or [Kernel data](../reference/kernel.md).
