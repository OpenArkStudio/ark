# Official plugins and repository organization

Core headers and build definitions live in `src/core/`, with the launcher in `src/application/`. Application manifests and shared configuration reading live in `src/config/`. All official plugins co-locate interface headers, implementation and private assembly in `src/plugins/<name>/`, retaining logical include paths through explicit CMake mapping. Business plugins live with their applications and arknet remains independent; this layout preserves plugin lifecycle and hot-update behavior.

## Existing implementations

| Repository | Related code in ark | Proposed destination | Integration work |
| --- | --- | --- | --- |
| [MySQLPlugin](https://github.com/OpenArkStudio/MySQLPlugin) | `src/plugins/mysql` | `src/plugins/mysql` | [Native coroutine API](../reference/mysql.md), prepared statements, transactions and shared business execution; validate deployment against a real MySQL server |
| [RedisPlugin](https://github.com/OpenArkStudio/RedisPlugin) | `src/plugins/redis` | `src/plugins/redis` | [Native coroutine API](../reference/redis.md), named pools, RESP/TLS and graceful shutdown; validate deployment against a real Redis server |
| [URLPlugin](https://github.com/OpenArkStudio/URLPlugin) | Net HTTP client and HTTP server plugin | `src/plugins/http` | Compare libcurl requests, encoding and actor integration; unify the HTTP service interface |
| [LuaScriptPlugin](https://github.com/OpenArkStudio/LuaScriptPlugin) | Integration with the new plugin interface required | `src/plugins/lua` | Define bindings and script lifecycle; replace old NFComm interface dependencies |
| [ActorPlugin](https://github.com/OpenArkStudio/ActorPlugin) | Integration with the new plugin interface required | `src/plugins/actor` | Define task scheduling, message delivery and business-thread responsibilities |

These repositories use different generations of interfaces and build systems. Compare features, overlapping implementations and dependencies during integration. MySQL and Redis clients belong in arknet; discuss other plugin dependencies separately.

## Directory layout

<FileTree>

- ark/ — Framework repository
  - src/ — Framework implementation
    - core/ — Module lifecycle and Entity foundations
      - ...
    - config/ — Shared TOML configuration reader
      - ...
    - application/ — Application launcher
      - ...
    - plugins/ — Built-in plugin implementations
      - kernel/ — Entity metadata, state and JSON loading
        - ...
      - net/ — Public headers, transports and private creation
        - ...
      - bus/ — Public module interfaces and private creation
        - ...
      - utility/ — GUIDs, timers, scheduling and compression
        - ...
      - http/ — Public routing interface and private creation
        - ...
      - mysql/ — MySQL coroutine access plugin
        - ...
      - redis/ — Redis coroutine access plugin
        - ...
    - proto/ — Protobuf schemas and code generation
      - ...
    - server/ — Built-in server roles
      - ...
  - examples/ — Example applications and business plugins
    - ...
  - tests/ — Core and cross-plugin integration tests
    - ...
  - benchmarks/ — Standalone C++ benchmark programs
    - ...
  - tools/ — Native JSON exporter and development tools
    - ...
  - cmake/ — Target, header-mapping and package helpers
    - ...
  - docs/ — English/Chinese documentation and Nimbus site
    - ...

</FileTree>

This shows the main directories. Existing Consul, etcd, Kafka and other official plugins follow the same convention. Lua and Actor integration remains planned; the table above identifies their proposed implementation directories.

MySQL, Redis, Utility, Net, Bus, HTTP and Kernel use a co-located layout; the database example is:

<FileTree>

- src/plugins/mysql/ — MySQL coroutine access plugin
  - CMakeLists.txt — Plugin build target
  - mysql_module.hpp — Explicit public API, mapped as ark/mysql/mysql_module.hpp
  - mysql_module.cpp — Hidden implementation class and one private factory
  - mysql_plugin.hpp — Private plugin type/factory declarations, excluded from public header mapping
  - mysql_plugin.cpp — Registration and explicit dynamic-library entry points
  - detail/ — Private native services, coroutine bridge and transactions
    - ...
- tests/database/ — Protocol, DSO and explicit real-service checks
  - ...

</FileTree>

Usage documentation belongs in `docs/`, and example applications in `examples/`. Define public interfaces around capabilities; keep database client types within implementation code where possible.

## Core and plugin boundaries

| Component | Responsibility | Build and release |
| --- | --- | --- |
| Core runtime | Plugin/module registration, dependency ordering and lifecycle | Validate interface compatibility together with official plugins |
| Official plugins | Optional networking, storage, scripting and discovery services | Independent CMake targets, enabled and packaged as needed |
| Business plugins | Product rules and business message handlers | Built and released in application repositories |
| arknet | IO execution, network endpoints, transport protocols and database clients | Independently versioned; ark pins the dependency |

Resolve plugin-specific dependencies when the plugin is enabled. Core public headers should avoid including MySQL, Redis or Lua client headers. Distinguish build dependencies from runtime service requirements: Redis integration tests also need an available Redis service.

## Database client responsibilities

Implement MySQL and Redis clients within arknet rather than adopting Boost.MySQL or Boost.Redis. arknet owns database protocol encoding/decoding, authentication and asynchronous communication. The ark plugins integrate framework configuration, module services and business callbacks.

The arknet single-node scope includes MySQL queries, parameterized prepared statements and transactions; Redis generic commands, RESP2/RESP3 and pipelining; and authentication, TLS, pools, timeouts, cancellation and queue limits for both. Redis Cluster, Sentinel and Pub/Sub follow later. ark integrates these clients through its public coroutine interfaces, with protocol-peer, hidden-symbol DSO and explicit real-service acceptance entry points. See the [database test guide](../../tests/database/README.md) for covered cases and limits; this does not establish arbitrary deployment, old-library compatibility or production capacity.

## Decisions to discuss next

1. Keep the unified snake_case public interfaces and directory layout; see [Project layout](../reference/directory-structure.md) for public headers and CMake targets.
2. Keep MySQL and Redis deployment validation aligned with the public coroutine APIs and native owning results. arknet provides the caller-driven executor; ark pumps it on the module thread. Use the explicit real-service acceptance modes before deploying a new server configuration.
3. Confirm interfaces and dependencies for Lua, Actor and other official plugins; add compilation, functional and failure-path verification per plugin.

Resume hot-update discussions about state ownership, in-flight calls and compatibility after the current modernization.

## Utility construction and compatibility

Utility keeps its four public module interfaces and scheduler helper headers in `src/plugins/utility/`, mapped explicitly through `ark::utility`. Each module implementation is private to its cpp file and is constructed by a private factory returning its public interface. The plugin uses `plugin_base` and explicit `DllEntryPlugin`/`DllExitPlugin` entry points. Only the timer module participates in manager updates. See [Utility APIs and migration](../reference/utility.md).

The removed `ark/utility/*_module_impl.hpp` and `ark/utility/utility_plugin.hpp` headers are a source compatibility change. Use the public module interfaces and typed lookup rather than constructing implementation classes. Rebuild Utility and consumers together; this does not promise compatibility with old implementation-class binaries.

Kernel co-locates its 38 existing headers with its cpp files in `src/plugins/kernel/`. `ark::kernel` preserves every `<ark/kernel/...>` path, including `data_impl.hpp`, `data_list_impl.hpp` and `kernel_plugin.hpp`. Kernel uses typed module/plugin bases, private factories and explicit entries while retaining public implementation/plugin types. Rebuild affected consumers together; see [Kernel construction](../reference/kernel.md#typed-kernel-construction) for the class-layout compatibility boundary. See [Kernel reference](../reference/kernel.md) for consumption checks and Protobuf requirements.
