# ARK guest-room Protobuf protocol

[中文](protocol_CN.md)

The canonical contract is [guest_room.proto](../../src/proto/proto/guest_room.proto), package `ark.protobuf.guest_room`, C# namespace `Ark.Client.Protocol`. Both peers generate their messages from this schema. The application protocol version is `1`, required in Join and Accepted. Upgrade the server and Godot client together: the former ARK2 JSON bodies are incompatible, although the transport marker remains `ARK2`. JSON remains the Entity schema/configuration format on the server.

TCP frames have a fixed 12-byte header: ASCII `ARK2` at bytes 0..3, unsigned big-endian 16-bit message ID at 4..5, zero 16-bit flags at 6..7 and unsigned big-endian 32-bit body byte length at 8..11. Body length excludes the header and is at most 8192 bytes. Bodies are uncompressed Protobuf. The sample's header limit does not change native CS/SS headers and their 4096-byte default. Bad magic, flags or excessive length close the connection before business dispatch. Partial frames are retained and coalesced frames are parsed separately; every connection owns fresh parser state.

| ID | Direction | Protobuf message |
| --- | --- | --- |
| 60000 | Client → server | `join_request`: name, room, protocol_version |
| 60001 | Server → joining client | `join_accepted`: logical player_id, name, room, dimensions, actual entity_id, protocol_version |
| 60002 | Client → server | `move_request`: sequence, x, y |
| 60003 | Server → joining client | `room_baseline`: complete current authorized roster |
| 60004 | Server → client | `error_response`: code, message, optional action_sequence |
| 60005 / 60006 | Ping / pong | `heartbeat`: uint32 nonce |
| 60008 | Client → server | `action_request`: sequence and action enum |
| 60009 | Server → joined peers | `room_delta`: added baselines and changed/removal deltas |

60007 is an internal unsupported-message dispatch key and is never emitted. Only Join, Move, Ping and Action are valid client IDs. Unknown/directionally invalid IDs and parsed malformed Protobuf close the connection; refused Join/Move requests have the exception below. Protobuf unknown fields are allowed for forward compatibility; an unknown action enum is a business error. Error codes are `room_full`, `already_joined`, `not_joined`, `invalid_message`, `stale_sequence`, `insufficient_resource` and `server_stopping`. Business errors retain the connection within its work/output limits. Action errors carry `action_sequence`, including zero for a missing sequence; other errors omit it.

Once the manager leaves `running`, owner-thread callback entry refuses Join, Move and Action with `server_stopping`, without changing membership or Entity fields. A refused Action parses its body only to echo `action_sequence`; malformed Action still closes the connection. Refused Join/Move bodies are not parsed, so even malformed bodies receive the stopping error. Ping, unsupported messages and connection events retain ordinary dispatch, and refusals consume the same work/output limits. Clients settle the matching pending action from its error, then rejoin after reconnecting without automatic replay. This adds no schema field or wire change and guarantees neither native send delivery, TCP accept quiescence nor library unload.

There is one `lobby`, at most 16 players and no account authentication. Names require 1..64 valid UTF-8 bytes without Unicode C0/C1 controls. Coordinates must be finite and inside `0..960` / `0..540`; the server chooses spawn positions. Movement and action uint32 sequences each start at 1 and increase independently. Player IDs are opaque decimal strings unique within the process; Entity GUIDs identify actual Kernel incarnations. Accepted binds both identifiers for the requesting session. Requests never select an Entity or authorize mutation through a supplied identity.

## Public Entity projection

The public class is `GuestPlayer`, schema ID `ark.guest-player.v1`. Every player baseline contains exactly these fields; the configuration ID at index 99 and all server-only data are excluded.

| Index | Field | Protobuf scalar alternative |
| --- | --- | --- |
| 100 | name | `string_value` bytes, validated as the name above |
| 101 / 102 | x / y | `double_value` |
| 103 | move_seq | `uint32_value` |
| 104 / 105 | hp / mp | `uint32_value` |
| 106 / 107 | hp_max / mp_max | `uint32_value` |
| 108 | action_seq | `uint32_value` |

Generic scalar oneofs explicitly distinguish bool, sint32, uint32, sint64, uint64, float, double, bytes and signed 64-bit GUID. Zero values retain oneof presence. Bytes may contain arbitrary data in the generic adapter; the public name applies stricter UTF-8 rules. Generic GUID values preserve negative native values and are bounded to signed 64-bit wire/native widths. Room Entity identities remain positive uint64 values bounded to native `guid_t`. The portable client validates a complete room envelope before committing roster changes, including exact projection/type, identities, schema/class, finite coordinates, positive HP/MP maxima and current-value bounds.

The owner-thread room update calls real `kernel.capture_scalar_snapshot` once per active Entity, then `scalar_publisher` compares the selected fields. Accepted is queued before a full RoomBaseline for a fresh connection. Existing peers receive a RoomDelta containing new player baselines, selected changed fields, or `removed = true` tombstones. Accepted Move/Action requests do not immediately broadcast full rosters. Mutations before a flush coalesce; a newly added baseline already contains their final state. Unchanged state emits no delta. Creation/removal before publication may emit nothing.

Each Entity publisher starts at generation 1 and revision 1. A delta uses the same generation and actual Entity GUID, `required_revision` equal to the previous revision and `revision = required_revision + 1`. A removal advances revision and contains no fields. Gaps, invalid schema/types or generation regressions require a fresh connection/baseline; duplicates cannot regress state, and a tombstone prevents resurrection. There is no ACK, replay or persistent replica history. Reconnection clears old parser/replica state, joins with new logical and Entity IDs and installs a new full baseline.

## Server-owned actions and limits

`action_damage` reduces own HP by 15, clamped at zero. `action_skill` requires and spends 20 MP. `action_rest` restores 20 HP/MP, clamped at the configured maxima without uint32 overflow. `action_unspecified` and unknown enum values are rejected. Initial values come from profile 1, shipped with HP/MP/maxima 100 and both sequences zero. HP zero still permits movement. A clamped action still advances `action_seq`, so its delta may contain only index 108. Invalid/stale/resource-rejected actions change no fields or sequences. Clients settle a pending action only from its matching error or authoritative sequence acknowledgement; they do not replay automatically.

The example admits 32 connections, 16 joined players, 64 business callbacks per owner tick and 32 messages per connection per second. Each peer owns at most eight queued frames and `8 * (8192 + 12)` queued bytes. Work/queue overflow, failed serialization/admission/send, unsupported captured state and revision exhaustion close affected sessions; a stream-wide capture failure closes joined sessions rather than continuing with partial state. Underlying transport queue limits still apply. These demonstration bounds do not establish production capacity, hostile-client completeness or Godot visual acceptance.

[wire-vectors.json](wire-vectors.json) contains shared Protobuf body/frame hex for C++ and C# checks, including full baseline, HP delta and tombstone. The schema and fixtures must remain byte-identical in the server/client repositories.
