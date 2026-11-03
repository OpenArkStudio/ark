# Network plugin

The network plugin integrates the independent arknet library with ark's message framing, session IDs and Update-thread business callbacks.

| Transport | Client | Server |
| --- | --- | --- |
| TCP | `tcp_client` | `tcp_server` |
| WebSocket | `websocket_client` | `websocket_server` |
| RUDP | `rudp_client` | `rudp_server` |
| HTTP | `http_client_module` | `http_server` |

`net` defines start, shutdown, send and Update operations. Application messages retain their existing headers. RUDP uses arknet's compatible ARQ wire format with FEC disabled by default.

See [network builds and tests](../networking.md), [ark::net](https://github.com/OpenArkStudio/ark/blob/main/src/plugins/net/net.hpp) and the [arknet protocol guides](https://openarkstudio.github.io/arknet/features/).

The optional routed [HTTP server plugin](http.md) exposes `http_server_module`; its route handlers run on arknet IO workers.

## API and private construction

All Net source headers and implementations live in `src/plugins/net/`. The
`ark::net` API target maps 19 explicit public headers to `<ark/net/...>` and
propagates their public dependencies. Enable `ARK_BUILD_PROTO=ON` for the service
headers that include protocol messages; `ark::net` then carries `ark::proto`.
Direct endpoint headers remain usable with protocol generation disabled.
API targets do not build or load the runtime DSO.

| Private construction | Interface and behavior |
| --- | --- |
| Service manager | `std::unique_ptr<net_service_manager_module>` factory; cpp-local `module_base<net_service_manager_module>`, retaining the public RTTI key |
| HTTP client | `std::unique_ptr<http_client_module>` factory; `module_base<http_client_module>` defined only in `http_client_module_impl.cpp`, registered under its public key |

Private assembly uses `plugin_base`. Both modules update; HTTP installs first/uninstalls last. `ark_net_plugin` and `DllEntryPlugin`/`DllExitPlugin` signatures stay unchanged; arknet owns IO workers/transport.

**Header migration:** remove includes of
`<ark/net/net_service_manager_module_impl.hpp>`, `<ark/net/net_plugin.hpp>` and
`<ark/net/http_client_module_impl.hpp>`.
Load the plugin through the application manifest and obtain the registered
`net_service_manager_module` instead of constructing the manager implementation.
Use CMake's target-provided include roots instead of only `-I <source>/include`.
Installed consumers use `find_package(ark CONFIG REQUIRED COMPONENTS net)` and `ark::net`; see [installation](../get-started/installation.md#install-and-consume-the-cmake-package) for the separate arknet dependency and matching dependency settings.

**Breaking source/ABI change:** `http_client_module_impl` and its old public
header are removed from the source/build/install interfaces. Include
`<ark/net/http_client_module.hpp>` and resolve `http_client_module` from the plugin
manager instead of constructing the concrete client. The factory is internal
to Net and is not an application API. Rebuild Net and consumers together, using
a fresh install prefix so obsolete headers from older installations do not linger.
`net_client_service_impl.hpp`, `net_server_service_impl.hpp`, endpoint classes
and callback templates remain public. Request overloads, timeout defaults,
future results and owner-update callback dispatch are unchanged. The official
HTTP client participates in the resource-readiness and cleanup phases below.

## Service-manager required providers

`net_service_manager_module` requires the registered `bus_module` and
`timer_module` interfaces. The private implementation declares both through
`module_base`; these are module dependencies, independent of plugin load order.
The manager rejects a missing provider or a dependency cycle before any module
`init()` hook runs. A valid graph puts each provider before Net in `init()`,
`post_init()`, `check_config()` and `pre_update()`, so Net reads initialized Bus
configuration and registers its health timer after provider initialization.
Resource cleanup runs all consumer-first `pre_shut()` calls while all objects
are alive, then reverse `shut()`/owned-object destruction; all DSOs close last.

`check-net-health-timer` exercises the production private factory with controlled
Bus and Timer providers. It checks both required interface keys, missing-provider
and cycle rejection before module side effects, startup and resource-cleanup
ordering, and the actual health registration and retirement described below.

## Service-manager health timer

The service manager owns its 20-second health timer registration and fails
startup if Timer returns ID `0`. Normal `pre_shut()` retires that registration
while the Timer provider object is still alive. Repeated `pre_shut()` and
`shut()` succeed, including when the provider has already cleared its wheel;
explicit `shut()` also performs the same retirement for direct lifecycle callers.
Other timer registrations and shared IO keep their existing lifetimes.

Direct lifecycle callers must retire the health callback before destroying its
Net receiver. The destructor does not call a borrowed Timer provider that may
already have been destroyed. `check-net-health-timer` uses the production private
factory and service-manager implementation with a provider backed by the real
time wheel. It checks callback-storage release, unrelated timers, repeated
retirement and registration failure. This scope does not establish arbitrary
callback retirement or DSO unload safety.

## HTTP client resource cleanup

`http_client_module` does not classify business roots or continuations and does not grant request authority from the manager’s polling phase. Applications close their own business ingress before process stop. Future/callback requests retain ordinary behavior until resource cleanup.

`ready_to_shut()` waits for native requests to publish results, queued callbacks to be delivered, and active callbacks to return and release captures. Callback exceptions propagate to the owner; later queued results remain available to subsequent updates. Continue manager updates during shutdown; `pre_shut()` then closes/cancels the request runtime. Direct cleanup or destruction does not establish natural IO completion.

A timeout does not make callback receivers safe to destroy; retain the module, receivers and code. Request IO/connect timeout defaults remain 20/5 seconds; these are request settings, not process-shutdown budgets. See [task lifecycle](../guides/tasks.md#shared-business-executor-for-modules) for optional manager deadlines and retention.

`check-network` and `check-net-http-drain` use actual implementations with controlled loopback or real DSOs for resource completion/cleanup. Results must identify their tested revision; these targets do not establish external Etcd/Consul or arbitrary workflow acceptance. Consume DSO-created futures before unload; only owning result values may remain.

During Net installation, an independently owned HTTP state capsule is enrolled before the request pool starts. Failed-start cleanup joins only an idle pool with no accepted requests, queued results or active callback frames; otherwise it retains the complete state as unsafe. Candidate unwinding defers native close to that owner, while successful-start disarm preserves normal destruction. No ordinary update, callback cancellation or startup retry is introduced. See [startup-failure cleanup](../project/startup-failure-cleanup.md#quiet-net-http-client-cleanup) and `check-http-client-startup-cleanup`.

## Server and client service examples

`msg_header::max_body_size() const` defines the locally configured protocol's
maximum body length. The default remains 4096 bytes for native CS/SS headers.
Custom headers can override it and must preserve that policy in `create()` and
`clone()`. Receive parsing checks the configured prototype before message
allocation; a decoded wire header cannot negotiate a larger limit. TCP,
WebSocket and RUDP sends and broadcasts check the supplied header's limit and
guard total-size arithmetic. Receive-buffer initial capacity remains independent
of this message limit.

**Upgrade:** the additional virtual method changes the `msg_header` vtable ABI.
Rebuild Net, protocol-header implementations and every consumer/plugin together;
do not mix old and new binaries. Native CS/SS wire layouts and their 4096-byte
default are unchanged. Matching custom protocol limits remain application-owned.

The process must load Bus, Utility and Net and provide its Bus configuration.
Start listeners explicitly after startup; loading the plugin does not create
them. These functions use the public manager interface for both roles:

```cpp
#include <ark/net/net_service_manager_module.hpp>
#include <ark/plugin_manager.hpp>

bool start_listeners(ark::plugin_manager& manager) {
    return manager.find_module<ark::net_service_manager_module>()->create_server();
}

bool connect_tcp(ark::plugin_manager& manager, const ark::bus_addr& target,
    const std::string& host, uint16_t port) {
    return manager.find_module<ark::net_service_manager_module>()->create_client_service(
        target, ark::proto_type::tcp, host, port);
}
```

Connection requests retain their existing asynchronous behavior. Continue calling
`manager.update(timestamp)` on the owner thread; keep callback receivers alive
until delivery stops. Net also queues outgoing SS connections from Bus relations
at startup, restricted to configured target types in the same channel and zone.
Applications still start listeners explicitly. See [Bus](bus.md) for endpoint
validation, migration requirements and the bounded two-process restart test.

`check-net-plugin`, included in application-enabled `check-core`, loads actual
Bus/Utility/Net DSOs, checks both interface keys, TCP message and HTTP callbacks
on the manager thread, futures completing without manager updates, pending-request
cancellation through `pre_shut()`, normal repeated shutdown and listener release.
DSO-created futures are consumed while the implementation library is still loaded;
this does not establish their validity across arbitrary library unloading. The
independent `tests/consumer/net` project links only `ark::net`, compiles each
public header separately and rejects the removed implementation and private factory/assembly paths. Existing
`check-network` endpoint, framing, reconnect and shutdown cases remain applicable.
With Boost, `check-install` also compiles the public HTTP client interface through
the relocated `ark::net` package and rejects implementation headers in the install tree.

## Initial application receive buffer

TCP, WebSocket and RUDP endpoints use a 4 KiB initial application receive buffer and grow as needed. Call `set_initial_receive_buffer_size(bytes)` on the network object before `start_client` or `start_server`; `get_initial_receive_buffer_size()` reads the configured value. Zero or a change while the endpoint is working returns `false`; existing sessions retain their selected capacity.

This is the framing buffer for application messages, configured separately from kernel socket send/receive buffers. It is not a 4 KiB maximum message size. The [local receive-buffer report](../performance/receive-buffers.md) retains the allocation, fragmented-input and large-batch throughput tradeoff. It does not establish network RTT or connection capacity.

## Receive-buffer ownership

**Breaking source/layout change:** `<ark/buffer.hpp>` exposes a move-only `ark::buffer`. Copy construction and copy assignment are deleted to prevent two objects from deleting the same array. Rebuild the Core/Net consumers and their plugins together. The tracked Net sessions already prohibit copying and own their buffer in place; copying a `shared_ptr` session handle does not copy that buffer. This repository audit does not establish whether external consumers relied on the former implicit copy operations.

Move construction and move assignment are `noexcept` and transfer the allocation, unread bytes, capacity and cursor state. Assignment releases the destination's previous allocation; self-move leaves it unchanged. A moved-from buffer has zero capacity, zero unread bytes and a null data pointer; it can be destroyed, assigned or written to again. Existing public method signatures remain available. Standalone construction still defaults to 512 KiB, while Net sessions retain their separately configured 4 KiB default.

```cpp
ark::buffer pending(4096);
pending.write("abc", 3);
ark::buffer processing(std::move(pending)); // transfers ownership
pending.write("next", 4);                 // allocates fresh storage
```

Include `<utility>` for `std::move`. `get_data()` returns a borrowed pointer into the current allocation; keep its owner alive and reacquire the pointer after compaction, growth or ownership replacement. This change introduces no clone API. Applications that need independent data can explicitly write the readable bytes into another buffer while the source remains valid.

The buffer keeps `ARK_NEW_ARRAY` / `ARK_DELETE_ARRAY` as its narrow allocation compatibility boundary, now using the same pair for construction, growth and release. This corrects the previous growth path that bypassed custom hooks with raw `new[]` / `delete[]`. Define a compatible, nonthrowing release hook consistently across translation units before including the header. Without overrides, allocation still uses throwing `new[]`: `std::bad_alloc` propagates and failed growth retains the old bytes, capacity and cursors. If a custom allocator returns null, construction retains an empty zero-capacity buffer and `write()` returns `false` without changing the existing state.

Both `net_session::add_buffer()` overloads return `-1` when a write is rejected,
retaining the unread bytes. TCP, WebSocket and RUDP receivers stop the affected
transport instead of assembling later input across a missing fragment. On
success the return value is the unread length capped at `INT_MAX`; use
`get_buffer_len()` for the exact `size_t` count. Throwing allocation failures
still propagate. Public signatures, class layouts, wire bytes and body limits
are unchanged.

A zero-length write succeeds without dereferencing its pointer; a null source with nonzero length returns `false`. Removing more than the unread length is a no-op, including an overflowing count. Unrepresentable growth throws `std::length_error` before allocation. Normal reads, writes, compaction and capacity growth retain their existing behavior.

With `ARK_BUILD_CORE_TESTS=ON`, `check-buffer` runs the lifetime/move, compaction/growth, boundary, deterministic default-allocation failure and separate custom-hook regressions; it is also part of `check-core`. The allocation-failure test fails one small array request instead of requesting excessive memory. `check-network` retains the existing Net framing/fragmented-input coverage. These checks do not establish service or capacity acceptance.

## Message and event ownership

`net_msg` owns its header and body. Storage capacity is independent of the declared body length; callback pointers remain borrowed.

| API | Behavior and failure |
| --- | --- |
| `net_msg::alloc_msg(head)` | Always consumes the heap-owned header. Null header or object/body allocation failure returns null; check before use. Receive parsing returns `false` on header/message allocation failure without queuing an incomplete message. |
| Native CS/SS `create()` / `clone()`, `net_event::alloc_event()` | Allocation failure returns null. |
| Message/event `release(pointer)` | Destroys and nulls the caller’s pointer; null is safe. |
| `copy_data(data, length)` | Requires `length` readable input bytes; overlap is supported. Grows before changing length. Zero accepts null and clears length; nonzero null throws `std::invalid_argument`. |
| `set_msg_length(length)` | Grows storage as needed, preserving the old prefix. Newly exposed bytes are uninitialized; initialize before use. |
| `copy_from(source)` | Deep-copies header/body; self-copy is unchanged. Null source throws `std::invalid_argument`; null clone throws `std::bad_alloc`; custom `clone()` exceptions propagate. Declared source length above capacity throws `std::length_error`. |
| `get_head()` / `get_msg_data()` | Direct header-length edits do not resize storage: use `set_msg_length()` or `copy_data()`. The body pointer is borrowed; keep its owner alive, reacquire after growth/copy replacement, and discard on release. |

Growth/copy allocation failure throws `std::bad_alloc`; allocation and clone failures preserve the destination’s header, bytes and length.

**Upgrade:** capacity tracking changes `net_msg` layout; rebuild Net and consumers/plugins together. These message/event and native-header factories use standard allocation; `ARK_NEW` / `ARK_DELETE` overrides no longer customize them. Public signatures, CS/SS wire bytes and platform types remain unchanged.

With `ARK_BUILD_NETWORK_TESTS=ON`, `check-net-message` covers ownership, framing and deterministic allocation failures without sockets, including null-returning receive hooks and failed-fragment reporting; `check-network` includes it. Local checks do not establish shared-service or capacity acceptance.

## Application ingress and resource closure

**Migration:** `<ark/net/external_root_callback.hpp>` and `reg_external_root_callback()` are removed. Register ordinary `reg_msg_callback` handlers; any application business-refusal policy runs before process stop. The framework does not classify or reject TCP/WebSocket/RUDP business roots from manager state. Heartbeat, control and completion callbacks retain ordinary behavior.

Net owners close service endpoints during cleanup, then clear borrowed callbacks and their captures. Retain receivers, providers and implementation DSOs until callback stacks return and native closure finishes; clean up consumers before destroying providers. Message pointers remain borrowed for the invocation only. This resource cleanup adds no wire-format or business-saving guarantee.

## Member callbacks and borrowed arguments

Endpoint constructors and client/server service registration bind the supplied
member function to the same non-owning receiver pointer with C++20
`std::bind_front`. Registration does not invoke the callback or extend the
receiver's lifetime; keep that object alive until its callbacks can no longer run.
Public callback signatures and Update-thread dispatch remain unchanged.

Message and event pointers borrow the original objects for that invocation.
The WebSocket client's `const conv_id_t&` parameter also refers to an invocation
argument; copy its value instead of retaining the reference. The server service's
forward callback retains its existing `int` parameter and conversion from
`conv_id_t`; this change does not expand that parameter's range.

`check-core` with `ARK_BUILD_PROTO=ON` exercises the public service-registration
helpers (their headers already depend on Protobuf). `check-network` exercises
actual endpoint delivery, including the WebSocket reference parameter and owner
Update thread. These checks do not establish capacity or a new ownership model.
