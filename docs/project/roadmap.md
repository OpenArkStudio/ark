# Roadmap

Use this index to find supported capabilities and remaining work. C++20 is the build baseline. The [historical roadmap](roadmap-history.md) preserves earlier plans; unchecked ideas are not scheduled work.

## Capabilities and remaining work

| Area | Available · details | Remaining work |
| --- | --- | --- |
| Project layout | Co-located plugin sources and explicit `<ark/...>` public-header mappings · [layout and interfaces](../reference/directory-structure.md) | Review remaining modules and optional integrations individually; C++20 and header layout do not establish complete modernization |
| Databases | Owner-thread Asio coroutine APIs, owning values, MySQL prepared statements/transactions and Redis commands/pipelines · [MySQL](../reference/mysql.md), [Redis](../reference/redis.md) | Target-environment recovery, persistence, cancellation/fault and TLS acceptance · [tested operations and exclusions](../guides/database-validation.md#acceptance-boundaries) |
| Entity configuration | Native CSV/XLSX/JSON export, C++/C# field constants and startup JSON loading · [tools](../guides/tools.md), [Kernel loading](../reference/kernel.md#json-entity-schema-and-business-data) | No live reload, database migration or client/server distribution filtering |
| Entity synchronization | Owning scalar/nested captures, bounded revisioned C++ publisher/replica and room scalar v1 Protobuf/Godot integration · [synchronization](../concepts/entity-sync.md), [guest room](../guides/examples.md) | General subscriptions/authorization, nested wire codec and client adaptation; nested C++ state does not change room v1 |
| Startup and shutdown | Required-provider graph and ordered startup, consumer-first `stop_accepting()`, HTTP request admission/drain, managed-task drain, cooperative shutdown waiting and opt-in single-slot cleanup for a private standalone wheel · [lifecycle status](lifecycle-closeout.md), [launcher](../guides/applications.md), [tasks](../guides/tasks.md) | Remaining Net protocol ingress migration, complete dependency-aware teardown and shared IO startup-failure cleanup; task drain does not certify arbitrary background work or general rollback |
| Optional plugins | Kafka construction/export checks; OSS SDK scaffold; explicit unsupported PhysX 3 configuration · [plugin boundaries](../reference/directory-structure.md) | Kafka broker/driver acceptance and OSS client/request functionality |

Database real-service acceptance records identify the tested macOS revisions and operations. The guest-room demo has bounded native checks and local Godot testing. Neither establishes production capacity or acceptance in every deployment.

## Priority design

Official-module admission closure, startup-failure cleanup and documentation synchronization are the current delivery priorities. General Entity subscriptions and nested wire/client adaptation remain deferred for later discussion. [Synchronization design boundaries](../concepts/entity-sync.md) cover authorization, nested identity/order, events and recovery. The trusted-binding nested C++ baseline/delta/replica is available; room v1 retains its scalar wire contract. Performance claims require measured payload and owner-thread cost.

## Deferred directions

Registry and business-plugin hot update remain paused pending ownership, in-flight work, dependency and compatibility design. New scope needs a named API/deployment, prerequisites and acceptance evidence before entering this roadmap.
