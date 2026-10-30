# Create a module

A module exposes a C++ service interface; its plugin registers the implementation and the manager drives its lifecycle. This counter and consumer run on one application thread. Pair them with [Create a plugin](plugins.md) to build the shared library.

## Define the interface and implementation

Save the public interfaces as `sample_module.hpp`, without concrete classes:

```cpp
#pragma once

#include <cstdint>

#include <ark/module.hpp>

namespace ark {

class counter_module : public module
{
public:
    virtual void increment() = 0;
    virtual std::uint64_t value() const = 0;
};

class sample_module : public module
{
};

} // namespace ark
```

Save factory declarations in plugin-private `sample_factory.hpp`; do not install it or expose it to interface consumers:

```cpp
#pragma once

#include <memory>

#include "sample_module.hpp"

namespace ark {

std::unique_ptr<counter_module> make_counter();
std::unique_ptr<sample_module> make_sample();

} // namespace ark
```

Define concrete classes and interface-returning factories in adjacent `sample_module.cpp`:

```cpp
#include <ark/module_base.hpp>

#include "sample_factory.hpp"

namespace ark {
namespace {

class counter_module_impl final : public module_base<counter_module>
{
public:
    void increment() override {
        ++value_;
    }

    std::uint64_t value() const override {
        return value_;
    }

private:
    std::uint64_t value_{};
};

class sample_module_impl final : public module_base<sample_module, counter_module>
{
public:
    bool post_init() override {
        counter_ = find_module<counter_module>();
        return counter_ != nullptr;
    }

    bool update() override {
        if (counter_ == nullptr)
        {
            return false;
        }
        counter_->increment();
        return true;
    }

    bool pre_shut() override {
        counter_ = nullptr;
        return true;
    }

private:
    counter_module* counter_{};
};

} // namespace

std::unique_ptr<counter_module> make_counter() {
    return std::make_unique<counter_module_impl>();
}

std::unique_ptr<sample_module> make_sample() {
    return std::make_unique<sample_module_impl>();
}

} // namespace ark
```

