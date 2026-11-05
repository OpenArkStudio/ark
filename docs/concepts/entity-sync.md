# Entity synchronization

Kernel captures owning values on its owner thread. A revisioned publication then
crosses the [generic Protobuf boundary](../reference/entity-protocol.md) into a
portable C# replica; Godot presents only committed state.

| Layer | Responsibility |
| --- | --- |
| Application | Bind each recipient to a root, schema, fixed field/type projection and allowed child identities |
| Kernel | `capture_nested_snapshot` freezes scalar fields, stable table-row lifetimes and container/child relationships |
| Publication | `nested_publisher` coalesces visible changes into one root revision; unchanged state emits nothing |
| Wire | `entity_sync.proto` preserves scalar types, explicit row identities, container replacements and child patches |
| Client | `NestedReplica` validates a private candidate and commits a complete root atomically; the room adapter commits all roots in its envelope together |
| Presentation | Render committed movement, HP/MP, inventory and equipment; controls send requests without changing authoritative values |

## Baseline, delta and removal

| Change | Publication |
| --- | --- |
| Join or reconnect | Complete authorized baseline, including empty tables/containers and zero values |
| Scalar/row-column edit | Sparse typed field patch |
| Row deletion and reuse | Remove the old row incarnation; introduce the replacement with a new incarnation |
| Equipment switch | Replace that container's final authorized membership with complete child state |
| Equipped child upgrade | Patch the same child identity and incarnation |
| Root removal | Tombstone; old deltas cannot resurrect it |

A valid duplicate does not roll state backward. A revision gap, unknown identity,
wrong type or exhausted bound invalidates the affected stream. The room closes
the connection and reconnects with a fresh binding and baseline; it does not
replay requests or perform an in-place reset.

## Demonstration and boundaries

The [guest room](../guides/examples.md) uses actual Kernel Entities: public fields
100–108, inventory table 200 and equipment container 300. It keeps one current
client/server contract; earlier scalar-only clients must be rebuilt together with
the server. [Room protocol](../../examples/guest_room/protocol.md) lists requests,
application limits and migration requirements.

The reusable C++ scalar interfaces remain available for scalar-only application
projections. They are not a second room protocol. Generic subscriptions, changing
permissions during a connection, AOI, cross-root transactions, fragmentation,
persistence and ACK/replay history remain outside this delivery. The
[replication design](../project/entity-replication-design.md) records these boundaries.
