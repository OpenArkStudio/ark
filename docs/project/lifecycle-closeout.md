# Startup and shutdown delivery status

The lifecycle framework provides required-provider metadata, graph validation, ordered startup and one-shot consumer-first `stop_accepting()` hooks with a compatibility default. The HTTP server rejects new business routes and drains admitted response sends. The HTTP client rejects external roots while allowing trusted continuations of accepted work; other Net protocol entry points still need migration. Startup-failure cleanup has one opt-in slot, validated with a private standalone wheel. Other Net ingress migration, complete dependency-aware teardown and shared IO startup-failure cleanup remain subsequent work; start with the [launcher guide](../guides/applications.md) for operating instructions.

The [startup-failure cleanup source map and minimum slice](startup-failure-cleanup.md) detail one-slot enrollment, constructor safety, one-shot release after failure and IO prerequisites.

## Current startup and resource ownership

`plugin_manager` stores the current failure description and a bounded latest-call record, not a full per-module startup ledger. Plugin and module storage remains unordered. After installation, the manager snapshots declared provider keys, rejects missing providers/cycles and uses a stable topological order for all four startup phases. Modules without declarations are independent; ready-node ties use lexicographic registration keys. Manifest order determines neither this order nor teardown dependencies. See [startup dependencies and rebuild requirements](../guides/modules.md#declare-startup-dependencies).

| Boundary reached | Resources that may already exist | Failure behavior |
| --- | --- | --- |
| Parse arguments and prepare logging | Manager, shared owner runtime and default logger; no application plugin yet | Invalid arguments/help retain the current exit status `0`; manifest errors return `1` |
| Load a library and enter `DllEntryPlugin` | Loaded handle, plugin object, registered modules; installation can already start IO | Missing library/entry, false startup result or entry exception stops further startup |
| Validate startup dependencies | Installation resources remain live; no module startup hook has run | Missing required keys/cycles return false; metadata exceptions propagate with dependency context |
| `init()` | All successfully installed plugins; completed and partially executed module initialization | Stop at the first false return or exception; later modules in that phase are not called |
| `post_init()`, `check_config()`, `pre_update()` | Earlier completed phases, callbacks, timers, listeners, outbound clients and any submitted work | Same first-failure rule; no later phase runs |
| `start()` succeeds | The selected modules have completed all four phases | Enter owner updates; only then create the Windows command thread |

Concrete acquisition points matter: Net's HTTP client starts an IO pool in its constructor, Net queues clients and a health timer in `post_init()`, and Game registers Kernel callbacks and starts listeners in `post_init()`. Database connection configuration can construct native pools; its lifetime cannot be inferred from a module's last completed hook.

On startup failure the launcher writes the phase/owner diagnostic, runs only explicitly prepared independent-resource cleanup and exits `1` through `_Exit` (plus `TerminateProcess` on Windows), bypassing object/static destruction. It neither runs normal shutdown hooks nor retries. A library whose entry is missing remains loaded until process exit. `register_plugin` releases its local candidate before `install()` and records the plugin only after installation returns: recording only successful installation would miss resources published by a throwing installation. This is a cleanup-design constraint, not an existing rollback guarantee.

## Current normal shutdown

| Step | Gate and action | Resources that must remain available |
| --- | --- | --- |
| Request stop | Seal managed roots and enter `stopping_acceptance`; defer hooks while a callback stack is active | Providers, services, bridges and the shared owner runtime |
| Stop new external roots | On an outer owner frame, call reached `stop_accepting()` hooks once in reverse cached topology, then enter `draining` | Accepted workflows retain provider operations and completions; this is not pool/transport retirement |
| Complete accepted tasks | Continue enabled module updates, then one bounded owner-runtime poll; do not cancel accepted work | Later awaits in an accepted task may submit additional provider operations |
| Return from active stacks | Zero pending tasks is insufficient while update/poll/final-completion code is active | The final completion removes group membership before calling user code |
| Check all modules | Every `ready_to_shut()` must return true before any `pre_shut()` | A false result waits and permits owner updates; an exception enters sticky `failed` |
| Tear down | Run every `pre_shut()`, then every `shut()`, plugin exits/uninstallation, then native library closes | Each called hook must preserve dependencies until it returns |
| Finish | All stages succeed; repeated `stop()` returns true | Emit the synchronous stop message and exit `0` |

`ready_to_shut()` defaults to true and cannot certify unmanaged work; there is no `drain_ready()` API. The `stop_accepting()` phase also defaults to true and does not itself migrate official ingress. It uses reverse cached topology; readiness, `pre_shut()`/`shut()` and plugin/library release retain their previous order. The teardown loops do not establish provider-before/after-consumer order; explicit unregistration order inside one plugin does not order separate plugins.

### Native work and retention

| Owner | Existing completion/retirement path | Boundary |
| --- | --- | --- |
| Managed tasks | `task_group::stop_accepting()`, module service publication, shared `manual_runtime::poll()` | Task count and handler budget do not measure native worker exit or elapsed shutdown time |
| MySQL / Redis | `ready_to_shut()` observes service/bridge quiescence; `pre_shut()` seals admission and calls native retirement; `shut()` requires `native_retired()` before clearing connections | Retirement uses the pinned arknet pool's `close_and_wait()`; legacy service `stop()` only requests close/suppresses callbacks and is not this barrier |
| Net HTTP client / routed HTTP server | Close new request admission, drain requests/callbacks or routes/response sends, then synchronously stop the pool/server in `pre_shut()` | The client preserves trusted continuations; the server does not pause TCP accept. Independent background work needs separate ownership; normal native closure can still block |
| Net service manager | Holds listeners, clients and callbacks; its current `shut()` does not explicitly retire those services | Object destruction and selected DSO tests cover concrete release paths, not a general dependency-aware callback barrier |

The shared manual runtime executes user handlers on its owner thread; native IO pools keep their own threads. Do not destroy owners or unload their code until the corresponding callbacks have returned and the owning native retirement barrier has completed. A successful `dlclose`/`FreeLibrary` does not prove physical image unmapping.

The default shutdown wait is unlimited. A positive `--shutdown_timeout_ms` supplies a monotonic `stop_until()` deadline, retained across later stop calls. Time is checked after admission/readiness hooks return and before `pre_shut()`: a successful slow hook cannot skip retention even with zero pending tasks. Before cleanup, expiry enters sticky `timed_out`; owner updates may continue but later completion cannot resume cleanup. After cleanup begins, hook/close boundary checks enter `cleanup_timed_out`, retaining remaining resources without further updates or cleanup. No check can interrupt a blocking update, hook or native close, or undo work performed before that call returned. Drain update failure also latches launcher retention. A hook/close failure enters sticky `failed`; updates and further teardown stop. No timeout authorizes forced cancellation, destroying pending frames or successful process exit.

## Selected route and remaining implementation

1. **Prepare independent cleanup before acquisition.** The current bounded latest-call record and single resource slot support an owning capsule enrolled before acquisition. The private standalone-wheel fixture releases that resource once after startup failure; every outcome still exits `1`, without normal shutdown or DSO unload. A complete acquisition/phase ledger remains future work and must distinguish entered from completed phases: a failing hook may have acquired resources. Preserve the original failure and report cleanup results separately. Shared Utility Timer consumers and IO require their own ownership and retirement proof; this slice provides no generic rollback or same-process restart.
2. **Declare providers at module granularity.** Stage 1 provides `module::get_dependencies()` and `module_base<interface, providers...>`, validates all declared keys/cycles after installation and before `init()`, then orders each startup phase provider-first. Timers plus Utility is the first migrated application; other official modules have not been assigned guessed dependencies. The graph validates registration keys, not the dynamic type of arbitrary raw `add_module()` entries. Extend declarations from actual retained pointers/callbacks before using them for teardown. For example, Game retains Kernel/Guid/Bus/Msg/Net/Map providers; Net's service manager retains Bus/Timer; Kernel retains Map/metadata/config/Guid. Installation can already acquire resources, so validation cannot replace acquisition cleanup. Callback ownership, missing lifetime edges and phase cycles still need explicit treatment before safe teardown can be claimed.
3. **Separate ingress quiescence from provider retirement.** Stage 2 implements the explicit module action that stops new external business roots while preserving completions and follow-up provider calls for accepted tasks. Hooks run consumer-first once, with default compatibility and sticky failure. Successful startup caches topology; a manual manager or legal idle registry mutation triggers one validated rebuild before the first hook, never during poll/reentry. Keep declarations stable while registered. The existing task/active-stack and all-module readiness gates remain. Ordering `pre_shut()`/`shut()` consumers before providers is a subsequent stage. If one readiness check requires another module to tear down first, that is a cycle between phases: resolve it in the earlier ingress-quiescence action or bring a separate phase change back for approval. Include callback receivers, timer registrations and cross-plugin captures in the lifetime inventory. Uninstall a plugin and close its library only after all objects/callables depending on its code are retired. Preserve the current sticky retention behavior on any failure or wait-limit expiry. Introduce this as an opt-in migrated set; do not silently reinterpret legacy hooks.

These are internal lifecycle changes, not Registry, hot-update, service-discovery or network-protocol work.

## Delivery stages

The route has been selected; these are implementation boundaries, not pending policy questions.

| Selected direction | Current delivery |
| --- | --- |
| Required module-level provider declarations, with missing/cycle validation and ordered startup | Available framework stage; Timers/Utility migrated, undeclared modules remain independent nodes |
| Separate ingress quiescence while accepted tasks retain provider access | Core contract, HTTP request-level integration and controlled business fixture; other Net protocol ingress still needs migration |
| Opt-in cleanup after startup failure, then exit `1` | Available single slot, latest-call record and private standalone-wheel validation; shared Utility Timer, IO and full phase history remain outside this slice |
| Consumer-before-provider complete cleanup, after all active/task/readiness gates | Subsequent stage; no new general shutdown or unload guarantee is provided |

## Existing tests and remaining acceptance

The [launcher suite](https://github.com/OpenArkStudio/ark/blob/main/tests/app/business_runtime.cmake) retains the original **13 scenarios** below and adds four admission-phase cases, **17 in total**. It loads the real launcher/test DSO and uses controlled protocol completions plus a real native thread; it is not a real database-service or general plugin-combination test.

| Scenarios | Required observation |
| --- | --- |
| `drain`, `drain_deadline`, `drain_max_deadline` | Accepted sequential awaits and final callback return, then native thread exit/join, hooks, uninstall/destruction and exit `0` |
| `drain_admission` | The accepted root makes two further awaits after the root gate closes, then final completion/native retirement/teardown succeed; hook/reentrant-stop/rejected-root events occur once |
| `stop_accepting_false`, `stop_accepting_throw`, `stop_accepting_throw_unknown` | One phase/owner diagnostic; pending frames/process/resources remain; no provider shutdown or later hook/unload |
| `pre_shut_false`, `shut_false`, `shut_throw` | First failing teardown phase is diagnosed once; no later unload/destruction or repeated teardown |
| `ready_throw`, `ready_throw_unknown` | Native exit can occur, but readiness failure prevents join/teardown and remains diagnosed once |
| `timeout_task`, `timeout_native`, `timeout_stalled` | Distinguish managed-task/readiness waits; retain process and objects even after late completion; stalled work never permits unload |
| `update_throw`, `update_throw_unknown` | Updates can later resume, but the earlier drain failure still prevents teardown |

The suite's owned-child observation timeout terminates retained test processes; that is not successful framework shutdown. It also checks four invalid deadline values. [Startup-failure tests](https://github.com/OpenArkStudio/ark/blob/988dfbc048425cdb56cbe5f14314320fe698d07d/tests/app/startup_failures.cmake) separately cover 12 failure modes in TOML and XML, including a real listener started before failure. [Core startup](https://github.com/OpenArkStudio/ark/blob/988dfbc048425cdb56cbe5f14314320fe698d07d/tests/core/plugin_manager_startup.cpp) and [business-runtime](https://github.com/OpenArkStudio/ark/blob/988dfbc048425cdb56cbe5f14314320fe698d07d/tests/core/business_runtime.cpp) cases cover first failure, callback/reentrancy guards and all-module readiness.

Core admission tests cover cached reverse order, one-shot/reentrant behavior, manual stop, legal idle additions/removals, same-key replacement, invalid graphs and metadata/hook failures. The [module guide](../guides/modules.md#stop-new-business-roots) records the pinned arknet ingress-only API gap; server stop/acceptor mutation is not a safe substitute.

The separate `check-app-shutdown-deadlines` suite covers three successful slow-hook cases with no managed task or native-thread wait: admission and readiness return after the deadline without entering `pre_shut()`; a slow `pre_shut()` prevents `shut()`/uninstall and all subsequent module updates. Each process stays retained until the suite terminates its owned child. These checks exercise hook-return boundaries, not preemption or rollback.

Permanent Core dependency tests cover missing/self/cyclic providers, transitive/diamond graphs, duplicate edges, stable independent ordering, metadata exceptions and first-failure termination in every startup phase. `check-app-module-dependencies` checks the real Timers DSO without Utility and requires dependency failure before `init()` and launcher exit `1`; existing typed-example/preset tests exercise the complete application. Local receipts and hosted acceptance remain separate. Later stages still need permanent compensation tests at every entered/acquired boundary, cross-plugin callback-lifetime checks and provider survival through accepted follow-up work; startup ordering alone establishes none of those shutdown guarantees.

Pre-stage-1 source entry points: [manager](https://github.com/OpenArkStudio/ark/blob/988dfbc048425cdb56cbe5f14314320fe698d07d/src/core/plugin_manager.hpp), [launcher](https://github.com/OpenArkStudio/ark/blob/988dfbc048425cdb56cbe5f14314320fe698d07d/src/application/main.cpp), [task group](https://github.com/OpenArkStudio/ark/blob/988dfbc048425cdb56cbe5f14314320fe698d07d/src/core/task.hpp), [Net services](https://github.com/OpenArkStudio/ark/blob/988dfbc048425cdb56cbe5f14314320fe698d07d/src/plugins/net/net_service_manager_module_impl.cpp), [MySQL](https://github.com/OpenArkStudio/ark/blob/988dfbc048425cdb56cbe5f14314320fe698d07d/src/plugins/mysql/mysql_module.cpp), [Redis](https://github.com/OpenArkStudio/ark/blob/988dfbc048425cdb56cbe5f14314320fe698d07d/src/plugins/redis/redis_module.cpp). Native pool semantics refer to the configured [arknet revision](https://github.com/OpenArkStudio/arknet/tree/a0dfe09cd543e75cfb5996e537ce2fa49f2a758f), not an unverified dependency upgrade.
