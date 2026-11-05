# Roadmap

Use this index to find supported capabilities and remaining work. C++20 is the build baseline. The [historical roadmap](roadmap-history.md) preserves earlier plans; unchecked ideas are not scheduled work.

## Capabilities and remaining work

| Area | Available · details | Remaining work |
| --- | --- | --- |
| Project layout | Co-located plugin sources and explicit `<ark/...>` public-header mappings · [layout and interfaces](../reference/directory-structure.md) | C++20 implementation choices and retained compatibility boundaries: [code conventions](code-style.md#c20-implementation); optional integrations retain their stated support limits |
| Databases | Owner-thread Asio coroutine APIs, owning values, MySQL prepared statements/transactions and Redis commands/pipelines · [MySQL](../reference/mysql.md), [Redis](../reference/redis.md) | Target-environment recovery, persistence, cancellation/fault and TLS acceptance · [tested operations and exclusions](../guides/database-validation.md#acceptance-boundaries) |
| Entity configuration | Native CSV/XLSX/JSON export, C++/C# field constants and startup JSON loading · [tools](../guides/tools.md), [Kernel loading](../reference/kernel.md#json-entity-schema-and-business-data) | No live reload, database migration or client/server distribution filtering |
| Entity synchronization | Owning captures, bounded revisioned C++ publisher/replica, [canonical nested Protobuf codec](../reference/entity-protocol.md), portable C# replica and room inventory/equipment integration · [synchronization](../concepts/entity-sync.md), [guest room](../guides/examples.md) | General subscriptions, dynamic authorization, AOI/interest management and in-place reset; each stream currently has a fixed projection |
| Startup and shutdown | Required-provider graph and ordered startup, managed-task cancellation, consumer-first per-module cleanup/object destruction, all DSOs last and opt-in reverse startup cleanup for up to 16 owners · [lifecycle status](lifecycle-closeout.md), [launcher](../guides/applications.md), [tasks](../guides/tasks.md) | Applications/operators complete announcements, ingress closure, player removal and saving before process stop; [guarded resource slices](startup-failure-cleanup.md#remaining-io-boundaries) do not guarantee generic live-IO retirement, business rollback or undeclared external callback safety |
| Archived plugins | Consul, Etcd Client, Kafka and OSS SDK: retained source, public headers, targets, explicit opt-in and compatibility checks; runtime plugins default off · [archived integrations](../reference/directory-structure.md#archived-integrations) | Feature development paused; resume only when explicitly re-enabled by the user |
| Optional physics | Explicit unsupported PhysX 3 configuration · [plugin boundaries](../reference/directory-structure.md) | No validated PhysX 3 integration |

Database real-service acceptance records identify the tested macOS revisions and operations. Native room checks and portable C# checks cover the included integration; actual Godot execution and deployment acceptance require their own matching revisions and results. These checks do not establish production capacity or acceptance in every environment.

## Current priorities

Current work is local. The [Entity replication contract](entity-replication-design.md) covers owning capture, trusted state, nested wire conversion and the portable client; the room uses one current protocol with coordinated server/client upgrades. General subscription/authorization orchestration remains TODO. Performance conclusions require measured payloads and owner-thread cost.

| Priority / gate | Current decision |
| --- | --- |
| Environment | Only the local machine is in scope; real deployment is deferred |
| Release | Publication scope and acceptance criteria remain undecided |
| Larger-load and sustained acceptance | First resolve the local 5000-connection problem and pass its acceptance; do not advance larger-scale or endurance work before that prerequisite |
| Investigation | The single tracking entry is [arknet issue #61](https://github.com/OpenArkStudio/arknet/issues/61); the investigation remains paused |

## Deferred directions

Registry and business-plugin hot update remain paused pending ownership, in-flight work, dependency and compatibility design. New scope needs a named API/deployment, prerequisites and acceptance evidence before entering this roadmap.
