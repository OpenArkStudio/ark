# Entity incremental synchronization

Kernel provides owning scalar captures and revisioned publisher/replica APIs. The guest room connects them to Protobuf and Godot C# through an explicit public projection. General subscriptions and nested replication remain proposals.

![Scalar synchronization flow](../assets/entity-sync-flow.svg)

## Current capability and boundary

| Component | Available | Boundary · contract |
| --- | --- | --- |
| Kernel | `capture_scalar_snapshot`, `scalar_publisher`, `scalar_replica`; coalesced changes, atomic revision checks and tombstones | In-process C++ values; caller selects authorized fields and flush point. Field/text limits cover reconstructed state, not encoded size · [scalar API](../reference/kernel.md#scalar-baselines-and-deltas), [capture limits](../reference/kernel.md#owning-scalar-snapshots) |
| Room v1 | Join/reconnect baseline, later player baselines, scalar deltas and removal over TCP; public fields 100–108, hidden configuration field 99 | Server assigns player/Entity identities; clients request actions. One lobby with bounded work/output, no account service or persistence · [protocol, identity and numeric limits](../../examples/guest_room/protocol.md) |
| Godot C# | Generated Protobuf messages, shared fixtures, whole-room validation before commit and scene updates on the engine thread | Upgrade both peers together; JSON-only room peers are incompatible. Entity definitions/profiles stay JSON · [compatibility contract](../../examples/guest_room/protocol.md), [client](https://github.com/OpenArkStudio/ark-client-godot) |
| Net / arknet | Framework frame delivery / transport IO | Enqueue is not an application acknowledgment; no ACK/replay, fragmentation or AOI in room v1 · [Net ownership](../reference/network.md) |

Native checks cover the C++ state loop and room transport; local two-engine Godot tests cover movement, HP/MP, removal and reconnect. Main CI passed on Linux, macOS and Windows. These results do not establish production capacity or general replication.

## Proposed general contract

The following design rules extend beyond room v1; they are not additional APIs, wire fields or scheduled work. Applications still need schema identity, authorized sessions, flush integration and numeric bounds.

| Topic | Proposed rule |
| --- | --- |
| Projection and identity | A full baseline contains only authorized state, with explicit zero/default representation for omitted authorized fields. Use runtime Entity GUID, incarnation/epoch, schema/class and stable member indices; configuration IDs remain separate. GUID reuse requires a new incarnation; rows need generations, child references need Entity and container/slot identity. |
| Revisions | One stream covers a root and its selected nested state; no cross-root atomic transaction. Freeze baseline R on the owner thread and collect later changes separately. Apply only matching base revisions; ignore duplicates, invalidate gaps/epochs/schema mismatches, reconnect for a fresh baseline. Hidden mutations need not advance a recipient's revision. |
| Collection and events | Use an explicit flush phase after business mutations, not module registration order; seal immutable owned batches, with callback mutations entering the next batch. Never pass borrowed `node*`, `row*` or `entity*` to IO workers. Coalesce state only against the known recipient baseline; `real_time` may request earlier flushes without bypassing order, authorization or bounds. Events need separate ordering, duplicate/gap and retention rules; overflow must report a gap or terminate, never silently discard. State reset does not replay or acknowledge events. |
| Session authorization | Bind server-assigned recipients and allowed projections; guest identities need no account service. Intersect `sync_self`/`sync_view` policy inputs with session authorization; deny missing bindings/policy and require Entity-owner binding for self-only fields. Client IDs grant no authority or write permission. Filter children recursively; parent access grants no child-field access. Team/guild/map/AOI policy remains separate. |
| Permission changes | Check authorization before delivery admission. Expansion requires a baseline before deltas; revocation removes client state and unadmitted unauthorized payloads. Close if queued immutable payloads are no longer authorized. A new epoch replaces the projection, removing omitted fields/rows/children. |
| Creation and removal | Publish after initialization/callbacks; initial writes form the baseline. Tombstones cancel unsealed state and prevent resurrection; already admitted batches remain ordered before removal. Unseen create/destroy may disappear from state replication. Events follow their own rules. |
| Recovery and bounds | Separate recipient progress from global dirty state. Bound pending bytes/batches/age, baseline size and reconnect/reset rate; reject oversized baselines. Overflow/admission failure closes the stream, disconnect releases state, reconnect installs a fresh baseline/epoch. No retained ACK/replay history. In-place reset and fragmentation are later choices; fragmentation would need atomic install and bounded assembly time/memory. |

### Proposed nested ordering

| Mutation | Required result |
| --- | --- |
| Add row → update | Add final values before later updates |
| Update row → delete | Delete supersedes pending updates for that generation |
| Delete row → reuse number | Delete old generation before creating new generation |
| Clear → add row | Clear barrier before new rows; supersede earlier row deltas |
| Place / swap / remove child | Ordered relationships or revisioned replacement of affected slots; remove does not imply destroy |
| Destroy child | Remove relationships before visible destruction; suppress unsealed updates, keep admitted batches before removal |
| Cross-container move | One ordered relationship batch or explicit projection reset; unsupported cross-root moves may be rejected |

Nested acceptance must exercise immediate/deferred paths, container-only batches, revoke during queueing, GUID reuse, shutdown and stale references against a baseline. Existing callbacks and operation enums do not establish this contract; legacy delayed-sync values remain separate from owning capture.

Owner-thread rules remain in force: Net callbacks arrive through Update; HTTP IO handlers must marshal Entity commands to the owner thread. Off-thread encoding would require immutable owned input, ordered completion and bounded queues. See [Net](../reference/network.md), [lifecycle](application-architecture.md), [data model](data-driven.md) and [data structures](../guides/data-structures.md).

The [configuration exporter](../guides/tools.md) shares indices, not a general C# Entity runtime. Index removal/reuse or type changes require an explicit compatibility/reset policy; JSON format version alone is not application schema identity. Measure payload and owner-thread cost after correctness acceptance. RPC, persistence, Registry and hot update are outside this scope.
