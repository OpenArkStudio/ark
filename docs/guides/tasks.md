# Coroutine tasks

`<ark/task.hpp>` provides `ark::task<T>` (an Asio `awaitable<T>` alias, with no custom promise) and `ark::task_group`. Groups start top-level `task<void>` tasks, which can await `task<T>` for an owning result or await compatible Asio operations. Modules use `plugin_manager`'s shared executor and module groups; standalone tools can create their own.

## Build and run the example

Follow [Install and build](../get-started/installation.md) to configure dependencies, then run from the ark source directory on macOS or Linux:

```sh
cmake -S . -B task-build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DARK_BUILD_APPLICATIONS=OFF -DARK_BUILD_PROTO=OFF \
  -DARK_BUILD_CONFIG=OFF -DARK_BUILD_TESTS=OFF \
  -DARK_BUILD_CORE_TESTS=ON -DARK_BUILD_EXAMPLES=ON
cmake --build task-build --target ark_task_example check-task-sdk
./task-build/examples/task_group/ark_task_example
```

In PowerShell, use the same options on one line:

```powershell
cmake -S . -B task-build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DARK_BUILD_APPLICATIONS=OFF -DARK_BUILD_PROTO=OFF -DARK_BUILD_CONFIG=OFF -DARK_BUILD_TESTS=OFF -DARK_BUILD_CORE_TESTS=ON -DARK_BUILD_EXAMPLES=ON
cmake --build task-build --target ark_task_example check-task-sdk
.\task-build\examples\task_group\ark_task_example.exe
```

Add your project's dependency toolchain/prefix options as needed. Boost.Asio is the default; core-only builds also support `-DASIO_STANDALONE=ON`. `ARK_BUILD_EXAMPLES=ON` builds this executable with applications disabled, without plugin DLLs, generated Protobuf messages or a database server.

The [complete example](../../examples/task_group/main.cpp) prints `result=42 completed=1`: start one coroutine, stop accepting new top-level tasks, then poll until completion. Its two-second failure guard exits with code 2 without destroying pending frames; it is not an application shutdown timeout or abandonment strategy.

If your CMake project already adds ark, use the public target:

```cmake
target_link_libraries(my_tasks PRIVATE ark::core)
```

`ark::core` propagates C++20, public includes and arknet requirements; no private `src/plugins` includes are needed. Tasks remain implemented in the public header.

## Start typed work on the owner thread

A typed child coroutine can suspend and return a value:

```cpp
#include <ark/task.hpp>

ark::task<int> next_value(int value) {
    co_await asio::post(asio::use_awaitable);
    co_return value + 1;
}

ark::task<> compute(int& result) {
    result = co_await next_value(41);
}
```

The example's `main()` creates a shared `arknet::manual_runtime` and group, then submits `compute(result)`. Its completion handler receives `std::exception_ptr` (empty on success). Referenced state must outlive the task; coroutines must not access captured state after their lambda closure is destroyed. Prefer named coroutine functions with explicit parameter ownership.

Construct, operate and destroy runtime/group on one thread. `manual_runtime` creates no task/continuation worker: `poll()` runs ready Asio handlers on its constructing thread. Timers/name resolution may use internal helper threads; native database/network pools retain their documented IO ownership.

## Shared business executor for modules

![Module tasks and their shared executor](../assets/shared-business-runtime.svg)

Each manager owns one library-provided `manual_runtime`. Register the module, call `manager.create_task_group(module, maximum)` during initialization, then submit through `manager.get_tasks(module)`. Capacity must be positive. Unknown modules, duplicate groups and creation during shutdown are rejected. Register each module pointer once; other registrations do not invalidate group references.

Private database bridges share this executor through `manager.get_task_runtime()`, including those inside public MySQL/Redis plugins. Drive updates in order:

1. Module `update()`: call `service.update()`, then `bridge.dispatch()`.
2. `manager.update(timestamp, maximum_handlers = 64)`: poll once, sharing the handler budget across all groups. Modules must not also poll their group/runtime.

`manager.is_polling_tasks()` is an owner-thread query that is true only while
`manager.update()` dispatches its central task poll, including task completions.
It is false during ordinary module updates and after the poll returns or throws.
Direct `task_group::poll()` or retained `manual_runtime::poll()` calls outside
`manager.update()` do not establish this phase.
The marker belongs to the manager object, so providers in another shared library
can inspect this phase without relying on that library's Asio thread-local
context. It is a trusted execution scope, not an identity token for arbitrary
external workflows; application ingress must not post fresh roots onto this
executor during drain.

**Migration:** raw group/runtime polls keep executing ready work and retain their
existing teardown/reentry guards, but no longer authorize Net HTTP follow-up
requests after admission closes. Drive manager-owned tasks with `manager.update()`.
Standalone groups and tools retain their direct-poll API. Rebuild the host and
all plugins together for the inline manager phase behavior; no virtual slots or
object data are added.

