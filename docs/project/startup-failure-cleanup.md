# Opt-in startup-failure cleanup

Startup failure defaults to immediate exit `1`. An independent owned resource explicitly enrolled through `plugin_manager::prepare_startup_cleanup()` may run its failure cleanup once first. The preallocated registry supports up to 16 independently owned resources across modules and covers callback-free Utility Timer wheels; retained business callbacks and shared IO require separate retirement. Acquisition source-map baseline: [d9618a66](https://github.com/OpenArkStudio/ark/tree/d9618a663fc2069b734007812590c88736d17ed5). There is no same-process retry, normal-shutdown rollback or inferred external business/transaction rollback; Registry and hot update remain paused.

## What exists when startup fails

[The loader](https://github.com/OpenArkStudio/ark/blob/d9618a663fc2069b734007812590c88736d17ed5/src/core/plugin_manager.hpp#L635) retains a successfully loaded DSO before its entry call; [plugin registration](https://github.com/OpenArkStudio/ark/blob/d9618a663fc2069b734007812590c88736d17ed5/src/core/plugin_manager.hpp#L372) releases its local plugin candidate **before** `install()` and inserts the plugin into the successful-plugin map only afterward. That map cannot be a complete failure-cleanup inventory.

| Failure point | Actual ownership / live resources |
| --- | --- |
| Plugin constructor throws | The DSO is already loaded. No complete plugin pointer returns; C++ unwinds completed bases/members, not the incomplete object's destructor. Escaped callbacks/threads need their own ownership proof. |
| Module factory or registration throws | [Private factories](https://github.com/OpenArkStudio/ark/blob/d9618a663fc2069b734007812590c88736d17ed5/src/plugins/utility/timer_module_impl.cpp) return `unique_ptr<interface>`; [typed registration](https://github.com/OpenArkStudio/ark/blob/d9618a663fc2069b734007812590c88736d17ed5/src/core/plugin_base.hpp#L54) keeps it until bookkeeping succeeds. A complete unpublished candidate is destroyed on rejection; completed constructor subobjects unwind on construction failure. Previously registered modules remain live. A destructor's existence does not prove nonblocking retirement. |
| Install throws after registering modules; or successful-plugin insertion allocates and throws | The released plugin and its published module/update entries may exist without a successful-plugin entry. No automatic uninstall or rollback occurs. |
| Dependency validation fails | Installation/constructor resources already exist; no module `init()` has run. |
| `init()` returns false / throws | All plugins have installed; earlier modules' init resources and the failing hook's partial resources can exist. Constructor-started IO can already run. |
| `post_init()` returns false / throws | All init calls passed; earlier post-init callbacks/timers/queued work and the failing hook's acquisitions can exist. |
| `check_config()` returns false / throws | Init/post-init passed. The name does not make this hook read-only; earlier resources and its partial acquisitions remain. |
| `pre_update()` returns false / throws | The first three stages passed; partial preparation remains possible. The launcher's ordinary update loop and Windows command thread have not started. |

The four-stage [driver](https://github.com/OpenArkStudio/ark/blob/d9618a663fc2069b734007812590c88736d17ed5/src/core/plugin_manager.hpp#L117) stops at the first false/exception. [The launcher](https://github.com/OpenArkStudio/ark/blob/d9618a663fc2069b734007812590c88736d17ed5/src/application/main.cpp#L309) flushes a diagnostic, then uses `_Exit(1)` (also `TerminateProcess` on Windows), bypassing normal object/static destruction. This retention is a process-failure policy, not proof that callbacks stopped or native workers joined.

| Concrete acquisition | Source consequence |
| --- | --- |
| [Utility Timer init](https://github.com/OpenArkStudio/ark/blob/d9618a663fc2069b734007812590c88736d17ed5/src/plugins/utility/timer_module_impl.cpp#L48) creates/initializes a wheel; Timers [post_init](https://github.com/OpenArkStudio/ark/blob/d9618a663fc2069b734007812590c88736d17ed5/examples/timers/timers_module.cpp) registers a callback | [Wheel initialization](https://github.com/OpenArkStudio/ark/blob/d9618a663fc2069b734007812590c88736d17ed5/src/core/time/timer.hpp#L79) builds local owners before commit. Wheel clear releases owned storage; it is not the module shutdown contract. |
| [HTTP client constructor](https://github.com/OpenArkStudio/ark/blob/d9618a663fc2069b734007812590c88736d17ed5/src/plugins/net/http_client_module_impl.cpp#L244) starts its IO pool | Workers can exist during install, before any init hook. Its destructor/pre-shutdown calls synchronous runtime stop. |
| Net [post_init](https://github.com/OpenArkStudio/ark/blob/d9618a663fc2069b734007812590c88736d17ed5/src/plugins/net/net_service_manager_module_impl.cpp#L111) queues configured clients and registers a health timer; [update](https://github.com/OpenArkStudio/ark/blob/d9618a663fc2069b734007812590c88736d17ed5/src/plugins/net/net_client_service_impl.cpp#L248) starts queued clients | Queueing is not connection completion. [create_server](https://github.com/OpenArkStudio/ark/blob/d9618a663fc2069b734007812590c88736d17ed5/src/plugins/net/net_service_manager_module_impl.cpp#L282) can acquire a listener during a calling startup hook. Record the call site, not one assumed phase. |
| HTTP [endpoint reservation](https://github.com/OpenArkStudio/ark/blob/main/src/plugins/http/http_server_module_impl.cpp) precedes native construction/start | A cpp-private local owner retains the reservation through native construction/binding/start and transfers its string by nonthrowing swap only after success. See the independent HTTP exception-safety boundary below. |
| MySQL [constructor/init/configure](https://github.com/OpenArkStudio/ark/blob/d9618a663fc2069b734007812590c88736d17ed5/src/plugins/mysql/mysql_module.cpp#L218) | Constructor owns state; init binds the owner runtime. `configure()` constructs the native pool **before** named-map insertion, so workers can exist if insertion later throws. Pool construction starts IO in pinned arknet `a0dfe09c`. |

## Independent resource enrollment and execution

On the owner thread, create a complete owning capsule and enroll it **before** acquisition:

```cpp
auto capsule = std::make_shared<owned_timer_context>();
if (!manager.prepare_startup_cleanup(
        "isolated-wheel", "my-plugin", capsule, release_timer_context)) {
    throw std::runtime_error("startup cleanup enrollment rejected");
}
return std::make_unique<isolated_timer_module>(capsule);
```

The API accepts owner and code-owner keys, a strong `shared_ptr<void>` and a plain `startup_cleanup_result (*)(void*)`. Each key must be nonempty, NUL-free and at most 95 bytes. The named `startup_cleanup_capacity` is **16**, preallocated in each manager. Resource owner IDs must be unique across all code owners; several resources may share a code-owner key. Invalid keys, duplicate owners, full capacity, a foreign thread, executor reentry or finished startup return false without retaining the rejected capsule. Preparation is `noexcept` and does not allocate. Acquire nothing after rejection. Capsule allocation happens before acquisition and retains the default failure exit if it fails.

Preparation is allowed at idle before startup or during starting/installing; cleanup is eligible only after the single `start()` returned false or threw and unwound. A finished attempt cannot start again. Embedded callers must still exit `1` after cleanup; this is no retry/recovery API. A separate constructor failure before entering start keeps its existing RAII/default-exit behavior.

The constructor retains the capsule, moves the wheel handle into it without throwing, then initializes the wheel. Cleanup never accesses an incomplete module; the host retains the capsule if construction throws. The action checks absent/partial ownership, calls the **owned resource's** `time_wheel_manager::shut()`, resets the handle and returns `done`. It does not call module pre-shutdown/shutdown or invoke business callbacks. The permanent fixture owns a private wheel without threads, external publication or borrowed providers; it does not certify cleanup of shared Utility Timer consumers; the guarded Timer integration below retains their business callbacks.

![Startup cleanup and retained failure exit](../assets/startup-failure-cleanup.svg)

| Record | Bounded meaning |
| --- | --- |
| `get_startup_call()` | Only the most recently entered host-managed call: plugin constructor/install, loading/dependency check and four module hooks. Fixed owner/phase buffers hold 95/23 bytes. Entry precedes user code; completion records passed/false/exception. This is not every object's phase history or a resource inventory. |
| Owned resource registry | Up to 16 entries with keys, strong capsules, code owners and actions prepared before acquisition; one attempt-wide entered marker and sticky result. The capsule represents partial acquisition; phases or a missing acquired marker do not prove resource absence. |

The launcher preserves the primary diagnostic, then calls `cleanup_failed_startup()` after the outer start frame returned/unwound. The manager seals enrollment and managed task root admission without cancellation/polling, and guards lifecycle/registry reentry. It runs actions in reverse successful enrollment order, independently of module startup order. Each action runs at most once. The first `unsafe`, `failed` or exception halts traversal and retains that owner and every older owner; exceptions and `not_run` action results become `failed`. Repeated calls return the cached result without resuming traversal. Enrollment, traversal and emergency diagnostics allocate no memory; individual actions must obey the same failure-path contract. It never calls normal stop, uninstall or DSO close. Successful startup only disarms attempt enrollment; the complete module must retain ongoing capsule ownership.

Every cleanup outcome still exits `1`. All loaded code, unretired owners and capsule/control-block deleter code stay alive until process exit, including a successfully emptied capsule. Unenrolled modules, partially installed objects absent from the successful-plugin map and existing constructor unwinding retain their previous behavior. An enrolled action must be independent, finite and nonblocking; the host cannot preempt a blocking function.

The emergency diagnostic uses a 1024-byte stack buffer and native stderr writes, prioritizing phase/owner over exception detail and truncating long text. It constructs no strings, formats no logger message and calls no virtual naming method. If allocation interrupted ordinary phase diagnostics, it uses the fixed call record. Windows retains `TerminateProcess`; other platforms retain `_Exit(1)`. Native write failure still proceeds to exit.

**Compatibility:** no module virtual method or ordinary shutdown order changes. Expanding the inline registry changes the public `plugin_manager` class layout: rebuild the host, every plugin and consumers against the same headers; do not mix old binaries.

**Independent HTTP exception safety:** a cpp-private reservation owner releases only the key it successfully reserved when construction/binding/start returns false or throws. Rejected duplicate reservations cannot release another module's key. After native start, a nonthrowing string swap transfers the reservation to the module for normal `pre_shut()` release; no public API, class layout or lifecycle changes are required. Port `0` continues to bypass fixed-endpoint reservation.

`check-http-startup-failure` compiles the production cpp in an isolated test translation unit. It persistently rejects allocation once the private set contains the key until the exception is caught, and verifies that the first failed allocation is the native server object, before its constructor or workers run. A native listen-failure case occupies a local port and checks reservation release after the false return. The same endpoint must then start successfully after either failure; duplicate rejection/shutdown must preserve the live owner's reservation, and shutdown permits reuse. A long-string ownership test verifies that the successful transfer is `noexcept`, preserves its storage and makes no allocation. The existing network routing/duplicate/restart regressions remain in `check-network`. This repairs local endpoint bookkeeping; it does not enroll HTTP in framework startup-failure cleanup or establish nonblocking native IO retirement.

## Utility Timer

Timer prepares its independent capsule before allocating the wheel object or storage. Cleanup uses `has_retained_callbacks()` to inspect every owned record, including completed timers and additions interrupted before active-ID registration.

| Wheel state | Failure cleanup |
| --- | --- |
| No retained callbacks | Release the wheel and return `done` |
| Any retained business callback | Return `unsafe`; retain the wheel, callback and required code until exit |

Explicit timer removal permits release only if no other callback remains. Successful startup disarms enrollment while Timer keeps its wheel. No normal hooks, business polling, shared IO closure or consumer rollback run.

`check-timer` and `check-startup-cleanup` cover real Timer initialization and later failures, persistent allocation failure, and composition with other cleanup owners. A retained callback stops traversal before older owners; repeated cleanup does not retry.

## IO stays outside the first slice

For Net/HTTP/MySQL opt-in, first prove constructor/member unwinding cannot lose a live owner or block before the failure runner. Contexts must exist before worker creation and callbacks must retain complete state rather than an incomplete module `this`. Keep callback code, receivers, bridge/runtime and native owners alive until a proven retirement barrier. Ordinary module update may execute business code on partial startup and is not a cleanup pump.

Pinned arknet's `io_pool::stop()` can wait/join; MySQL [retirement](https://github.com/OpenArkStudio/ark/blob/d9618a663fc2069b734007812590c88736d17ed5/src/plugins/mysql/detail/mysql_service.hpp#L432) uses `close_and_wait()`, while service `stop()` only requests close/suppresses callbacks. See the [pinned pool implementation](https://github.com/OpenArkStudio/arknet/blob/a0dfe09cd543e75cfb5996e537ce2fa49f2a758f/include/arknet/mysql/pool.hpp#L972). These calls are not a nonblocking startup-cleanup contract. Require an existing or separately reviewed request/retired observation, bounded host driving and callback policy before enrollment; do not hide a blocking close in a detached thread or add a new promise/compensation transaction here. Closing a local pool does not establish SQL/business rollback or rewrite an unknown native outcome.

## Scoped validation

`cmake --build <build> --target check-startup-cleanup --parallel 1` runs permanent C++ checks for false/exception in four stages, 16-owner capacity, invalid/duplicate/foreign/late enrollment without retained rejected ownership, reverse order, successful disarm, early halt, sticky results and reentry. The actual launcher loads a separate DSO to exercise constructor failures before/partway/after acquisition, partial-install failure and every startup stage; it checks one release per visited owner, exit1, no later update/ordinary shutdown/uninstall/DSO or capsule destruction. Three-module cases verify reverse cleanup and retention of older owners after unsafe/failed/throwing cleanup. Isolated allocation-failure children keep global new failing through the real emergency exit: capsule allocation failure, allocation-free enrollment/recording and full-capacity traversal, interrupted phase diagnostics and partial wheel initialization. `check-app-startup-failures` retains the existing unenrolled real-listener scenarios.

Prepare owning call arguments before arming startup allocation failures. A separate pre-entry failure case verifies that an enrolled resource stays retained and cleanup does not run before `start()` is entered. An unexpected-termination control must report context and exit with failure, without waiting for a Windows crash dialog.

This is local scoped module/private-resource acceptance, not IO retirement, business rollback or hosted-platform proof; raw logs remain in the local receipt. HTTP coverage is limited to reservation exception safety and ordinary local listener reuse, with no claim about generic IO startup rollback.

Two IO decisions remain TODO; neither blocks the private-wheel slice:

1. Which first IO owner is allowed to enroll, and for its already accepted startup operations which must finish versus may cancel? Source code cannot choose that business/completion policy.
2. What absolute startup-failure cleanup wait budget does that deployment permit? Expiry still means retained owners and exit `1`; the value must come from its operational requirement, and cannot preempt a blocking API.
