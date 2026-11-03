# Startup and shutdown delivery status

The framework orders startup and releases process resources. Application and operator procedures complete announcements, ingress closure, player removal and saving before process stop; the framework has no business-admission or natural business-drain phase.

<CardGrid>

<LinkCard title="Operate the launcher" href="../guides/applications.md" description="Startup, resource shutdown and optional timeout handling." />
<LinkCard title="Prepare failure cleanup" href="startup-failure-cleanup.md" description="Independent ownership, 16-owner enrollment and IO prerequisites." />

</CardGrid>

## Startup remains fail-fast

Declared provider keys order all four startup phases provider-first. Missing providers/cycles fail before hooks; undeclared modules are independent. Ready-node ties use registration keys, not manifest order. Installation and partial hooks can already acquire resources, so topology is not acquisition rollback.

On startup failure the launcher reports phase/owner, invokes only independent cleanup explicitly enrolled before acquisition, and exits `1` through `_Exit` (plus Windows `TerminateProcess`). The preallocated registry holds up to 16 owners, visits reverse enrollment order and stops at unsafe/failed cleanup. Normal shutdown hooks, object/static destruction, DSO unload and same-process retry do not run. Utility Timer’s callback-free wheel cleanup does not cover retained business callbacks or shared IO. See [startup-failure cleanup](startup-failure-cleanup.md).

## Resource shutdown

<Steps>

<Step title="Finish business operations before stop">

The application or operator announces shutdown, closes business entry, removes players and completes saving before requesting process stop.

</Step>

<Step title="Request resource cancellation">

`stop()` enters `stopping` and calls task-group `close()` to seal top-level task admission and request cancellation. It never forcibly destroys suspended frames.

</Step>

<Step title="Drive safe completion">

Continue `manager.update()` for service/bridge dispatch and one bounded shared-executor poll. Wait for actual task completion, active stacks to return and every module’s `ready_to_shut()`. A false readiness result waits; an exception enters `failed`.

</Step>

<Step title="Release consumers before providers">

Use two reverse dependency passes. First run every `pre_shut()` while all module objects remain alive, closing/joining native callback sources. Only then run each module’s `shut()` and destroy its owned object before its providers. Stop on the first false result or exception.

</Step>

<Step title="Close libraries last">

Uninstall plugins after all modules are released. Keep every DSO loaded through module destructors, callback/capture destruction and plugin exits, then close native libraries. Success enters `stopped`; repeated stop succeeds.

</Step>

</Steps>

Cancellation is cooperative and may not stop an awaited operation or undo a remote write. A zero task count does not prove callback/native-thread safety. Modules must retain owners/code until their native work is ready. Declared edges must represent real borrowed-provider lifetimes; the Map/Kernel runtime cycle is not a declared dependency cycle. Raw external owners and undeclared edges remain application responsibilities. `task_group::poll()` remains available to standalone owners; manager-owned groups use manager updates. There is no HTTP continuation authority tied to a poll phase.

Provider objects remain alive during consumer `shut()`/destruction, but their native resources may already be closed by the preceding `pre_shut()` pass. Release borrows or unregister metadata only where the interface permits; do not rely on database writes, network sends or business saving at this stage. Complete those operations while running, before process stop.

### Native resource boundaries

| Owner | Resource retirement |
| --- | --- |
| Managed tasks | `close()` requests cancellation; service completions and shared-runtime polling retire actual frames |
| MySQL / Redis | Readiness observes service/bridge quiescence; `pre_shut()` seals admission and retires pools through native `close_and_wait()`; `shut()` requires native retirement before clearing connections |
| Net HTTP client | Readiness waits for native results, queued delivery and active callbacks/capture release; `pre_shut()` closes/cancels its runtime. Ordinary calls have no business-root/continuation classification |
| Routed HTTP server | Readiness atomically seals native route dispatch and waits for active route/response-send leases; `pre_shut()` stops/joins the server and releases its endpoint. Routes have no manager-state HTTP 503 gate |
| Utility scheduler | Readiness requests resource close, discards queued timers and seals further scheduling, then waits for already-dispatched callbacks/captures; `pre_shut()` joins retired workers. This is resource retirement, not application business admission |
| Net services | Owning cleanup closes endpoints and clears borrowed callback captures before consumers/providers are destroyed. Ordinary protocol callbacks retain their registration behavior |

