# Guest room example

[中文](README_CN.md)

`guest_room_plugin` runs a small 2D guest lobby through the real ARK launcher, Utility, Kernel, Bus and Net plugins. A joined session owns a `GuestPlayer` Kernel Entity whose formal fields and initial profile come from JSON. Movement and HP/MP actions update that Entity; the owner-thread flush captures selected fields and publishes Protobuf baselines/deltas. Net callbacks and all business mutations execute on the launcher owner thread. Login and the default CS/SS framing and 4096-byte limit are unchanged. No Registry, Consul, database, account or persistence service is required.

The matching C# Godot client uses ARK2 and displays HP/MP. `damage` reduces own HP by 15, `skill` spends 20 MP when available, and `rest` restores 20 HP/MP up to the configured maxima. HP zero still permits movement. The server owns amounts and targets; the client requests only a kind and an independent action sequence. See the [protocol and migration](protocol.md).

## Entity schema and initial profile

[resource/meta/entity_schema.json](resource/meta/entity_schema.json) is the authoritative Kernel schema and static configuration. It declares the required empty `Player` class, `GuestPlayer` fields at stable indices 100..108 (`name`, `x`, `y`, `move_seq`, `hp`, `mp`, `hp_max`, `mp_max`, `action_seq`), and profile identifier `id` at index 99. Name is a string, coordinates are doubles, and sequences/attributes are uint32. This replaces the example's placeholder XML metadata and custom fields.

The `GuestPlayer` configuration row with `id = 1` supplies every scalar value, including HP/MP/maxima of 100 and both sequences zero. Joining calls `create_entity(..., "GuestPlayer", 1, ...)` and then assigns the session name and spawn coordinates through ordinary Entity setters. Schema definitions determine field types and indices; business JSON rows provide values. There are no hardcoded replacement attribute defaults or runtime schema edits. The profile ID is separate from the opaque session player ID in snapshots. Schema masks are metadata; the module explicitly selects public indices 100..108 for `capture_scalar_snapshot` and `scalar_publisher`. New connections receive a complete baseline; existing peers receive added baselines, changed fields and tombstones. Movement and actions coalesce before each owner-thread flush.

Edit a profile through the framework's [JSON Entity schema and business-data workflow](../../docs/reference/kernel.md#json-entity-schema-and-business-data), export valid JSON, stage it and restart the example. HP/MP maxima must be positive, current values must not exceed their maxima and initial sequences must be zero. Kernel selects this file when present and rejects malformed selected JSON without falling back to XML. The example checks the required field indices/types and profile in `pre_update()`, after every module's `post_init()` has loaded its static configuration, before opening its listener. Restart/rejoin creates fresh state; there is no hot reload or persistence.

## Stop new business requests

Join, Move and Action use `reg_external_root_callback()` to check manager state when their owner-thread callbacks begin. Once the manager leaves `running`, new business roots receive the existing Protobuf `error_response` with code `server_stopping`, without changing membership, coordinates, HP/MP or sequences. An Action refusal echoes its `action_sequence` so the C# client can settle the matching pending action. Ping, unsupported-message handling and connection events retain ordinary callbacks; drain updates can still deliver heartbeats, disconnect tombstones and accepted-work completions.

Refusals share the existing 32-message-per-second, 64-callback-per-tick and eight-frame-per-peer work/output limits; overflow still closes the session. Refused Join/Move bodies are not parsed. Action is parsed only to correlate its sequence; malformed Action, heartbeat and unsupported messages still close the session. The gate does not pause TCP accept or guarantee native send completion, callback unregistration or library unload. Shared IO, Registry, hot update, nested wire changes and the TCP 5000 workstream remain paused.

## Build and run

Use the repository's normal native dependencies, including Protobuf and the existing optional `nlohmann_json` CMake package. Enable applications, examples and this optional example in the same build:

```sh
cmake -S . -B app-build -DARK_BUILD_APPLICATIONS=ON -DARK_BUILD_EXAMPLES=ON -DARK_BUILD_GUEST_ROOM_EXAMPLE=ON -DARK_BUILD_PROTO=ON
cmake --build app-build --target app guest_room_plugin ark_kernel_plugin ark_utility_plugin ark_bus_plugin ark_net_plugin --parallel 1
./examples/guest_room/run.sh --bin-dir "$PWD/app-build/bin" --runtime-dir "$PWD/run/guest_room" --host 127.0.0.1 --port 19100
```

