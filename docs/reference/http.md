# HTTP server plugin

The optional HTTP plugin exposes `http_server_module` and arknet's public
`http_router` type. It starts an HTTP/1 listener explicitly and routes requests on
arknet IO workers. The Net plugin's HTTP client remains a separate interface.

## Build and configure

Enable `ARK_BUILD_APPLICATIONS=ON` and `ARK_BUILD_PLUGIN_HTTP=ON`; applications use
Boost.Asio and Boost.Beast. Deploy `ark_http_plugin` with the usual platform suffix
and Debug `_d`. Select the plugin and its configuration in the application manifest:

```toml
[[plugins]]
name = "ark_http_plugin"
config = "http.plugin.toml"
```

The configuration path resolves against the selected plugin configuration directory.
The shipped `config/http.plugin.toml` contains:

```toml
[http]
host = "0.0.0.0"
port = 9090
threads = 4
```

| Required field | TOML constraint |
| --- | --- |
| `host` | Nonempty string without NUL |
| `port` | Integer `0..65535` |
| `threads` | Integer `1..255` |

Unknown/missing keys, wrong types and out-of-range values fail `init()` with file/field diagnostics; syntax errors add line/column. Renaming XML does not convert it: see [plugin configuration](../service-manifests.md#http-and-consul-plugin-settings). Initialization parses without listening.

<Aside type="note" title="Configuration versus overload defaults">

`start_server()` uses configured values. Explicit-overload defaults `port = 8080`, `thread_num = 1` are not TOML defaults. Port zero requests an OS-assigned port; the module does not expose it, so configure a known port when clients need discovery.

</Aside>

## Route requests

After startup, obtain the required registered interface and register routes before
starting the listener:

```cpp
#include <ark/http/http_server_module.hpp>
#include <ark/plugin_manager.hpp>

bool listen(ark::plugin_manager& manager) {
    auto* server = manager.find_module<ark::http_server_module>();
    server->get_router().add(arknet::http::verb::get, "/users/:id",
        [](const auto& context, auto& response) {
            response.set(arknet::http::field::content_type, "text/plain");
            response.body() = "user:" + std::string(context.param("id"));
        });
    return server->start_server();
}
```

The router reference is borrowed from the module; `get_router()` returns the same
owned router each time. Route context/response arguments belong to that invocation.
Keep handler state alive and synchronize shared business state accessed by IO
workers. Routing does not wait for `manager.update()` or move handlers to the
manager's thread. `make_200<response_body_type>()` retains the public response
helper, including content type, length and keep-alive behavior.

| Listener / shutdown boundary | Behavior |
| --- | --- |
| Endpoint reservation | Rejects start while running; two instances in one library cannot reserve one nonzero endpoint. `pre_shut()` stops/releases server and reservation, retaining restart behavior when used alone |
| Ordinary requests | No manager-state business gate or automatic HTTP 503; routes retain ordinary behavior until the resource-readiness seal; later transport requests close without invoking routes |
| `ready_to_shut()` | Atomically seals native route dispatch, then waits for active route/response-send leases to retire; failed sends also retire leases without proving client receipt |
| Resource closure | `pre_shut()` stops/joins the native server, clears router captures and releases its endpoint; blocked routes must return cooperatively. Independently scheduled work needs separate ownership |

No generic hot replacement or HTTP/2 support; arknet retains IO workers, request parsing and transport.

## Public interfaces and header mapping

The public header lives in `src/plugins/http/http_server_module.hpp`. An explicit
CMake mapping through `ark::http` retains `<ark/http/http_server_module.hpp>`,
including when runtime plugins and Protobuf are disabled. The concrete class
lives in cpp, inherits `module_base<http_server_module>` and is constructed by a
private interface-returning factory. Private plugin assembly uses `plugin_base`
and explicit `DllEntryPlugin` / `DllExitPlugin` functions. The public virtual and
router types, RTTI key, library stem and entry signatures are unchanged. The
module does not register an update callback, matching its inherited default.

**Header migration:** replace `<ark/http/http_server_module_impl.hpp>` and
`<ark/http/http_plugin.hpp>` includes with the public module header and obtain the
loaded interface from `plugin_manager`. Use the `ark::http` target's include roots
instead of only `-I <source>/include`. Private assembly/parser headers are not
mapped. The generated mapping is an include view of the co-located source header.

## Validation

`check-http-plugin` uses the actual HTTP DSO and loopback requests for public interfaces, routing, IO-worker dispatch, resource readiness, repeated closure and endpoint reuse. `check-http-resource-cleanup` checks resource waiting/retention without a manager-state business-refusal gate. Acceptance requires actual results for the tested revision; earlier HTTP 503 cases are not current-contract evidence. Independently scheduled route work is outside HTTP send-lease accounting.

`check-network` suite retains its HTTP duplicate binding,
restart, public response helper and invalid-TOML assertions through the private
factory. `tests/consumer/http` links only `ark::http`, compiles the public header
separately and rejects private header paths with apps/Protobuf disabled. See
[directory structure](directory-structure.md) for the public-header consumer command, and
[arknet HTTP](https://openarkstudio.github.io/arknet/features/) for routing APIs.

## Legacy HTTP example construction

`examples/http_server` and `examples/http_client` remain excluded from default
example builds. Their adjacent sample interface headers support typed lookup;
concrete classes are declared only in cpp and inherit `module_base` of the sample
interface. Private factories return owned interfaces to `plugin_base`, which
exports explicit `DllEntryPlugin` / `DllExitPlugin` entries and registers both
samples with `module_update::disabled`, preserving their inherited update behavior.
The server sample uses `http_server_sample_module` to avoid its old collision with
the public `http_server_module` service. Library stems remain `http_server_plugin`
and `http_client_sample_plugin`; installed public headers remain unchanged.
Rebuild the example DSOs and their host together. Source code that instantiated
an example implementation must instead load the plugin and look up its sample
interface; the server sample lookup now uses `http_server_sample_module`.

The server still requests `0.0.0.0:8080` with four IO threads and returns success
from `post_init()` after calling `start_server()`, including when that call rejects
the listener. The client still performs a future POST and a callback POST to
`172.20.3.74:2379/v3/kv/put` with the original JSON body and HTTP timeout defaults
(20 seconds IO, 5 seconds connect), then grants a 100-second lease through an etcd
client using the existing 5/2-second timeout defaults. A null dependency or client
result makes `init()` return `false` before dereferencing it; Debug builds retain
the manager's assertions for missing module lookup. These
fixed endpoints remain demonstrations that need an appropriate external setup.
IO, transport and request completion belong to arknet; callbacks keep the public
HTTP client's manager-update contract.

Core-test configurations default `ARK_BUILD_HTTP_EXAMPLE_TESTS` to `ON` when
Boost.Asio is selected and `ark::http`, `ark::net` and `ark::etcd_client` API
targets exist; otherwise the default is `OFF`. Explicit `OFF` remains supported.
Existing Applications CI satisfies those prerequisites and runs the fixture via
`check-core`; the default application-example selection stays unchanged.
For an isolated construction regression, configure with
`-DASIO_STANDALONE=OFF -DARK_BUILD_CORE_TESTS=ON -DARK_BUILD_HTTP_EXAMPLE_TESTS=ON`,
then run `cmake --build core-build --target check-http-examples --parallel 2`.
The test target compiles both actual example modules and DSOs, calls their
factories, loads their exported entries and verifies interface identity,
configuration selection, POST/lease arguments and timeouts, owner-update callback
delivery, listener arguments, lease-owner destruction, repeated stop and reload.
Public service doubles make these checks deterministic without listening or
contacting etcd. They establish example construction and call contracts; real HTTP
wire coverage remains in `check-http-plugin` / `check-network`, and this fixture
does not establish external etcd acceptance.