Neither completed native closes nor `dlclose`/`FreeLibrary` success proves physical OS image removal or arbitrary hot-update safety.

### Optional resource deadline and retention

`--shutdown_timeout_ms` defaults to `0` (unlimited). A positive value supplies `stop_until()` with a monotonic deadline; the earliest supplied deadline persists across later calls.

| State | Behavior |
| --- | --- |
| `running` | Ordinary execution |
| `stopping` | Continue owner updates until tasks, active stacks and native resources are safe |
| `timed_out` | Optional deadline expired before cleanup; retain resources, allow owner updates, never resume cleanup |
| `cleanup_timed_out` | Deadline expired after cleanup began; retain remaining resources without more updates/cleanup |
| `failed` | Readiness/cleanup/native close failed; retain remaining resources without retrying later cleanup |
| `stopped` | Hooks, object destruction, plugin exits and native closes succeeded |

Checks occur at readiness and cleanup return boundaries and before cleanup. They cannot preempt blocking hooks, native joins or closes, or undo completed work. The launcher reports `shutdown:` context once. An owner-update failure also latches launcher retention even if later updates recover. Timeout/failure does not permit pending-frame destruction or successful process exit.

## Migration and remaining boundaries

**BREAKING CHANGE:** `module::stop_accepting()` and Net’s `reg_external_root_callback()` helper are removed. Delete module overrides and replace helper registrations with ordinary protocol callbacks plus any application-owned business gate needed before stopping the process. HTTP request continuation special cases and manager-state HTTP 503 admission are removed. Rebuild the launcher, all official/business plugins and affected consumers together with compatible headers/compiler settings; do not mix old binaries. Lookup keys and wire formats are unchanged.

Startup fail-fast and the registered startup-cleanup contract are unchanged. Shared IO startup-failure cleanup, undeclared cross-plugin callback lifetimes, Registry and hot update remain separate work. Dependency order does not establish production capacity or a general unload certificate.

## Verification scope

Permanent targets include Core task/dependency/cleanup tests, `check-app-business-runtime`, `check-app-shutdown-deadlines`, `check-native-unload` and the affected official plugin suites. Their existence is not a passing receipt for this revision: report exact commands, tested source and results when running them. Owned-child observation deadlines terminate retained test processes; they are not successful framework shutdown.

The [database service report](../reports/database-services.md#public-database-service-workflows) records **17 passing modes on 2026-11-03** using build `3948d881`, with equivalent database/core/configuration/dependency/build inputs at `10e385b4`. The [acceptance index](../guides/database-validation.md) links the native entries and their limits.

| Evidence | Executed boundary and result |
| --- | --- |
| Positive workflows | 3 actual-DSO modes, 1,351 cycles / 4,053 exit-`0` invocations in 300 seconds; owning results, commit/rollback visibility and resource stop |
| Failure controls | 2 modes, each once, exit `0`; real module factories/implementations linked into the host, covering authentication rejection, observed timeouts and explicit recovery |
| Cancellation, TLS and manager stop | 12 actual-DSO modes, each once, exit `0`: 4 explicit request-cancellation modes, 3 TLS controls and 5 pending-request manager-stop modes. Cancellation/stop checks independently observe the tagged active operation and target-session retirement |

MySQL cancellation retains `outcome_unknown=true`; cancelled transaction/prepared results remain `unconfirmed`, and an observed external zero is not a rollback acknowledgement or global exactly-once guarantee. These bounded database observations add no general startup-IO or shutdown guarantee, long-duration stability or production-capacity conclusion. Direct-module positive, restart, isolation and server-termination evidence retain their own recorded revisions. Local native tests, hosted CI and deployed documentation remain separate evidence states.
