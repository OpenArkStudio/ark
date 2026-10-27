# ARK guest-room protocol v2

[中文](protocol_CN.md)

TCP frames have a fixed 12-byte header: ASCII `ARK2` at bytes 0..3, unsigned big-endian 16-bit message ID at 4..5, zero 16-bit flags at 6..7 and unsigned big-endian 32-bit body byte length at 8..11. Body length excludes the header and is at most 8192 bytes. This limit belongs to the locally configured sample header, not a negotiated wire field; native CS/SS headers retain their 4096-byte default. The body is a UTF-8 JSON object. There is no compression. Bad magic, nonzero flags or oversized bodies close the connection before message allocation/business dispatch. Partial headers/bodies are retained; coalesced frames are parsed separately. Each connection owns fresh parser state.

**Upgrade from v1:** update both the server and C# client together. `ARK1` frames are rejected by v2, and old clients reject `ARK2`. The expanded snapshot and 8192-byte budget retain the 16-player limit, including 64-byte escaped names and full uint32 attributes. Rebuild matching Net and protocol consumers for the `max_body_size()` header ABI.

| ID | Direction | Body |
| --- | --- | --- |
| 60000 | Client → server: join | `{"name":"Alice","room":"lobby"}` |
| 60001 | Server → joining client: accepted | `{"player_id":"1","name":"Alice","room":"lobby","width":960,"height":540}` |
| 60002 | Client → server: move | `{"seq":1,"x":120.0,"y":100.0}` |
| 60003 | Server → joined peers: full snapshot | `{"room":"lobby","players":[{"id":"1","name":"Alice","x":120.0,"y":100.0,"seq":1,"hp":85,"mp":100,"hp_max":100,"mp_max":100,"action_seq":1}]}` |
| 60004 | Server → client: error | `{"code":"invalid_message","message":"bounded reason"}`; action errors also include `"action_seq":1` |
| 60005 | Client → server: ping | `{"nonce":1}` |
| 60006 | Server → client: pong | `{"nonce":1}` |
| 60008 | Client → server: attribute action | `{"seq":1,"kind":"damage"}` |

ID 60007 is an internal unsupported-message callback key, never a wire response. Only join, move, ping and action are accepted client request IDs. Unsupported/directionally invalid IDs produce `invalid_message`; mapping them to 60007 keeps Net's reserved IDs 1/65535 out of this protocol's SS control paths.

There is one `lobby`, at most 16 joined players and no account authentication. Player IDs are opaque strings assigned uniquely within the running example process. A name contains 1..64 UTF-8 encoded bytes and no Unicode C0/C1 controls. Sequences, nonce and all snapshot HP/MP/max fields are integral JSON values in `0..4294967295`; floating-point representations such as `1.0`, negative values, booleans and strings are rejected. Movement starts at sequence 1 and strictly increases independently of action sequence. Snapshot `seq` exposes the Entity's `move_seq`; `action_seq` exposes its separately accepted action sequence. Coordinates must be finite and inside `0..960` / `0..540`; the server chooses the spawn position.

An action contains exactly `seq` and `kind`, requires joining, and starts at sequence 1. The server applies it only to the requesting session's Entity. Client amounts, targets or HP/MP values are rejected.

| Kind | Server-owned effect |
| --- | --- |
| `damage` | Reduce own HP by 15, clamped at zero |
| `skill` | Spend 20 MP only when at least 20 MP is available |
| `rest` | Restore 20 HP and 20 MP, clamped at the configured maxima without uint32 overflow |

The shipped profile starts HP, MP and both maxima at 100, and both sequences at zero. Every accepted action advances `action_seq` and broadcasts a full snapshot, including a clamped no-op at zero or maximum. HP zero does not disable movement. This is an attribute demonstration, without combat, targeting or PvP rules.

A successful join queues accepted before its full snapshot and broadcasts the roster to joined peers. Each accepted move/action broadcasts a new authoritative roster. Disconnect destroys the session's Kernel Entity and broadcasts the reduced roster. A reconnect rejoins as a fresh player with a new ID and newly loaded profile defaults; old identity, sequences and resources are not resumed. Ping does not require joining.

Errors are `room_full`, `already_joined`, `not_joined`, `invalid_message`, `stale_sequence` and `insufficient_resource`. Invalid, stale and resource-rejected actions leave every Entity field and accepted sequence unchanged. An action error echoes a valid uint32 request sequence as `action_seq`; missing or invalid sequence values use the uncorrelated sentinel zero. Clients keep the session, show the error and settle a pending action only for its matching sequence or an authoritative snapshot acknowledging it. No automatic replay is promised. Business errors keep the session open unless work/output limits are exceeded; reliable business acknowledgement, durable identity and persistence remain outside this protocol.