`manager.stop(timestamp)` first seals new top-level tasks in every group, then runs [one-shot module admission hooks](modules.md#stop-new-business-roots) in reverse cached topology. Active update/completion stacks defer those hooks to the next outer owner-frame stop. It returns `false` without waiting while tasks or active stacks remain. Keep services and bridges accepting for accepted coroutines' later operations. After completion and stack unwinding, another `stop()` synchronously runs `pre_shut()`, `shut()` and unloading; user hooks may block.

`manager.stop_until(timestamp, deadline)` accepts a `std::chrono::steady_clock::time_point`. The earliest deadline supplied remains active across later calls, including `stop(timestamp)`; an unlimited call cannot clear it. Without a supplied deadline, `stop(timestamp)` retains its unlimited behavior. Time checks occur at synchronous hook boundaries and before `pre_shut()`. A hook that returns after expiry cannot enter later cleanup. Expiry is sticky and returns `false`; `get_shutdown_failure()` identifies the phase/module, while `get_shutdown_exception()` remains empty for a plain timeout. Once cleanup has begun, completed actions are not undone and `cleanup_timed_out` also disables updates. See [launcher timeout behavior](applications.md#check-startup-and-stop-the-process).

| State | Caller action |
| --- | --- |
| `running` | Update normally and accept tasks |
| `stopping_acceptance` | Groups are sealed; return from active stacks and call stop on an outer owner frame to close module root gates once |
| `draining` | Continue module updates and the central poll; retain business state and services |
| `timed_out` | A deadline expired before cleanup; continue owner updates but never resume teardown |
| `cleanup_timed_out` | A deadline expired after cleanup began; retain remaining resources without further updates or cleanup |
| `failed` | A shutdown hook failed/threw or a native library close failed; stop further unloading and retain remaining resources |
| `stopped` | Shutdown hooks and native close calls succeeded; repeated `stop()` returns `true` |

Inspect `get_shutdown_state()`, `get_shutdown_failure()` and `get_shutdown_exception()` to distinguish waiting from failure. After successful admission hooks, the launcher continues driving pending tasks. A false/exception in an admission hook enters sticky failed: it reports once and pins pending frames/resources without further updates or teardown; it does not mean wait-and-retry. There is no implicit shutdown timeout, cancellation, detach or frame destruction instead of draining; task checks do not wait and handler budgets do not bound shutdown latency.

| `dynamic_library::unload()` result | Effect |
| --- | --- |
| Native close fails | Return `false`, keep handle; manager records `unload: <plugin>`, latches failure and retains the wrapper/remaining libraries |
| Native close succeeds | Clear handle |
| Empty/already closed wrapper | Return `true` without a native call |

Later `stop()` calls do not retry plugin exits/closes or roll back teardown. Close success does not establish OS image removal.

Use manager/groups on their constructing thread. In update/shutdown hooks or executor callbacks, do not mutate module registries or recursively update. Any pending managed task prevents every module's unregistration: it may borrow another provider. Destroying a manager with tasks, on an active callback stack or from another thread terminates; destruction cannot initiate draining.

<Aside type="caution" title="Task completion does not release native IO resources">

The task check covers only managed groups' accepted top-level tasks. Each module's admission/readiness hooks have their documented native scope; see the [Net HTTP client](../reference/network.md#http-client-admission-and-drain) and [HTTP server](../reference/http.md). Safe code unloading also requires external callbacks to return, pool workers to exit and background cleanup to finish; zero tasks proves none of these. Unmanaged work must not reference a module being destroyed.

</Aside>

## API and admission

| API | Behavior |
| --- | --- |
| `task_group(runtime, maximum = 64)` | Retain shared runtime and cap accepted top-level tasks; null runtime/zero limit throws `std::invalid_argument` |
| `start(task<>, completion = {})` | Return a nonzero observation/cancellation ID, or zero when full/stopped; refused lazy tasks never start or invoke completion |
| `stop_accepting()` | Refuse new top-level tasks without cancelling/polling accepted work; harmless when repeated on owner thread |
| `cancel(id)` / `cancel_all()` | Request terminal cancellation; retain accepted tasks until actual completion |
| `close()` | Stop admission and request cancellation; no polling/waiting |
| `poll(maximum = 64)` | Run up to this many ready shared-runtime handlers without waiting; zero does nothing |
| `pending()` | Count accepted top-level tasks whose completion has not retired |
| `closed()` | True only when admission is stopped and no accepted top-level tasks remain |

Dropping an ID does not cancel; dropping an unstarted `task<T>` runs no coroutine body. Task-body exceptions reach the optional completion handler as failures; completion-handler exceptions propagate from `poll()`.

Shared-runtime polling may run other groups' or operations' handlers. The limit counts handlers, not task steps/time; it bounds neither frame time nor hook/shutdown duration. Reentrant polling is rejected. Wrong-thread calls throw `std::logic_error`; destruction has stricter rules below.

## Finish accepted work before destruction

| Intent | Action and required driving |
| --- | --- |
| Natural completion | `stop_accepting()`, then continue owner updates; keep services/bridges available and accepting further asynchronous operations |
| Cancellation | `close()`, then drive service completions, bridge dispatch and runtime polls until actual completion |

Cancellation depends on the awaited operation and coroutine code. Requests neither forcibly destroy frames nor prove that a remote write did not happen.

<Aside type="caution" title="Destruction requires an empty owner">

Destroy a group on its constructing thread with `pending() == 0`; otherwise it calls `std::terminate`. Destruction does not detach, recursively poll or wait. Keep runtime, awaited-operation owners and referenced state alive as long as needed.

</Aside>

The last completion retires group membership before invoking user code, which may destroy the empty group object, but not unrelated outstanding operations. `closed()` tracks only this group's top-level tasks: it does not establish transport drain, a callback barrier, safe DLL unload or a complete module shutdown design.

Rebuild all plugins against the same updated manager headers; stable binary plugin ABI and hot-update compatibility are not provided.

## Continue

- [Create a module](modules.md): lifecycle callbacks and borrowed module interfaces.
- [Create a plugin](plugins.md): plugin registration, build and runtime selection.
- [Database coroutine tests](../../tests/database/COROUTINES.md): private native-operation bridges, owner update order and explicit test teardown.
- [MySQL](../reference/mysql.md) and [Redis](../reference/redis.md): public plugin configuration, shared business executor and native pool retirement.

Use `ark::scheduler_task` when deriving from the scheduler helper base. `ark::task<T>` denotes Asio coroutines; `ark::scheduler` call signatures are unchanged.
