# ARK guest-room protocol v1

[中文](protocol_CN.md)

TCP frames have a fixed 12-byte header: ASCII `ARK1` at bytes 0..3, unsigned big-endian 16-bit message ID at 4..5, zero 16-bit flags at 6..7 and unsigned big-endian 32-bit body byte length at 8..11. Body length excludes the header and is at most 4096. The body is a UTF-8 JSON object. There is no compression. Bad magic, nonzero flags or oversized bodies close the connection before message allocation/business dispatch. Partial headers/bodies are retained; coalesced frames are parsed separately. Each connection owns fresh parser state.

| ID | Direction | Body |
| --- | --- | --- |
| 60000 | Client → server: join | `{"name":"Alice","room":"lobby"}` |
| 60001 | Server → joining client: accepted | `{"player_id":"1","name":"Alice","room":"lobby","width":960,"height":540}` |
| 60002 | Client → server: move | `{"seq":1,"x":120.0,"y":100.0}` |
| 60003 | Server → joined peers: full snapshot | `{"room":"lobby","players":[{"id":"1","name":"Alice","x":120.0,"y":100.0,"seq":1}]}` |
| 60004 | Server → client: error | `{"code":"invalid_message","message":"bounded reason"}` |
| 60005 | Client → server: ping | `{"nonce":1}` |
| 60006 | Server → client: pong | `{"nonce":1}` |

There is one `lobby`, at most 16 joined players and no account authentication. Player IDs are opaque strings assigned uniquely within the running example process. A name contains 1..64 UTF-8 encoded bytes and no Unicode C0/C1 controls. `seq` and `nonce` are integral JSON values in `0..4294967295` (floating-point representations such as `1.0` are rejected). Moves start at sequence 1 and strictly increase. Coordinates must be finite and inside `0..960` / `0..540`; the server chooses the spawn position.

A successful join queues accepted before its full snapshot and broadcasts the roster to joined peers. Each accepted move broadcasts a new authoritative roster. Disconnect destroys the session's Kernel entity and broadcasts the reduced roster. A reconnect rejoins as a fresh player with sequence 0 and a new ID; old identity and state are not resumed. Ping does not require joining.

Errors are `room_full`, `already_joined`, `not_joined`, `invalid_message` and `stale_sequence`. Invalid requests leave entity state unchanged. Unsupported/directionally invalid message IDs produce `invalid_message`; the sample header maps these IDs to internal callback key 60007 so Net's reserved IDs 1/65535 cannot enter its default SS control paths. This mapping is local dispatch, not an additional wire message. Application errors keep the session open unless work/output limits are exceeded. No failed movement replay, durable identity or reliable business acknowledgement is promised.
