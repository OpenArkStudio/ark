# Startup and shutdown closeout proposal

This proposal records the existing contract and the smallest next design for partial-startup cleanup and dependency-aware shutdown. The design below is pending approval; it changes no runtime behavior. Source baseline: [988dfbc0](https://github.com/OpenArkStudio/ark/tree/988dfbc048425cdb56cbe5f14314320fe698d07d). Start with the [launcher guide](../guides/applications.md) for operating instructions.

## Current startup and resource ownership

`plugin_manager` stores one current failure description, not a per-module startup ledger. Plugin and module containers are unordered; manifest order does not establish initialization or teardown dependencies.

| Boundary reached | Resources that may already exist | Failure behavior |
| --- | --- | --- |
| Parse arguments and prepare logging | Manager, shared owner runtime and default logger; no application plugin yet | Invalid arguments/help retain the current exit status `0`; manifest errors return `1` |
| Load a library and enter `DllEntryPlugin` | Loaded handle, plugin object, registered modules; installation can already start IO | Missing library/entry, false startup result or entry exception stops further startup |
| `init()` | All successfully installed plugins; completed and partially executed module initialization | Stop at the first false return or exception; later modules in that phase are not called |
| `post_init()`, `check_config()`, `pre_update()` | Earlier completed phases, callbacks, timers, listeners, outbound clients and any submitted work | Same first-failure rule; no later phase runs |
| `start()` succeeds | The selected modules have completed all four phases | Enter owner updates; only then create the Windows command thread |

Concrete acquisition points matter: Net's HTTP client starts an IO pool in its constructor, Net queues clients and a health timer in `post_init()`, and Game registers Kernel callbacks and starts listeners in `post_init()`. Database connection configuration can construct native pools; its lifetime cannot be inferred from a module's last completed hook.

On startup failure the launcher flushes the phase/owner diagnostic and exits `1` through `_Exit` (plus `TerminateProcess` on Windows), bypassing object/static destruction. It neither runs normal shutdown hooks nor retries. A library whose entry is missing remains loaded until process exit. `register_plugin` releases its local candidate before `install()` and records the plugin only after installation returns: recording only successful installation would miss resources published by a throwing installation. This is a cleanup-design constraint, not an existing rollback guarantee.

## Current normal shutdown

| Step | Gate and action | Resources that must remain available |
| --- | --- | --- |
| Request stop | The owner loop calls `stop()`, enters `draining` and seals every managed group's top-level admission | Providers, services, bridges and the shared owner runtime |
| Complete accepted tasks | Continue enabled module updates, then one bounded owner-runtime poll; do not cancel accepted work | Later awaits in an accepted task may submit additional provider operations |
| Return from active stacks | Zero pending tasks is insufficient while update/poll/final-completion code is active | The final completion removes group membership before calling user code |
| Check all modules | Every `ready_to_shut()` must return true before any `pre_shut()` | A false result waits and permits owner updates; an exception enters sticky `failed` |
| Tear down | Run every `pre_shut()`, then every `shut()`, plugin exits/uninstallation, then native library closes | Each called hook must preserve dependencies until it returns |
| Finish | All stages succeed; repeated `stop()` returns true | Emit the synchronous stop message and exit `0` |

`ready_to_shut()` is the current module hook; this proposal adds no `drain_ready()` API. The default hook returns true and cannot certify unmanaged work. There is no common ingress-stop phase or declared dependency graph. The teardown loops do not establish provider-before/after-consumer order; explicit unregistration order inside one plugin does not order separate plugins.

### Native work and retention

| Owner | Existing completion/retirement path | Boundary |
| --- | --- | --- |
| Managed tasks | `task_group::stop_accepting()`, module service publication, shared `manual_runtime::poll()` | Task count and handler budget do not measure native worker exit or elapsed shutdown time |
| MySQL / Redis | `ready_to_shut()` observes service/bridge quiescence; `pre_shut()` seals admission and calls native retirement; `shut()` requires `native_retired()` before clearing connections | Retirement uses the pinned arknet pool's `close_and_wait()`; legacy service `stop()` only requests close/suppresses callbacks and is not this barrier |
| Net HTTP client / routed HTTP server | Synchronous pool/server stop in `pre_shut()`; transport wrappers stop on destruction | These calls can block; default module readiness does not independently certify all callback receivers or retained external handles |
| Net service manager | Holds listeners, clients and callbacks; its current `shut()` does not explicitly retire those services | Object destruction and selected DSO tests cover concrete release paths, not a general dependency-aware callback barrier |

The shared manual runtime executes user handlers on its owner thread; native IO pools keep their own threads. Do not destroy owners or unload their code until the corresponding callbacks have returned and the owning native retirement barrier has completed. A successful `dlclose`/`FreeLibrary` does not prove physical image unmapping.

The default shutdown wait is unlimited. A positive `--shutdown_timeout_ms` measures monotonic owner-loop wait, including managed-task and readiness waits. It cannot interrupt a blocking update, readiness check, hook or native close. Expiry latches launcher retention even if tasks or native work later finish: no further `stop()` or unload attempt is made, but owner updates continue where possible. A drain update failure also latches retention. A hook/close failure instead puts the manager in sticky `failed`; updates and further teardown stop. Earlier successful hooks/closes are not undone. No timeout path authorizes forced cancellation, resource deletion or successful process exit.

## Proposed minimum change

1. **Record acquisitions before adding cleanup.** Add an internal ledger for loaded handles, entered/completed installation and module phases, and acquired-resource compensation records. Capture both entered and completed phases: a failing hook may have acquired resources. Keep the original startup failure primary and report cleanup failures separately. An opt-in module may register cleanup for a resource at acquisition; absent a proven cleanup action, retain the current failure-exit policy. Do not call all normal shutdown hooks on partially initialized modules or add same-process restart.
2. **Declare lifetime edges at module granularity.** Inventory actual retained pointers and callbacks, then declare required provider edges through construction metadata. For example, Game retains Kernel/Guid/Bus/Msg/Net/Map providers; Net's service manager retains Bus/Timer; Kernel retains Map/metadata/config/Guid. Validate missing required providers and cycles before module initialization in an opt-in application. Installation may already acquire resources, so graph validation cannot replace acquisition cleanup. Manifest order and RTTI names alone cannot encode callback ownership. Independent nodes need only a stable tie-break order; cyclic/unknown lifetime edges need an explicit policy before safe teardown can be claimed.
3. **Separate ingress quiescence from provider retirement.** Propose an explicit, opt-in action that stops new external business roots while preserving completions and follow-up provider calls for accepted tasks. Keep the existing global task/active-stack and all-module readiness gates in the first migration, then order `pre_shut()`/`shut()` consumers before providers using declared edges. If one readiness check requires another module to tear down first, that is a cycle between phases: resolve it in the earlier ingress-quiescence action or bring a separate phase change back for approval. Include callback receivers, timer registrations and cross-plugin captures in the lifetime inventory. Uninstall a plugin and close its library only after all objects/callables depending on its code are retired. Preserve the current sticky retention behavior on any failure or wait-limit expiry. Introduce this as an opt-in migrated set; do not silently reinterpret legacy hooks.

These are internal lifecycle changes, not Registry, hot-update, service-discovery or network-protocol work.

## Decisions required

| Decision | Recommended starting point | Alternative and cost |
| --- | --- | --- |
| Startup failure policy | Keep immediate exit `1` as default; opt in to cooperative compensation, then exit `1`, only for modules with explicit cleanup records | Require rollback for every existing plugin first; each constructor/installation side effect needs a verified compensator |
| Dependency declaration and migration | Required module-level lifetime edges in typed construction metadata; migrate a complete application set and reject unresolved/cyclic declarations there | Plugin-level manifest ordering is simpler but cannot represent cross-plugin module/callback lifetimes |
| New external work during drain | Approve a distinct ingress-quiescence contract while providers stay usable by accepted work | Reuse `ready_to_shut()`/`pre_shut()`; this mixes waiting, admission and retirement and needs precise new semantics before implementation |

## Existing tests and remaining acceptance

The existing [launcher suite](https://github.com/OpenArkStudio/ark/blob/988dfbc048425cdb56cbe5f14314320fe698d07d/tests/app/business_runtime.cmake) contains these **13 scenarios**. It loads the real launcher/test DSO and uses controlled protocol completions plus a real native thread; it is not a real database-service or general plugin-combination test.

| Scenarios | Required observation |
| --- | --- |
| `drain`, `drain_deadline`, `drain_max_deadline` | Accepted sequential awaits and final callback return, then native thread exit/join, hooks, uninstall/destruction and exit `0` |
| `pre_shut_false`, `shut_false`, `shut_throw` | First failing teardown phase is diagnosed once; no later unload/destruction or repeated teardown |
| `ready_throw`, `ready_throw_unknown` | Native exit can occur, but readiness failure prevents join/teardown and remains diagnosed once |
| `timeout_task`, `timeout_native`, `timeout_stalled` | Distinguish managed-task/readiness waits; retain process and objects even after late completion; stalled work never permits unload |
| `update_throw`, `update_throw_unknown` | Updates can later resume, but the earlier drain failure still prevents teardown |

The suite's owned-child observation timeout terminates retained test processes; that is not successful framework shutdown. It also checks four invalid deadline values. [Startup-failure tests](https://github.com/OpenArkStudio/ark/blob/988dfbc048425cdb56cbe5f14314320fe698d07d/tests/app/startup_failures.cmake) separately cover 12 failure modes in TOML and XML, including a real listener started before failure. [Core startup](https://github.com/OpenArkStudio/ark/blob/988dfbc048425cdb56cbe5f14314320fe698d07d/tests/core/plugin_manager_startup.cpp) and [business-runtime](https://github.com/OpenArkStudio/ark/blob/988dfbc048425cdb56cbe5f14314320fe698d07d/tests/core/business_runtime.cpp) cases cover first failure, callback/reentrancy guards and all-module readiness.

This proposal's review does not add a new runtime pass receipt or hosted acceptance claim. Before implementing the proposed policy, add permanent tests for compensation after each entered/acquired boundary, missing/cyclic edges, cross-plugin callback receivers and provider survival through accepted follow-up work. No independent violation of an already specified contract was established by this source review; missing rollback/ordering guarantees remain design work rather than an unverified runtime fix.

Source entry points: [manager](https://github.com/OpenArkStudio/ark/blob/988dfbc048425cdb56cbe5f14314320fe698d07d/src/core/plugin_manager.hpp), [launcher](https://github.com/OpenArkStudio/ark/blob/988dfbc048425cdb56cbe5f14314320fe698d07d/src/application/main.cpp), [task group](https://github.com/OpenArkStudio/ark/blob/988dfbc048425cdb56cbe5f14314320fe698d07d/src/core/task.hpp), [Net services](https://github.com/OpenArkStudio/ark/blob/988dfbc048425cdb56cbe5f14314320fe698d07d/src/plugins/net/net_service_manager_module_impl.cpp), [MySQL](https://github.com/OpenArkStudio/ark/blob/988dfbc048425cdb56cbe5f14314320fe698d07d/src/plugins/mysql/mysql_module.cpp), [Redis](https://github.com/OpenArkStudio/ark/blob/988dfbc048425cdb56cbe5f14314320fe698d07d/src/plugins/redis/redis_module.cpp). Native pool semantics refer to the configured [arknet revision](https://github.com/OpenArkStudio/arknet/tree/a0dfe09cd543e75cfb5996e537ce2fa49f2a758f), not an unverified dependency upgrade.
