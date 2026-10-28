# Redis restart recovery operations

[简体中文](restart_CN.md)

This opt-in C++ runner loads the real Redis plugin DSO through public APIs.
`check-database` compiles it without starting it or connecting to a server.
The original named pool remains configured throughout; recovery submits explicit
new read-only commands and never replays the interrupted operation. See the
[recovery behavior and local results](../../docs/guides/database-restart.md).

## Build and private service requirements

Build using the [database test configuration](README.md#build):

```sh
cmake --build database-build \
  --target ark_database_public_plugin_restart_tests --parallel 1
./database-build/tests/database/ark_database_public_plugin_restart_tests
```

Set `ARK_DATABASE_REDIS_HOST=127.0.0.1`, explicit
`ARK_DATABASE_REDIS_PORT` and nonempty `ARK_DATABASE_REDIS_PASSWORD` for a new,
private authenticated service. The runner refuses other hosts. It requires the
same `ARK_DATABASE_FAULT_DIR`, `ARK_DATABASE_FAULT_TOKEN`, ownership marker,
server identity and separately launched
[owned-server fault script](server_termination.md#private-actor-coordination).
Use that guide's fresh `/tmp/ark-dso-termination-owned.*` root, canonical executable,
private config/data/PID file, authenticated readiness and PID-specific listener
checks. Disable Redis persistence (`save ""`, `appendonly no`). Neither the runner
nor the fault script installs or discovers a service. Never use shared services or application
data. The existing fault script uses Unix process tools; this is not a Windows injector.

## Controlled restart sequence

1. Start the owned server, record its PID/start time/executable/data/listener
   identity, then launch the existing fault script and this C++ runner concurrently.
2. The runner verifies PING and the absence of its unique stream key. It starts
   read-only `XREAD BLOCK 60000 STREAMS <unique-key> $` with a 20-second deadline.
   A separate public observer pool must see the named client, `cmd=xread`, blocked
   flag and server client ID before publishing `redis.ready`.
3. The fault script verifies the owned identity again, signals only that PID with
   `SIGKILL`, and publishes `redis.fired`. The orchestration shell must reap the
   original child with exit 137 and verify that its PID and listener are absent.
4. The interrupted request must return `disconnected`, a native transport error,
   `command_may_have_run=true` and no replies. Cancellation and timeout are rejected.
   A new PING through `primary` while the service remains absent must return
   `connection_failed`, a transport error and `command_may_have_run=false`.
   Only then does the runner atomically publish `redis.down` containing the token.
5. Wait for `redis.down` and verify its token. Recheck the empty listener, start
   the same private config on the same endpoint, and record the replacement PID.
   Verify a different PID, its executable/start time/data/PID file/listener and an
   authenticated PONG. Atomically publish `redis.restarted` containing the token
   through a fresh `redis.restarted.tmp` file. Do not publish readiness from a timer.
6. The runner submits new PING and binary ECHO requests through the original
   `primary` pool. Both must succeed. Shutdown starts while this accepted recovery
   workflow is still pending; it must naturally drain, complete each root once
   on the owner's shared executor, reach stopped with zero pending tasks and
   accept repeated stop. Results remain owned after manager/runtime/DSO teardown.
7. Require runner and fault script exit zero. Shut down and reap the replacement child
   through its authenticated normal shutdown. Verify both exact PIDs and the
   endpoint listener are absent, then remove the owned directory, including
   credentials, data and all coordination markers. Keep a bounded supervisor and
   identity-checked cleanup trap for failures as well.

All waits in the C++ host are bounded. Observation polling merely throttles
checks. The shell owns restart and process authority; the runner only publishes
observations and consumes its private coordination markers. Never reuse markers.
The runner creates no keys or streams and performs no application writes.
