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

`manager.update()` polls the shared executor after service completion dispatch. `manager.is_polling_tasks()` only observes that phase; it grants no business-request or HTTP continuation authority. Standalone groups retain `task_group::poll()`; manager-owned module tasks use the manager to avoid duplicate polling.

`manager.stop(timestamp)` enters `stopping` and calls `close()` on every task group, sealing admission and requesting cancellation. It returns `false` while tasks or active callback stacks remain; it never forcibly destroys frames. Continue owner updates and native completion dispatch. Cancellation depends on the awaited operation; shutdown does not wait for natural business completion or establish that a remote write did not occur. Complete announcements, ingress closure, player removal and saving before process stop.

After tasks and active stacks finish and every module reports `ready_to_shut()`, all `pre_shut()` calls run in reverse dependency order with every object alive, closing native callback sources. A second reverse pass runs `shut()` and destroys each owned module object before its providers. Plugin uninstallation follows all modules; all native libraries close last. Providers remain alive through consumer destruction. Hooks may block; handler budgets do not bound shutdown duration.

`manager.stop_until(timestamp, deadline)` accepts a `std::chrono::steady_clock::time_point`. The earliest supplied deadline persists; later unlimited `stop()` cannot clear it. No deadline is set by default. Checks occur at readiness and synchronous cleanup boundaries; they cannot interrupt blocking calls or undo completed actions. `get_shutdown_failure()` identifies phase/module; a plain timeout leaves `get_shutdown_exception()` empty. See [launcher timeout behavior](applications.md#check-startup-and-stop-the-process).

| State | Caller action |
| --- | --- |
| `running` | Update normally and accept tasks |
| `stopping` | Groups have requested cancellation; continue owner updates until frames, active stacks and native resources are safe |
| `timed_out` | Optional deadline expired before cleanup; continue owner updates without resuming cleanup |
| `cleanup_timed_out` | Deadline expired after cleanup began; retain remaining resources without further updates or cleanup |
| `failed` | Readiness, cleanup or native close failed; retain remaining resources without retrying later cleanup |
| `stopped` | Resource cleanup and native closes succeeded; repeated `stop()` returns `true` |

Inspect `get_shutdown_state()`, `get_shutdown_failure()` and `get_shutdown_exception()` to distinguish waiting from failure. There is no default business-drain deadline, thread detachment or pending-frame destruction to finish shutdown.

| `dynamic_library::unload()` result | Effect |
| --- | --- |
| Native close fails | Return `false`, keep handle; manager records `unload: <plugin>`, latches failure and retains the wrapper/remaining libraries |
| Native close succeeds | Clear handle |
| Empty/already closed wrapper | Return `true` without a native call |

Later `stop()` calls do not retry plugin exits/closes or roll back teardown. Close success does not establish OS image removal.

Use manager/groups on their constructing thread. In update/shutdown hooks or executor callbacks, do not mutate module registries or recursively update. Any pending managed task prevents every module's unregistration: it may borrow another provider. Destroying a manager with tasks, on an active callback stack or from another thread terminates; destruction cannot initiate resource shutdown.

<Aside type="caution" title="Task completion does not release native IO resources">

The task check covers only managed groups' accepted top-level tasks. Each module’s readiness and resource cleanup have their documented native scope; see the [Net HTTP client](../reference/network.md#http-client-resource-cleanup) and [HTTP server](../reference/http.md). Safe code unloading also requires external callbacks to return, pool workers to exit and background cleanup to finish; zero tasks proves none of these. Unmanaged work must not reference a module being destroyed.

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
| Standalone-group natural completion | `stop_accepting()`, then continue owner updates; keep services/bridges available and accepting further asynchronous operations |
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
