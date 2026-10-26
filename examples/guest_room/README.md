# Guest room example

[中文](README_CN.md)

`guest_room_plugin` runs a small 2D guest lobby through the real ARK launcher, Utility, Kernel, Bus and Net plugins. A joined session owns a `GuestPlayer` Kernel entity. Movement updates the server entity; snapshots read its custom name, position and sequence fields. The sequence uses the supported Kernel custom int64 setter while preserving the protocol's unsigned 32-bit bounds. Net callbacks and all business mutations execute on the launcher owner thread. Login and the default CS/SS framing are unchanged. No Registry, Consul, database, account or persistence service is required.

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

Use `app.exe` / `app_d.exe` on Windows. Connect the C# Godot demo to the same host/port; send movement at about 10 Hz. The client must join again after reconnecting, clear old frame/state buffers and accept a new player ID. Detailed wire fields and errors are in the [protocol](protocol.md); [wire-vectors.json](wire-vectors.json) mirrors the C# test fixture.

## Native verification

```sh
cmake --build app-build --target check-guest-room --parallel 1
```

`check-guest-room-codec` checks the shared golden frames, every single split point, coalesced frames, the 4096/4097-byte boundary, invalid magic/flags, fresh parser state and numeric/name bounds using the real Net session parser. On POSIX systems, `check-guest-room-server` launches the real `app`, loads the actual plugin DSOs and exercises two native TCP peers: join, both directions of authoritative movement, stale and malformed requests, bounded errors, disconnect, reduced roster, fresh rejoin, invalid-frame close and normal process shutdown. It stages an isolated runtime and uses local port 19191; run one instance at a time. The process test uses POSIX child-process control and is not a Windows runtime acceptance check.

The application bounds frames to 4096 bytes, connections to 32, joined players to 16, business callbacks to 64 per tick, per-peer messages to 32 per second and its own pending output to eight frames per peer. Exceeding work/output limits closes the affected peer. JSON nesting is limited to eight levels. The sample retains the underlying transport's send-queue limit and closes peers whose sends are rejected. These are demonstration limits; no comprehensive hostile-client hardening, production-capacity, public deployment or Godot visual acceptance is implied by native tests.
