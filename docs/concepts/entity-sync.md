# Entity incremental synchronization

Kernel provides owning scalar captures and revisioned publisher/replica APIs. The guest room connects them to Protobuf and Godot C# through an explicit public projection. Owning nested capture is available; nested deltas/replicas and general subscriptions remain future stages.

![Scalar synchronization flow](../assets/entity-sync-flow.svg)

## Current capability and boundary

| Component | Available | Boundary · contract |
| --- | --- | --- |
| Kernel | `capture_scalar_snapshot`, `scalar_publisher`, `scalar_replica`; coalesced changes, atomic revision checks and tombstones | In-process C++ values; caller selects authorized fields and flush point. Field/text limits cover reconstructed state, not encoded size · [scalar API](../reference/kernel.md#scalar-baselines-and-deltas), [capture limits](../reference/kernel.md#owning-scalar-snapshots) |
| Nested capture | `capture_nested_snapshot`; owned row lifetimes and child identities, explicit root/child authorization, aggregate bounds | Capture only; no nested differ/replica or codec · [nested API](../reference/kernel.md#owning-nested-snapshots) |
| Room v1 | Join/reconnect baseline, later player baselines, scalar deltas and removal over TCP; public fields 100–108, hidden configuration field 99 | Server assigns player/Entity identities; clients request actions. One lobby with bounded work/output, no account service or persistence · [protocol, identity and numeric limits](../../examples/guest_room/protocol.md) |
| Godot C# | Generated Protobuf messages, shared fixtures, whole-room validation before commit and scene updates on the engine thread | Upgrade both peers together; JSON-only room peers are incompatible. Entity definitions/profiles stay JSON · [compatibility contract](../../examples/guest_room/protocol.md), [client](https://github.com/OpenArkStudio/ark-client-godot) |
| Net / arknet | Framework frame delivery / transport IO | Enqueue is not an application acknowledgment; no ACK/replay, fragmentation or AOI in room v1 · [Net ownership](../reference/network.md) |

Native checks cover the C++ state loop and room transport; local two-engine Godot tests cover movement, HP/MP, removal and reconnect. Main CI passed on Linux, macOS and Windows. These results do not establish production capacity or general replication.

## Next design boundary

Owning nested capture records stable row lifetimes and separates child identity from container slots. The next stages must commit one authorized root projection in order. Row numbers can be reused; deleting a row changes presentation positions without renumbering surviving row keys. Existing borrowed delayed-sync callbacks cannot provide owning nested publications.

The [general Entity replication design](../project/entity-replication-design.md) ties these constraints to current source, defines baseline/delta ordering, authorization, recovery and memory bounds, and records the selected boundaries: row lifetime tokens, one atomic root without cross-root transactions, and fixed projections with reconnect recovery. Only capture is provided in this stage; nested differ/replica work follows separately. It reuses the scalar leaf values while keeping room v1 unchanged.

Owner-thread rules remain in force: Net callbacks arrive through Update; HTTP IO handlers must marshal Entity commands to the owner thread. See [Net](../reference/network.md), [lifecycle](application-architecture.md), [data model](data-driven.md) and [data structures](../guides/data-structures.md). RPC, persistence, Registry and hot update remain outside this design.
