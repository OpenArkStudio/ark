# Database restart recovery operations

[简体中文](restart_CN.md)

This opt-in C++ runner loads the real Redis or MySQL plugin DSO through public APIs.
`check-database` compiles it without starting it or connecting to a server.
The original named pool remains configured throughout; recovery submits explicit
new read-only commands and never replays the interrupted operation. See the
[recovery behavior and local results](../../docs/guides/database-restart.md).

## Build and private service requirements

Build using the [database test configuration](README.md#build):

```sh
cmake --build database-build \
  --target ark_database_public_plugin_restart_tests --parallel 1
./database-build/tests/database/ark_database_public_plugin_restart_tests redis
# With ARKNET_ENABLE_SSL=ON:
./database-build/tests/database/ark_database_public_plugin_restart_tests mysql
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

## MySQL restart with trusted TLS

The optional `mysql` mode requires `ARKNET_ENABLE_SSL=ON`; no argument retains
Redis mode. Set `ARK_DATABASE_MYSQL_HOST=127.0.0.1`, explicit
`ARK_DATABASE_MYSQL_PORT`, `ARK_DATABASE_MYSQL_USER`, nonempty
`ARK_DATABASE_MYSQL_PASSWORD`, `ARK_DATABASE_MYSQL_DATABASE` and
`ARK_DATABASE_MYSQL_CA`. Use a fresh MySQL 8.4 data directory, private schema and
SELECT-only account with required TLS. The trusted certificate must contain the
loopback IP. Disable MySQL X Protocol and bind only the owned IPv4 loopback
listener. Use the same private actor ownership/identity checks as above, with
`mysql` markers and the MySQL config requirements in the fault guide.

1. Verify authenticated TLS readiness. The C++ host preflights `primary` and
   `observer` with `SELECT 1`, then submits `SELECT SLEEP(60) /*<unique-tag>*/`
   with a 20-second client deadline. A separate public observer pool must see the
   tag, connection ID and `User sleep` state in `information_schema.processlist`
   before publishing `mysql.ready`.
2. The actor rechecks the owned PID and sends only that PID `SIGKILL`. Reap the
   original child with exit 137 and verify the PID/listener are absent. The
   interrupted result must have native `transport`, `outcome_unknown=true`, a
   transport error and no rows/server error. Timeout and cancellation fail.
3. A new `SELECT 1` while the server is absent must return `transport`, native
   `connection_refused`, `outcome_unknown=false` and no rows/server error. Only
   then does the host publish `mysql.down`.
4. Verify its token and the empty listener. Restart the same private data/config
   at the same endpoint. Verify the replacement PID differs, its owned identity
   and authenticated TLS readiness, then atomically publish `mysql.restarted`.
5. Through the original `primary` pool, the caller submits a new
   `SELECT CAST(42 AS SIGNED), CONCAT('restart', CHAR(0), 'binary')`.
   Require the integer and embedded-NUL string, natural drain of accepted work,
   exactly-once owner-executor completions and idempotent stop. Both native error
   results and the owning recovery row are checked again after DSO teardown.
6. Require zero exits, normally shut down and reap the replacement service,
   verify both PIDs and listener absent, and remove all private credentials,
   data and markers. Keep failure cleanup identity-checked and bounded.

The C++ host uses public `mysql_module` coroutines and retains the same named
pools without reconfiguration. Recovery is an explicitly new read-only query;
there is no automatic write replay. This checks reconnect behavior, not crash
durability, transaction recovery or high availability.
