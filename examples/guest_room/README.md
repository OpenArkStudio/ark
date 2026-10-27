# Guest room example

[中文](README_CN.md)

`guest_room_plugin` runs a small 2D guest lobby through the real ARK launcher, Utility, Kernel, Bus and Net plugins. A joined session owns a `GuestPlayer` Kernel Entity whose formal fields and initial profile come from JSON. Movement and HP/MP actions update that Entity; snapshots read its named schema fields. Net callbacks and all business mutations execute on the launcher owner thread. Login and the default CS/SS framing and 4096-byte limit are unchanged. No Registry, Consul, database, account or persistence service is required.

The matching C# Godot client uses ARK2 and displays HP/MP. `damage` reduces own HP by 15, `skill` spends 20 MP when available, and `rest` restores 20 HP/MP up to the configured maxima. HP zero still permits movement. The server owns amounts and targets; the client requests only a kind and an independent action sequence. See the [protocol and v1 migration](protocol.md).

## Entity schema and initial profile

[resource/meta/entity_schema.json](resource/meta/entity_schema.json) is the authoritative Kernel schema and static configuration. It declares the required empty `Player` class, `GuestPlayer` fields at stable indices 100..108 (`name`, `x`, `y`, `move_seq`, `hp`, `mp`, `hp_max`, `mp_max`, `action_seq`), and profile identifier `id` at index 99. Name is a string, coordinates are doubles, and sequences/attributes are uint32. This replaces the example's placeholder XML metadata and custom fields.

The `GuestPlayer` configuration row with `id = 1` supplies every scalar value, including HP/MP/maxima of 100 and both sequences zero. Joining calls `create_entity(..., "GuestPlayer", 1, ...)` and then assigns the session name and spawn coordinates through ordinary Entity setters. Schema definitions determine field types and indices; business JSON rows provide values. There are no hardcoded replacement attribute defaults or runtime schema edits. The profile ID is separate from the opaque session player ID in snapshots. Schema masks are metadata; this demonstration module explicitly builds and broadcasts full snapshots after accepted mutations rather than enabling automatic Entity replication.

Edit a profile through the framework's [JSON Entity schema and business-data workflow](../../docs/reference/kernel.md#json-entity-schema-and-business-data), export valid JSON, stage it and restart the example. HP/MP must not exceed their maxima and initial sequences must be zero. Kernel selects this file when present and rejects malformed selected JSON without falling back to XML. The example checks the required field indices/types and profile in `pre_update()`, after every module's `post_init()` has loaded its static configuration, before opening its listener. Restart/rejoin creates fresh state; there is no hot reload or persistence.

## Build and run

Use the repository's normal native dependencies, including the existing optional `nlohmann_json` CMake package. Enable applications, examples and this optional example in the same build:

```sh
cmake -S . -B app-build -DARK_BUILD_APPLICATIONS=ON -DARK_BUILD_EXAMPLES=ON -DARK_BUILD_GUEST_ROOM_EXAMPLE=ON
cmake --build app-build --target app guest_room_plugin ark_kernel_plugin ark_utility_plugin ark_bus_plugin ark_net_plugin --parallel 1
./examples/guest_room/run.sh --bin-dir "$PWD/app-build/bin" --runtime-dir "$PWD/run/guest_room" --host 127.0.0.1 --port 19100
```

Supply the normal dependency toolchain/prefix arguments when configuring; select the `guest-room` vcpkg manifest feature when using vcpkg. Add `--debug` to the run command for a Debug build (`app_d` and `_d` plugins). The script stages a complete matching binary directory, the TOML manifests and the example's metadata. `--host` is a local bind address/hostname; `--port` must be 1..65535. Use a fresh runtime directory. Stop with Ctrl+C. The launcher retains resources and reports a failure if its five-second shutdown deadline is exceeded.

The equivalent platform-neutral staging command is:

```sh
cmake -DBIN_DIR=/absolute/app-build/bin -DRUNTIME_DIR=/absolute/run/guest_room -DHOST=127.0.0.1 -DPORT=19100 -P examples/guest_room/stage.cmake
cd /absolute/run/guest_room
./bin/app --busid=1.1.31.1 --name=guest_room --app_conf=conf/guest_room.app.toml --log_conf=conf/log.conf --shutdown_timeout_ms=5000
```

Use `app.exe` / `app_d.exe` on Windows. Connect the official [C# Godot client](https://github.com/OpenArkStudio/ark-client-godot) to the same host/port; send movement at about 10 Hz. The client must join again after reconnecting, clear old frame/state buffers and accept a new player ID. Detailed wire fields and errors are in the [protocol](protocol.md); [wire-vectors.json](wire-vectors.json) mirrors the C# test fixture.

## Native verification

```sh
cmake --build app-build --target check-guest-room --parallel 1
```

`check-guest-room-codec` checks shared ARK2 golden frames, every single split point, coalesced frames, the 8192/8193-byte boundary, old-marker rejection, invalid magic/flags, fresh parser state and numeric/name bounds using the real Net session parser. Its worst-case 16-player snapshot includes escaped names, 20-digit player IDs, doubles and uint32 attributes, and exceeds the old 4096-byte budget.

On POSIX systems, `check-guest-room-server` launches the real `app`, loads the actual plugin DSOs and exercises native TCP peers: join, authoritative movement/actions, independent sequences, malformed/stale/resource errors without mutation, clamped no-op acknowledgements, HP zero movement, disconnect/rejoin with fresh configured defaults, invalid-frame close and normal shutdown. Additional launches load changed JSON profiles near UINT32_MAX and at 19 MP to check overflow-safe recovery and the resource boundary. Invalid initial bounds/sequences must fail startup. It stages an isolated runtime and uses local port 19191; run one instance at a time. The process controller is not a Windows runtime acceptance check.

The Applications workflow enables this optional example on its existing platform matrix. Its default build includes the plugin on Linux, macOS and Windows; `check-core` includes the codec checks everywhere and the real launcher/two-peer process checks on POSIX systems. Workflow configuration is separate from the result of an individual hosted run.

The application bounds frames to 8192 bytes, connections to 32, joined players to 16, business callbacks to 64 per tick, per-peer messages to 32 per second and its own pending output to eight frames per peer. Exceeding work/output limits closes the affected peer. JSON nesting is limited to eight levels. The sample retains the underlying transport's send-queue limit and closes peers whose sends are rejected. These are demonstration limits; no comprehensive hostile-client hardening, production-capacity, public deployment or Godot visual acceptance is implied by native tests.