Supply the normal dependency toolchain/prefix arguments when configuring; select the `guest-room` vcpkg manifest feature when using vcpkg. Add `--debug` to the run command for a Debug build (`app_d` and `_d` plugins). The script stages a complete matching binary directory, the TOML manifests and the example's metadata. `--host` is a local bind address/hostname; `--port` must be 1..65535. Use a fresh runtime directory. Stop with Ctrl+C. The launcher retains resources and reports a failure if its five-second shutdown deadline is exceeded.

The equivalent platform-neutral staging command is:

```sh
cmake -DBIN_DIR=/absolute/app-build/bin -DRUNTIME_DIR=/absolute/run/guest_room -DHOST=127.0.0.1 -DPORT=19100 -P examples/guest_room/stage.cmake
cd /absolute/run/guest_room
./bin/app --busid=1.1.31.1 --name=guest_room --app_conf=conf/guest_room.app.toml --shutdown_timeout_ms=5000
```

Use `app.exe` / `app_d.exe` on Windows. Connect the official [C# Godot client](https://github.com/OpenArkStudio/ark-client-godot) to the same host/port; send movement at about 10 Hz. The client must join again after reconnecting, clear old frame/state buffers and accept a new player ID and actual Entity GUID. Detailed wire fields and errors are in the [protocol](protocol.md); [wire-vectors.json](wire-vectors.json) mirrors the C# test fixture.

## Native verification

```sh
cmake --build app-build --target check-guest-room --parallel 1
```

`check-guest-room-codec` uses the real Net parser to verify shared Protobuf body/frame hex, every split point, coalescing, 8192/8193-byte limits, headers, fresh parser state and strict UTF-8 names. Adapter tests cover all nine scalar alternatives, integer boundaries, arbitrary bytes, zero-value presence, duplicate fields, limits, malformed encodings, public projection types, revision gaps, duplicates, tombstones and revision exhaustion. A 16-player worst-size Protobuf baseline must fit the frame budget.

On POSIX, `check-guest-room-server` starts the real launcher/plugin libraries and two native TCP clients. It checks Accepted/Entity binding, late-join full baselines, added baselines for existing peers, coalesced movement deltas, HP/MP actions, sequence-only clamped actions, quiet unchanged flushes, errors without state changes, tombstones, reconnect with new Entity GUIDs, malformed-frame shutdown and normal shutdown. Additional processes load near-UINT32_MAX and 19-MP profiles and reject invalid initial profiles. Tests own runtime directories and port 19191; run one copy at a time. This process controller does not establish Windows or C# Godot acceptance.

The same test executable also starts an admission child with a fixed 20-second observation deadline. Actual Room/Kernel/Net DSOs and a controlled accepted task hold a drain while native TCP requests check before-stop join/movement/actions, correlated refusals and unchanged snapshots, heartbeats, malformed/control closure, disconnect tombstones, owner-thread completion and exact eight-frame/32-per-second/64-per-tick limits. Individual socket operations retain three-second deadlines. The controller terminates and reaps only its own child on timeout; that is not normal framework shutdown. The controlled host links the room plugin for concrete type access and keeps the DSO loaded throughout the process; this check does not establish library unmapping.

The Applications workflow enables this optional example on its existing platform matrix. Its default build includes the plugin on Linux, macOS and Windows; `check-core` includes the codec checks everywhere and the real launcher/two-peer process checks on POSIX systems. Workflow configuration is separate from the result of an individual hosted run.

The sample limits bodies to 8192 bytes, connections to 32 and joined players to 16. Each tick permits 64 business callbacks; each connection permits 32 messages per second and eight queued frames / `8 * (8192 + 12)` owned bytes. Work/output overflow or transport send rejection closes the session. Unsupported captured state or revision exhaustion requires reconnection and a fresh baseline. These bounds and native tests do not establish production capacity, deployment or Godot visual acceptance. See the [protocol](protocol.md) for compatibility, projection, revision and recovery rules.
