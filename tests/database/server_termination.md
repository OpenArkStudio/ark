# Owned-server termination through public plugin DSOs

[简体中文](server_termination_CN.md)

`ark_database_public_plugin_server_termination_tests redis|mysql` loads the actual
hidden-symbol plugin DSO using only public API targets. `check-database` compiles
this opt-in program but never starts it or signals a server. MySQL requires
`ARKNET_ENABLE_SSL=ON` and trusted TLS. The separate Bash actor requires `lsof`,
`ps`, `rg` and the private service layout below; it targets owned local Unix
processes and does not provide a Windows fault injector.

Build with the [database test configuration](README.md#build):

```sh
cmake --build database-build \
  --target ark_database_public_plugin_server_termination_tests --parallel 1
```

## Workload and assertions

Redis sends a single read-only `XREAD BLOCK 60000 STREAMS <exclusive-key> $`.
Preflight checks that the stream key does not exist; the test creates no stream
or values. A separate authenticated observer connection reads `CLIENT LIST` and
must find the exact named primary connection with `cmd=xread`, blocked flag and
server client ID. MySQL sends `SELECT SLEEP(60) /*<unique-tag>*/` against a private
schema. A separate trusted-TLS connection to the same owned server must observe
that exact tag and connection ID in `information_schema.processlist`, in
`User sleep`. Only then does the C++ host publish the private fault barrier.
Observer polling delays are throttling, never proof of active server work.

The separate shell actor rechecks the ownership token, captured PID/start time,
canonical executable text mapping, private data directory working directory,
PID file and PID-specific IPv4 loopback listener before sending `SIGKILL` to that
single PID. MySQL's defaults-file argument and the private configuration/data
mapping are checked too. The parent must collect that child's intentional
signal exit, the actor's successful receipt and the host exit; a marker alone
does not prove process termination.

The pending request has a 20-second operation timeout; the server block is
60 seconds. Expected failures are Redis `disconnected` with
`command_may_have_run=true`, and MySQL `transport` with
`outcome_unknown=true`, each retaining a nonzero native transport error.
Cancellation, operation timeout and MySQL server SQL errors are rejected.
These are service results; a coroutine exception fails the test. The request and
observer roots complete exactly once on the owner's shared executor, retire
before stop, and preserve the owning native result after module/manager
teardown and DSO unload. Cooperative stop is bounded and idempotent. No pending
request is cancelled to inject the failure; the retired task ID rejects cancel.

## Private actor coordination

Use fresh disposable service instances and private credentials, on IPv4
loopback only. MySQL needs a fresh schema/account with SELECT permission and
required trusted TLS with the loopback IP in the certificate. Do not point the
actor at a shared or installed service. This test does not restart servers or
inspect persistence, transaction rollback or write effects.

Before launching a server, create a new private root and token:

```sh
umask 077
private_root=$(mktemp -d /tmp/ark-dso-termination-owned.XXXXXX)
export ARK_DATABASE_FAULT_DIR=$private_root
export ARK_DATABASE_FAULT_TOKEN=$(openssl rand -hex 32)
printf '%s\n' "$ARK_DATABASE_FAULT_TOKEN" > "$private_root/ownership"
```

Create config/data/PID-file paths under this root. Redis's config must contain
exact `dir <datadir>` and `pidfile <pidfile>` lines; MySQL's config must contain
`datadir=<datadir>` and `pid-file=<pidfile>` and start with
`--defaults-file=<config>`. Launch in the foreground as a child of the
orchestration shell, capture `$!`, and verify readiness with authenticated
protocol requests and the PID's listener. Do not use elapsed sleep as readiness.
Both servers must use their private data directory as their working directory.

After readiness, record the identity from that owned child. The executable path
must be canonical (resolve the directory with `pwd -P`), while `datadir`,
`config`, `pidfile` use the original private-root spelling:

```sh
printf '%s\n%s\n%s\n%s\n%s\n' "$ARK_DATABASE_FAULT_TOKEN" "$service_pid" \
  "$canonical_executable" "$datadir" "$(ps -p "$service_pid" -o lstart=)" \
  > "$private_root/$mode.identity"
# mode is redis or mysql; port belongs only to this owned child.
bash tests/database/owned_server_fault_actor.sh "$mode" "$service_pid" \
  "$canonical_executable" "$config" "$datadir" "$pidfile" "$port" > "$actor_log" 2>&1 &
actor_pid=$!
```

Run the C++ host concurrently with the actor. Supply the same endpoint variables
as [public DSO service acceptance](README.md#real-plugin-dsos-against-real-services):
Redis host/port/password and optional username; MySQL host/port/user/password/
database/CA. Keep credentials in private files or environment, never command
arguments or committed logs. Set `mode` to the protocol being tested:

```sh
./database-build/tests/database/ark_database_public_plugin_server_termination_tests "$mode"
host_status=$?
wait "$actor_pid"; actor_status=$?
wait "$service_pid"; service_status=$?
# Require host_status=0, actor_status=0, service_status=137 (SIGKILL on Bash).
```

Capture nonzero signal exits explicitly if the orchestration uses `set -e`.
Wrap the whole run in a bounded shell supervisor. Missing/mismatched/stale
markers fail; the actor refuses to signal without a valid active barrier within
15 seconds. Keep a cleanup trap: stop an un-faulted verified child through its
authenticated normal shutdown; after either path, reap all owned children,
verify their exact PIDs are absent, then remove the token-guarded private root,
including credentials, data and private TLS keys. Never reuse fault markers.

This covers the observed peer-loss request and local retirement. It does not
establish restart/recovery, persistence, automatic replay behavior, all fault
races, deployed-service acceptance, cross-host behavior or capacity. The precise
native transport error depends on the platform; no exact EOF/reset numeric value
is required. Preserve local identities/results/cleanup evidence outside Git.
