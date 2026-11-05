# Guest-room protocol

The room uses the framework's [generic Entity protocol](../../docs/reference/entity-protocol.md).
The canonical schemas are `src/proto/proto/entity_sync.proto` and `guest_room.proto`;
client copies and shared [wire vectors](wire-vectors.json) must match byte-for-byte.

## Connection and frames

| Item | Contract |
| --- | --- |
| Framing | 12-byte header: `ARK2` magic, big-endian uint16 message ID, two zero bytes, big-endian uint32 body length |
| Body | Protobuf, at most 8192 bytes; malformed headers or oversized bodies close the connection |
| Join | `join_request` names `lobby`, a nonempty valid UTF-8 name of at most 64 bytes without controls, and `protocol_version = 2` |
| Acceptance | Server assigns a logical player ID and positive signed-64-bit Entity GUID; the client binds its own identity before installing a room baseline |
| Compatibility | One current contract only. Server and client must be rebuilt together; former scalar room messages are incompatible and are not negotiated |

| ID | Request / response |
| ---: | --- |
| 60000 / 60001 | Join / accepted |
| 60002 | Movement intent with increasing `sequence` and finite coordinates inside 960 × 540 |
| 60003 | Complete room baseline with per-player instance bindings and generic nested baselines |
| 60004 | Error; optional `action_sequence` correlates a rejected action |
| 60005 / 60006 | Ping / pong nonce |
| 60008 | Business action intent with its own increasing sequence |
| 60009 | Added player baselines and generic Entity deltas/tombstones |

## Explicit public projection

The application supplies the field/type layout independently from state values.
A player binding declares its root identity, root incarnation, stream generation
and two fixed authorized equipment GUIDs. It cannot add field permissions or types.
Both peers see the following demonstration projection; this is not an account or privacy system.

| Member | State |
| --- | --- |
| `GuestPlayer` fields 100–108 | Name, x/y, movement sequence, HP/MP, maxima and action sequence; configuration ID 99 remains hidden |
| Table 200, `InventoryItem` | Row 1 holds potion item 1 and quantity; columns 1/2 are uint32 item ID/quantity |
| Container 300 | Slot 1 holds one authorized `GuestEquipment`; fields 1/2 are uint32 item ID/power |
| Initial data | HP/MP and maxima come from profile 1; potion quantity 2, blade item 1001/power 10; alternate staff item 1002/power 20 remains available for switching |

## Publication and recovery

1. Business requests mutate real Kernel fields, table rows and container relations on the owner thread.
2. Each owner-thread flush captures a complete authorized root once. `nested_publisher` emits a baseline for a new player or a delta when visible state changes.
3. Row numbers remain stable. Deleting and recreating row 1 assigns a new incarnation; it cannot receive an old row's patch. Changing the equipped child replaces the container membership; upgrading it patches that child's fields.
4. Clients validate an entire room envelope in private candidates before exposing any player change. Root revisions, generations, row lifetimes, declared child identities and resource bounds must match. Valid duplicates do not regress state.
5. Invalid state or a revision gap closes the session. Reconnect clears the old replica and pending intents, receives a new identity and installs a fresh baseline. There is no replay, ACK history, in-place reset or cross-root transaction.

| Action | Server result |
| --- | --- |
| `damage` / `skill` / `rest` | HP −15; MP −20 if available; restore HP/MP by up to 20 |
| `pickup` | Add one potion, up to 99; an empty table receives a new row lifetime |
| `use_potion` | Consume one potion and restore up to 10 HP; delete the row when empty |
| `equip` | Switch between the two fixed authorized equipment Entities |
| `upgrade` | Increase the equipped Entity's power by one, up to 100 |

Successful clamped actions still acknowledge their sequence. Rejected requests do
not mutate state or advance the authoritative action sequence. A local send or UI
click is not a server confirmation.

## Bounds

The room allows 16 joined players and 32 connections. Per connection: 32 requests/s,
eight pending frames and `8 * (8192 + 12)` owned output bytes; the owner tick admits
at most 64 requests. Overflow or send rejection closes the affected session.

Per root: depth 1, three Entities, eight rows, 64 fields/member records and 512
string/metadata bytes. Patches separately allow 128 operations, 64 values and 512
string bytes; lifetime history is limited to 64 records. Protobuf depth is limited
to 16 and its body budget remains separate from native state/patch limits.
These are demonstration bounds, not framework defaults or production capacity.
