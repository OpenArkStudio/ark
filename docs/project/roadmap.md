# Roadmap

Use this index to find supported capabilities and remaining work. C++20 is the build baseline. The [historical roadmap](roadmap-history.md) preserves earlier plans; unchecked ideas are not scheduled work.

## Capabilities and remaining work

| Area | Available · details | Remaining work |
| --- | --- | --- |
| Project layout | Co-located plugin sources and explicit `<ark/...>` public-header mappings · [layout and interfaces](../reference/directory-structure.md) | Review remaining modules and optional integrations individually; C++20 and header layout do not establish complete modernization |
| Databases | Owner-thread Asio coroutine APIs, owning values, MySQL prepared statements/transactions and Redis commands/pipelines · [MySQL](../reference/mysql.md), [Redis](../reference/redis.md) | Target-environment recovery, persistence, cancellation/fault and TLS acceptance · [tested operations and exclusions](../guides/database-validation.md#acceptance-boundaries) |
| Entity configuration | Native CSV/XLSX/JSON export, C++/C# field constants and startup JSON loading · [tools](../guides/tools.md), [Kernel loading](../reference/kernel.md#json-entity-schema-and-business-data) | No live reload, database migration or client/server distribution filtering |
| Scalar synchronization | Owning captures, revisioned publisher/replica and explicit room v1 Protobuf/Godot integration · [synchronization](../concepts/entity-sync.md), [guest room](../guides/examples.md) | General subscriptions/authorization and nested table/container replication remain proposals |
| Startup and shutdown | First-failure propagation, managed-task admission/drain and cooperative shutdown waiting · [launcher](../guides/applications.md), [tasks](../guides/tasks.md) | Partial-startup rollback and common dependency-aware shutdown; task drain does not establish completion of arbitrary background work |
| Optional plugins | Kafka construction/export checks; OSS SDK scaffold; explicit unsupported PhysX 3 configuration · [plugin boundaries](../reference/directory-structure.md) | Kafka broker/driver acceptance and OSS client/request functionality |

Database real-service acceptance records identify the tested macOS revisions and operations. The guest-room demo has bounded native checks and local Godot testing. Neither establishes production capacity or acceptance in every deployment.

## Priority design

General Entity subscriptions and nested replication share discussion priority with startup/shutdown lifecycle work. [Synchronization design boundaries](../concepts/entity-sync.md) cover authorization, nested identity/order, events and recovery; room v1 already fixes the scalar wire contract. Performance claims require measured payload and owner-thread cost.

## Deferred directions

Registry and business-plugin hot update remain paused pending ownership, in-flight work, dependency and compatibility design. New scope needs a named API/deployment, prerequisites and acceptance evidence before entering this roadmap.
