# Startup and shutdown delivery status

Ordered startup, one-shot ingress closure and accepted-task drain are available. Full dependency-aware teardown and shared IO startup-failure cleanup remain future work.

<CardGrid>

<LinkCard title="Operate the launcher" href="../guides/applications.md" description="Startup, shutdown and timeout handling." />
<LinkCard title="Prepare failure cleanup" href="startup-failure-cleanup.md" description="Independent ownership, 16-owner enrollment and IO prerequisites." />

</CardGrid>

## Current startup and resource ownership

After installation, `plugin_manager` uses declared provider keys to order all four startup phases topologically. Missing providers/cycles fail validation; undeclared modules are independent. Ready-node ties use lexicographic registration keys, not manifest order. Plugin/module storage remains unordered. The manager retains a failure description and bounded latest-call record, not a complete phase ledger. See [dependencies and rebuild requirements](../guides/modules.md#declare-startup-dependencies).

| Boundary reached | Resources that may already exist | Failure behavior |
| --- | --- | --- |
| Parse arguments and prepare logging | Manager, shared owner runtime and default logger; no application plugin yet | Invalid arguments/help retain the current exit status `0`; manifest errors return `1` |
| Load a library and enter `DllEntryPlugin` | Loaded handle, plugin object, registered modules; installation can already start IO | Missing library/entry, false startup result or entry exception stops further startup |
| Validate startup dependencies | Installation resources remain live; no module startup hook has run | Missing required keys/cycles return false; metadata exceptions propagate with dependency context |
| `init()` | All successfully installed plugins; completed and partially executed module initialization | Stop at the first false return or exception; later modules in that phase are not called |
| `post_init()`, `check_config()`, `pre_update()` | Earlier completed phases, callbacks, timers, listeners, outbound clients and any submitted work | Same first-failure rule; no later phase runs |
| `start()` succeeds | The selected modules have completed all four phases | Enter owner updates; only then create the Windows command thread |

Resource ownership follows acquisition: the HTTP-client constructor starts IO; Net queues clients/timers in `post_init()`; Game registers Kernel callbacks and listeners there; database configuration can create pools. A completed-hook record cannot prove resource lifetime.

<Aside type="caution" title="Startup failure exits the process">

The launcher reports phase/owner, runs only prepared independent cleanup, then exits `1` through `_Exit` (plus Windows `TerminateProcess`). No normal hooks, object/static destruction or retry run. Missing-entry libraries remain loaded. `register_plugin` releases its candidate before `install()` and records success afterward, so that table cannot inventory resources published by a throwing installation.

</Aside>

## Current normal shutdown

<Steps>

<Step title="Request stop">

Seal managed roots and enter `stopping_acceptance`; defer hooks while a callback stack is active

Keep: Providers, services, bridges and the shared owner runtime

</Step>

<Step title="Stop new external roots">

On an outer owner frame, call reached `stop_accepting()` hooks once in reverse cached topology, then enter `draining`

Keep: Accepted workflows retain provider operations and completions; this is not pool/transport retirement

</Step>

<Step title="Complete accepted tasks">

Continue enabled module updates, then one bounded owner-runtime poll; do not cancel accepted work

Keep: Later awaits in an accepted task may submit additional provider operations

</Step>

<Step title="Return from active stacks">

Zero pending tasks is insufficient while update/poll/final-completion code is active

Keep: The final completion removes group membership before calling user code

</Step>

<Step title="Check all modules">

Every `ready_to_shut()` must return true before any `pre_shut()`

Keep: A false result waits and permits owner updates; an exception enters sticky `failed`

</Step>

<Step title="Tear down">

Run every `pre_shut()`, then every `shut()`, plugin exits/uninstallation, then native library closes

Keep: Each called hook must preserve dependencies until it returns

</Step>

<Step title="Finish">

All stages succeed; repeated `stop()` returns true

Keep: Emit the synchronous stop message and exit `0`

</Step>

</Steps>

<Aside type="caution" title="Admission order is not complete teardown order">

`stop_accepting()` and `ready_to_shut()` default to true; they do not migrate ingress or certify unmanaged work. There is no `drain_ready()` API. Only admission hooks use reverse topology. Readiness, `pre_shut()`, `shut()`, plugin/library release retain their previous order; one plugin’s unregistration order cannot order another plugin.

</Aside>

### Native work and retention

| Owner | Existing completion/retirement path | Boundary |
| --- | --- | --- |
| Managed tasks | `task_group::stop_accepting()`, module service publication, shared `manual_runtime::poll()` | Task count and handler budget do not measure native worker exit or elapsed shutdown time |
| MySQL / Redis | `ready_to_shut()` observes service/bridge quiescence; `pre_shut()` seals admission and calls native retirement; `shut()` requires `native_retired()` before clearing connections | Retirement uses the pinned arknet pool's `close_and_wait()`; legacy service `stop()` only requests close/suppresses callbacks and is not this barrier |
| Net HTTP client / routed HTTP server | Close new request admission, drain requests/callbacks or routes/response sends, then synchronously stop the pool/server in `pre_shut()` | The client preserves trusted continuations; the server does not pause TCP accept. Independent background work needs separate ownership; normal native closure can still block |
| Opt-in Net external roots | [`reg_external_root_callback()`](../reference/network.md#explicit-external-root-registration) checks manager state on owner-thread dispatch; the application supplies rejection handling | Ordinary callbacks remain unchanged. The manager and borrowed receivers must outlive registration; this is not callback unregistration or an unload barrier |
| Net service manager | Holds listeners, clients and callbacks; its current `shut()` does not explicitly retire those services | Object destruction and selected DSO tests cover concrete release paths, not a general dependency-aware callback barrier |

Drive business continuations through `manager.update()`; direct `task_group::poll()` / `manual_runtime::poll()` calls do not grant HTTP continuation authority during drain. Native IO keeps its pool threads. Retain owners/code until callbacks return and native retirement completes. Successful `dlclose`/`FreeLibrary` does not prove physical image unmapping.

The default wait is unlimited. A positive `--shutdown_timeout_ms` sets a monotonic `stop_until()` deadline retained across stop calls. Checks run after admission/readiness hooks return, before `pre_shut()`, and at cleanup hook/close return boundaries. Zero pending tasks does not excuse a slow hook.

| Result | Retained behavior |
| --- | --- |
| `timed_out`, before cleanup | Owner updates may continue; late completion cannot resume cleanup |
| `cleanup_timed_out`, after cleanup begins | Retain remaining resources; no further updates or cleanup |
| `failed`, hook/close failure | No further updates or teardown |
| Drain-update failure | Launcher retention stays latched even if updates later recover |

<Aside type="caution" title="Deadlines do not preempt blocking work">

These states are irreversible. Checks cannot interrupt blocking updates, hooks or native closes, or undo completed actions. Expiry never permits forced cancellation, pending-frame destruction or successful exit.

</Aside>

## Selected route and remaining implementation

| Next implementation boundary | Required evidence |
| --- | --- |
| Independent cleanup before acquisition | Owning capsule and 16-owner enrollment; preserve the first failure and report cleanup separately. Entered phases can own resources even if incomplete. Private-wheel validation does not cover shared Utility Timer or IO; no general rollback, normal shutdown, DSO unload or same-process restart |
| Extend provider declarations | `module::get_dependencies()` / `module_base<interface, providers...>` validate keys before `init()`, not dynamic types of raw `add_module()` entries. Timers/Utility is migrated; derive further edges from actual retained pointers/callbacks |
| Complete dependency-aware teardown | Retire callbacks, timer registrations and cross-plugin captures before unloading their code; keep task/active-stack/readiness gates and sticky retention. Resolve readiness/cleanup phase cycles during ingress closure or review the phase change separately |

Provider inventory: Game retains Kernel/Guid/Bus/Msg/Net/Map; Net’s manager retains Bus/Timer; Kernel retains Map/metadata/config/Guid. Installation can already acquire resources, so graph validation cannot replace acquisition cleanup. Do not guess edges or silently reinterpret legacy hooks.

Successful startup caches topology. Manual managers and legal idle registry changes trigger one validated rebuild before admission hooks, never during poll/reentry; declarations remain stable while registered. Complete `pre_shut()`/`shut()` ordering is still future work.

Registry, hot update, service discovery and network protocols are outside this lifecycle change.

## Delivery stages

The route has been selected; these are implementation boundaries, not pending policy questions.

| Selected direction | Current delivery |
| --- | --- |
| Required module-level provider declarations, with missing/cycle validation and ordered startup | Available framework stage; Timers/Utility migrated, undeclared modules remain independent nodes |
| Separate ingress quiescence while accepted tasks retain provider access | Core contract, HTTP request-level integration, opt-in Net root registration, guest-room Join/Move/Action and controlled business fixture; other application callbacks still need explicit classification and migration |
| Opt-in cleanup after startup failure, then exit `1` | Available preallocated 16-owner registry with reverse cleanup and early halt, latest-call record and private standalone-wheel validation; shared Utility Timer, IO and full phase history remain outside this slice |
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

The guest room refuses new Join/Move/Action through its [actual business entry points](../../examples/guest_room/README.md#stop-new-business-requests), using existing `server_stopping` errors with Action sequence correlation while preserving state and the existing work/output limits. Heartbeats, disconnect tombstones and accepted-task completion continue. A separate child with an observation deadline loads actual Room/Kernel/Net DSOs, but its controlled host keeps the room DSO linked: this does not verify library unmapping or guarantee native send delivery. Shared IO, Registry, hot update, nested wire changes and the TCP 5000 workstream remain paused.

Core admission tests cover cached reverse order, one-shot/reentrant behavior, manual stop, legal idle additions/removals, same-key replacement, invalid graphs and metadata/hook failures. The [module guide](../guides/modules.md#stop-new-business-roots) records the pinned arknet ingress-only API gap; server stop/acceptor mutation is not a safe substitute.

The separate `check-app-shutdown-deadlines` suite covers three successful slow-hook cases with no managed task or native-thread wait: admission and readiness return after the deadline without entering `pre_shut()`; a slow `pre_shut()` prevents `shut()`/uninstall and all subsequent module updates. Each process stays retained until the suite terminates its owned child. These checks exercise hook-return boundaries, not preemption or rollback.

Permanent Core dependency tests cover missing/self/cyclic providers, transitive/diamond graphs, duplicate edges, stable independent ordering, metadata exceptions and first-failure termination in every startup phase. `check-app-module-dependencies` checks the real Timers DSO without Utility and requires dependency failure before `init()` and launcher exit `1`; existing typed-example/preset tests exercise the complete application. Local receipts and hosted acceptance remain separate. Later stages still need permanent compensation tests at every entered/acquired boundary, cross-plugin callback-lifetime checks and provider survival through accepted follow-up work; startup ordering alone establishes none of those shutdown guarantees.

Pre-stage-1 source entry points: [manager](https://github.com/OpenArkStudio/ark/blob/988dfbc048425cdb56cbe5f14314320fe698d07d/src/core/plugin_manager.hpp), [launcher](https://github.com/OpenArkStudio/ark/blob/988dfbc048425cdb56cbe5f14314320fe698d07d/src/application/main.cpp), [task group](https://github.com/OpenArkStudio/ark/blob/988dfbc048425cdb56cbe5f14314320fe698d07d/src/core/task.hpp), [Net services](https://github.com/OpenArkStudio/ark/blob/988dfbc048425cdb56cbe5f14314320fe698d07d/src/plugins/net/net_service_manager_module_impl.cpp), [MySQL](https://github.com/OpenArkStudio/ark/blob/988dfbc048425cdb56cbe5f14314320fe698d07d/src/plugins/mysql/mysql_module.cpp), [Redis](https://github.com/OpenArkStudio/ark/blob/988dfbc048425cdb56cbe5f14314320fe698d07d/src/plugins/redis/redis_module.cpp). Native pool semantics refer to the configured [arknet revision](https://github.com/OpenArkStudio/arknet/tree/a0dfe09cd543e75cfb5996e537ce2fa49f2a758f), not an unverified dependency upgrade.
