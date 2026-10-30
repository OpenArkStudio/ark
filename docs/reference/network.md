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
HTTP client participates in the admission and drain phases described below.

## HTTP client admission and drain

`http_client_module` overrides `stop_accepting()` to close new request roots without cancelling accepted HTTP IO; repeated calls succeed.

| New root after closure | Rejection delivery |
| --- | --- |
| Future `get`, `post`, `async_request` | `{false, operation_aborted.message()}`, immediately ready |
| Both callback `post` overloads | Same rejection, delivered by owner `update()`; rejected callbacks grant no continuation permission during drain |

The untagged request interface recognizes two bounded continuation scopes: a
synchronous call inside this client's accepted HTTP callback, and execution in
the associated manager's central task-poll phase during owner-thread
`manager.update()`. The manager's `is_polling_tasks()` reads that object's phase;
it does not depend on an Asio context marker local to the calling shared library. This preserves
Etcd's callback retry and provider requests initiated by previously accepted
managed tasks. Merely running a module's `update()` does not grant admission.
Calls deferred outside these scopes to another thread or an external scheduler
carry no accepted-work identity and are rejected after admission closes. The
interface does not infer arbitrary application workflow ownership. The shared
executor is a trusted continuation scope: application ingress must not post new
roots onto it during drain. Keep lifecycle and callback dispatch on the module's
owner thread, and close application ingress before draining tasks.

**Migration:** direct `task_group::poll()` and retained `manual_runtime::poll()`
still execute handlers and retain their existing teardown/reentry guards, but no
longer grant HTTP request continuation permission after admission closes. Drive
managed tasks through `manager.update()` instead. Its module-update portion does
not grant this permission; only the central task poll does, including task
completion handlers. The phase is restored on normal return and exceptions.
Rebuild the host and all plugins against the updated manager headers together.
This changes admission behavior without adding virtual slots, object data or a
wire-format change.

`ready_to_shut()` waits for every accepted native request to publish its result,
all queued callback results to be delivered, and active callback invocations to
return and release their captures. Callback exceptions propagate to the owner;
the active scope is restored and later queued results remain available to a
subsequent update. Continue manager updates while shutdown is draining. Only then
can normal manager shutdown call `pre_shut()`, which retains its existing request
cancellation and IO-runtime stop behavior. Direct `pre_shut()` or destruction
still cancels pending IO; it is not a graceful-drain shortcut.

Manager deadlines and failure states retain their existing semantics. A drain
timeout does not cancel HTTP IO or make callback receivers safe to destroy;
continue owner updates while retaining the module, receivers and implementation
library. A failed manager does not resume updates automatically. Request IO and
connect timeout defaults remain 20 and 5 seconds. This integration covers the Net
HTTP client, not TCP/WebSocket/RUDP ingress or the separate HTTP server plugin.

`check-network` exercises the production factory, active request completion,
rejection through all request overloads, callback and managed-task follow-ups,
manager drain ordering, repeated stop, callback exceptions and capture release
using loopback HTTP. These local checks do not establish external Etcd/Consul
acceptance or arbitrary application continuation safety.

Application-enabled `check-core` also runs `check-net-http-drain`, which loads
actual Net, Bus and Utility DSOs. Its host-side managed coroutine suspends before
admission stop, then resumes on the shared owner task executor and calls the
loaded HTTP interface. Controlled loopback responses check the accepted request,
rejection of a fresh external root, a callback retry during drain, binary owning
results, receiver release and normal manager teardown/unload. The host does not
compile the private HTTP implementation or substitute a client. DSO-created
futures are consumed before unload; only their owning result values are retained.
This test covers the shared-executor boundary in the configured host/DSO toolchain;
it does not establish arbitrary deferred workflow identity or external services.

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

With `ARK_BUILD_NETWORK_TESTS=ON`, `check-net-message` covers ownership, framing and deterministic allocation failures without sockets; `check-network` includes it. Local checks do not establish shared-service or capacity acceptance.

## Explicit external-root registration

Include `<ark/net/external_root_callback.hpp>` and use
`reg_external_root_callback(server, manager, id, on_accept, on_reject)` only for
callbacks that begin new external business workflows. Both handlers are required;
an empty handler returns `false` before service registration. Duplicate IDs retain
the service's existing `false` result and original callback.

```cpp
#include <utility>
#include <ark/net/external_root_callback.hpp>

bool register_request(ark::net_server_service& server, ark::plugin_manager& manager,
    ark::net_msg_functor handle_request, unsigned& rejected) {
    return ark::reg_external_root_callback(server, manager, 45001,
        std::move(handle_request),
        [&rejected](const ark::net_msg*, ark::conv_id_t) { ++rejected; });
}
```

This example explicitly records refusal locally. Applications supply their own
rejection handler; the helper adds no error message, wire format, session close
or output discard policy. Keep the counter and other borrowed state alive.

The wrapper checks the manager state at owner-thread callback entry: `running`
invokes `on_accept`; every other state invokes `on_reject`. Admission is not at
socket receipt or framing: a queued frame that has not entered its business
callback can still be refused. An accepted handler can request `manager.stop()`;
that handler continues while later root entries are refused. Existing
`reg_msg_callback` registrations, completion/control callbacks, registration and
heartbeat processing, sends and provider operations retain their behavior.
Do not classify roots from the transport, CS/SS header, Bus ID or connection role.
For an ID mixing roots and completions, classify inside the business handler
instead of wrapping the entire ID.

Continue owner updates during `draining` and pre-cleanup `timed_out` to deliver
accepted workflows' results. `failed`, `stopped` and `cleanup_timed_out` managers
do not update; the predicate is not a promise of delivery in those states.

Register and dispatch on the manager owner thread. The helper borrows the manager;
the manager and every captured receiver must outlive the service's callback
registration. It supplies no unregister, independent module-removal or library
unload guarantee. Message pointers remain borrowed for that invocation; avoid a
strong service capture creating a service/callback ownership cycle. Handler
exceptions follow the existing callback path; provide a nonthrowing rejection
handler. This header-only addition changes no service virtual table, class layout
or wire format. The existing guest-room and unmarked business registrations are
not automatically migrated.

`check-net-external-root` (Core tests plus Protobuf) exercises the registered
wrapper with a contract recorder across running, stopping, draining, timed-out,
failed and stopped states, invalid/duplicate registration and reentrant stop.
`check-net-plugin` additionally loads real Bus/Utility/Net DSOs and verifies TCP
root acceptance, refusal during drain and continuing ordinary callbacks on the
same session. Recorder cases are not transport proof; neither check establishes
production capacity or complete application migration.

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