`module_base<interface_type, providers...>` supplies manager/name access, required-provider metadata and protected `find_module<T>()`, without adding lifecycle or `update()` overrides. The inheritance chain is implementation → base → interface → `module`. The consumer declares `counter_module` as required; [plugin registration](plugins.md#pair-registration-and-unregistration) explicitly enables its updates and disables the counter’s. Co-locate hpp/cpp by function; consumers need only the interface.

## Register and find the same interface

Register `counter_module_impl` under `counter_module`, then retrieve it with `find_module<counter_module>()`. The registration key comes from the interface type's RTTI name; an implementation name or a shared-library filename is a different key. Register one implementation per interface in a manager. Duplicate registration is not a supported multi-provider model.

Returned pointers are borrowed: do not delete them or retain them after module unregistration. The manager refuses any module removal while managed business tasks remain, since another module's task may borrow that provider. Build consumers/plugins with compatible headers, compiler/ABI settings and RTTI enabled.

| Lookup | Missing interface RTTI key or runtime-type mismatch | Calling constraints |
| --- | --- | --- |
| `find_module<T>()` | Asserts with assertions enabled; returns `nullptr` otherwise | Check before use; do not probe optional services through failed lookup in Debug; existing assertion contract unchanged |
| `plugin_manager::try_find_module<T>()` | Returns `nullptr` without logging, asserting or changing registration | Manager owner thread only; foreign-thread calls throw `std::logic_error`. Server startup uses it to report missing required modules through normal startup failure |

The two example modules share the application update thread. The counter is deliberately not atomic; it does not support calls from arbitrary worker threads. Queue worker completions for the owning module thread or use a service's documented asynchronous API. See the [network reference](../reference/network.md) for network callback dispatch.

## Declare startup dependencies

Use ordinary C++ template parameters to declare required providers. For example, the Timers sample uses the same form as:

```cpp
#include <ark/module_base.hpp>
#include <ark/utility/guid_module.hpp>
#include <ark/utility/timer_module.hpp>

class dependent_module final
    : public ark::module_base<ark::module, ark::timer_module, ark::guid_module>
{
};
```

| Dependency declaration | Contract |
| --- | --- |
| `module_base<interface_type, providers...>::get_dependencies()` | Returns provider interface RTTI keys; the first parameter selects the inherited interface. Providers add metadata, not bases or owned pointers |
| `module::get_dependencies()` | Empty by default; legacy implementations may override required keys directly. Do not mutate the registry while reading metadata |
| Validation | Repeated keys count once; self-dependency is a cycle. Optional lookups stay nullable, not required. Only key presence is checked; raw `add_module()` callers must supply the matching interface |
| Cache | Read once on the owner after installation. Legal idle additions/removals invalidate order; first admission-stop rebuilds when needed. Declarations stay stable while registered; polling/reentrant stop never reread them |
| Ordering | Stable registration-key order among ready nodes within compatible builds, not across compiler RTTI encodings |

Timers looks up Timer/Guid in `init()`; Utility’s Timer, Guid, Schedule and Compress require no providers. Other applications retain existing declarations/checks, without a complete callback-lifetime graph.

**Upgrade:** metadata and `stop_accepting()` change `module`’s vtable; cached order changes manager layout, and Timers changes its public base specialization. Rebuild the launcher, all official/business plugins and affected consumers together with compatible headers/compiler settings; do not mix older binaries. Lookup keys stay unchanged; no cross-compiler ABI is promised. Dependencies govern startup and reverse admission-stop hooks, not update, readiness, `pre_shut()`, `shut()` or unloading. Official ingress migration, reverse complete cleanup and opt-in startup-failure compensation remain on the [subsequent route](../project/lifecycle-closeout.md).

## Choose lifecycle stages

| Callback | Appropriate work | Current boundary |
| --- | --- | --- |
| `init()` | Initialize local resources | Declared providers have completed this phase; independent modules may follow |
| `post_init()` | Resolve interfaces and use dependencies initialized in `init()` | Declared providers also finish `post_init()` before their consumers |
| `check_config()` | Check configuration and dependencies | A failure stops the remaining startup work |
| `pre_update()` | Prepare work before the application loop | Keep this stage bounded |
| `update()` | Publish queued service and bridge completions | Only explicitly enabled registrations participate; the manager then polls its shared executor once. Its Boolean result does not stop updates |
| `stop_accepting()` | Close external business-root admission | Consumer-first once, before task drain; false/exception retains resources |
| `pre_shut()` | Release subscriptions and prepare resource cleanup | Entered after managed tasks finish and the active update/completion stack returns; the first `false` or exception stops shutdown |
| `shut()` | Release module-owned resources | Entered only if every `pre_shut()` succeeds; the first `false` or exception stops remaining callbacks and prevents further unloading |

![Successful module lifecycle](../assets/module-lifecycle.svg)

All four startup phases reuse validated provider-first order; independent ready nodes use registration-key order, not manifest/insertion order. Base callbacks return `true`; override only what is needed. This plugin explicitly disables counter updates and enables consumer updates, incrementing the counter once per manager update.

<Aside type="caution" title="Handle the manager's startup result">

| Startup failure | Result and diagnostic |
| --- | --- |
| Missing provider / cycle | `start()` returns false before hooks; `dependencies: <consumer> requires missing module '<provider>'`, or a blocked-module list including downstream nodes outside the cycle |
| Metadata exception | Propagates unchanged with `dependencies: <module>` |
| Any of four startup callbacks returns `false` / throws | Stops current and later stages. False makes `plugin_manager::start()` return `false`; exceptions propagate unchanged. Each callback runs at most once per stage; context is `phase: module` |
| Missing library / `DllEntryPlugin` | Returns false with `load_plugin: <plugin>`; library load/entry calls use `load_plugin: plugin`. A loaded library missing its entry stays retained until process exit because static initialization may have started work; no automatic unload |
| `get_startup_failure()` | Owner-thread reads only; empty after successful startup |

Installation/registration and completed stages are not rolled back. The launcher reports context/available exception details and exits `1` before updates or the Windows command thread, without `pre_shut()` / `shut()` or same-process retry. Deployment tools should start a fresh process. Other invariant assertions are unchanged. Custom loops must select all required plugins and check startup results; see the [launcher failure contract](applications.md#check-startup-and-stop-the-process).

</Aside>

## Stop new business roots

`bool stop_accepting()` defaults to `true` for source compatibility. Close only the external business-root gate; accepted workflows still need provider requests, completions and bridges. Do not seal a provider or call its `close()` / `stop()` merely because the operation has a similar name.

| Stage / boundary | Behavior |
| --- | --- |
| `stopping_acceptance` | First `manager.stop()` seals all managed groups. Active update/poll/completion returns false and defers hooks to an outer owner-frame `stop()`. Before task waits, hooks run once in reverse cached topology, reversing registration-key ties |
| Cold / invalidated cache | A never-started manual manager or invalidated graph rebuilds/validates once under the lifecycle guard before hooks, including same-key replacement; no stale pointers. No metadata reread during polling or after hooks begin |
| Hard failure `failed` | First hook false/exception or invalid rebuilt graph retains `stop_accepting: <module>` and exception details; stops remaining hooks, updates and teardown, pinning accepted frames/resources. Gates stay closed; repeated stop never retries. False is not a readiness wait |
| `draining` | After all hooks succeed, enabled updates/shared owner poll finish accepted tasks and active stacks, then all-module readiness and synchronous teardown. Task services/bridges stay open; no reordering of `pre_shut()`, `shut()`, plugin exits or library closes, and no startup-failure compensation |
| Reentrancy | Reentrant stop returns false; recursive update/start and registry mutation are rejected before effects |
| `timed_out` / `cleanup_timed_out` | `stop_until(timestamp, deadline)` checks after admission/readiness hooks and before cleanup. Before cleanup, expiry retains resources with updates allowed; after cleanup begins, remaining resources stay retained with updates disabled. Later `stop()` cannot bypass expiry |

Hooks/unloading are synchronous and can block; the poll’s handler-count budget does not bound hook duration. Checks at hook/close returns cannot preempt or undo actions. Failed hooks report phase/module, retain launcher/resources and are not retried automatically. See [timeout states](tasks.md#shared-business-executor-for-modules).

**Migration boundary:** the official HTTP server closes per-request admission and drains accepted response sends. Net provides the opt-in [external-root callback helper](../reference/network.md#explicit-external-root-registration), with an explicit application-owned rejection handler. Ordinary registrations and guest-room behavior are unchanged; remaining TCP/WebSocket/RUDP business entry points require individual migration. Admission at business callback entry does not pause transport-level accept. Stopping a transport or manipulating its acceptor is not a substitute for preserving accepted workflows. See [HTTP shutdown](../reference/http.md#route-requests).

## Keep callbacks within their owner's lifetime

The example only holds a borrowed pointer and clears it in `pre_shut()`. A real module may also register timers, event handlers or asynchronous operations that capture `this`. Record their identifiers or cancellation handles, stop accepting new work, and unregister or cancel them according to the owning service's API before destroying the module. Cancellation alone must not be assumed to mean that every completion has already run.

The manager tracks roots accepted by its task groups, but not arbitrary callbacks, native pool workers or background cleanup. Those operations must finish according to their own service contracts before their code or state can be released. Do not infer safe library replacement or a complete callback-drain guarantee from a zero managed-task count, `pre_shut()`, `shut()` or a successful `stop()` return. See [shared module tasks](tasks.md#shared-business-executor-for-modules) for the admission and drain contract. Hot-update behavior is outside this guide.

## Continue

<CardGrid>

<LinkCard title="Create a plugin" href="plugins.md" description="Registration pairs, DLL entry points, build and runtime selection." />
<LinkCard title="Application launcher" href="applications.md" description="Process arguments and configuration paths." />
<LinkCard title="Coroutine tasks" href="tasks.md" description="Task ownership, admission and owner-thread polling." />
<LinkCard title="Kernel reference" href="../reference/kernel.md" description="Entity and data services from official modules." />

</CardGrid>

## Use a base class with a private implementation

The [complete example above](#define-the-interface-and-implementation) is the recommended recipe. Existing `ARK_DECLARE_MODULE_FUNCTIONS` modules remain supported. Preserve interface RTTI keys and update selection when following the [plugin compatibility table](plugins.md#use-typed-registration-and-explicit-entries). Return `std::unique_ptr<interface_type>` from a private factory, keep concrete classes in cpp and include the base only in the implementation.
