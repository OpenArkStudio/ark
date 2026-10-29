# Database service acceptance records

[Acceptance index](../guides/database-validation.md)

This record separates public-module service/failure checks, positive plugin-DSO workflows, MySQL DSO write transactions, server-observed request cancellation, sent MySQL transaction-query and prepared-execute cancellation, owned-server termination and verified Redis TLS. Each scope has its own tested revisions and environment; a passing result applies only to that scope.

## Public modules against real services

The four public service/failure invocations passed on **2026-10-21** using isolated local servers.

| Environment | Value |
| --- | --- |
| Machine | Mac mini · Apple M4 Pro · 14 cores · 48 GiB |
| System | macOS 27.0.1 (26A434) |
| Build | Apple Clang 21.0.0 · C++20 · Boost.Asio · Debug |
| ark | `61ad91ae59e93391e03d685fa01ddf02ba0ae98e` |
| arknet | v0.2.2 · `c5432ca176edf9f4c06db03713f03764184c5cea` |
| Services | Redis 7.4.2 over authenticated TCP; MySQL 8.4.11 with authentication and verified TLS |

| Program mode | Result | Coverage |
| --- | --- | --- |
| Public service · Redis | Passed | Binary pipeline, follow-up requests, input/result ownership, owner-thread resumption and drain |
| Public service · MySQL | Passed | Query/prepared results, commit/rollback, ownership, owner-thread resumption and drain |
| Public failure · Redis | Passed | Wrong password, server-observed blocking-command timeout and explicit-request recovery |
| Public failure · MySQL | Passed | Wrong password, untrusted CA, server-observed slow-query timeout and explicit-request recovery |

Both normal workflows also passed host-stack unwinding while requests were pending. Server processes and data were dedicated to these tests; the processes were stopped after completion. Reproduce using the [public service](../../tests/database/README.md#public-modules-against-real-services) and [failure](../../tests/database/README.md#public-real-service-failure-checks) commands. Results cover these functional scenarios on one machine; cross-machine capacity, request cancellation, server termination and dynamic-library unloading require separate acceptance.

## Real plugin DSOs against real services

Both positive modes of `ark_database_public_plugin_service_tests` passed on **2026-10-26**, each with exit code `0`.

| Environment | Value |
| --- | --- |
| Machine | Mac16,11 · Apple M4 Pro · 14 cores · 48 GiB |
| System | macOS 27.0.1 (26A434) · arm64 |
| Build | Apple Clang 21.0.0 · C++20 · Boost.Asio · Debug · OpenSSL 3.6.4 |
| ark | `9923f33b230e052cacceb09bff21f80c95c5027f` |
| arknet | CI pin `a0dfe09cd543e75cfb5996e537ce2fa49f2a758f` |
| Services | Redis 7.4.2 over authenticated TCP; MySQL Community Server 8.4.13 with required TLS, a trusted CA and IP identity verification |

The host loads the actual hidden-symbol Redis or MySQL plugin DSO through the normal manager and uses only public module, manager and task interfaces. It does not link the module implementation objects.

| Program argument | Result | Protocol coverage |
| --- | --- | --- |
| `redis` | Passed · exit `0` | Ordered binary/empty ECHO replies, PING and an explicit follow-up command |
| `mysql` | Passed · exit `0` | Signed/text/NULL query fields, binary prepared results, independent statement-close diagnostics and read-only transaction queries followed by confirmed commit and rollback |

In both workflows, the manager closes top-level task admission while an accepted root is pending before its first owner poll. Real operations and later awaits then finish during natural drain. Every continuation and completion runs on the owner thread/shared executor, the root completes once, and repeated stop succeeds. Owning results remain valid after the database module, manager and runtime are destroyed. No task-cancellation shortcut is used.

The services used independent loopback instances with fresh private data directories and authentication. MySQL had a dedicated schema and SELECT-only account requiring SSL. Both owned processes shut down successfully; private data, authentication files and TLS private keys were removed. The workflows make no key/table writes and do not verify transaction persistence or isolation.

### Reproduce the positive DSO workflows

Prepare isolated services, the `ARK_DATABASE_*` endpoint/authentication variables, a MySQL schema and trusted CA using the [real-plugin service instructions](../../tests/database/README.md#real-plugin-dsos-against-real-services). With the SSL-enabled database build configured:

```sh
cmake --build database-build --target ark_database_public_plugin_service_tests --parallel 1
./database-build/tests/database/ark_database_public_plugin_service_tests redis
./database-build/tests/database/ark_database_public_plugin_service_tests mysql
```

These are standalone C++ test modes. They do not create or stop services; missing configuration or an unavailable protocol fails rather than becoming a successful skip. `check-database` builds this program and its DSOs without running the real-service modes.

## MySQL DSO writes and two-connection commit visibility

On **2026-10-29**, the `mysql-transactions` mode of `ark_database_public_plugin_service_tests` passed with exit code `0`. The executed ark source was `4452acaeed6a3834d048f025f2970825803402bb`; this record was added afterward without changing the executable source. The local arknet source matched CI pin `a0dfe09cd543e75cfb5996e537ce2fa49f2a758f`. The machine was Mac16,11 / Apple M4 Pro / 14 cores / 48 GiB, running macOS 27.0.1 (26A434) arm64. The C++20 Debug build used Apple Clang 21.0.0, Boost.Asio 1.90.0 and OpenSSL 3.6.4; the private server was MySQL Community Server 8.4.13 with required TLS, trusted CA and IP identity verification.

The C++ host loaded the actual hidden-symbol MySQL plugin DSO and used only public coroutine APIs. It created a unique InnoDB table in a dedicated schema. Parameterized autocommit and transaction inserts preserved binary values containing NUL, a quote and a backslash, with successful statement-close diagnostics. Two public connection configurations had distinct server connection IDs, and the observer was in autocommit mode. Both transactions read their own writes while the observer saw no corresponding row before completion. After confirmed commit, the observer saw the committed binary row; after confirmed explicit rollback, it saw no rolled-back row. Retained query and transaction results remained readable after subsequent operations, table removal and module/manager/runtime destruction. Every continuation used the owner thread/shared executor, and the accepted workflow finished through natural manager drain and idempotent stop.

The service had a fresh private data directory, dedicated schema and SSL-required account restricted to CREATE, DROP, SELECT and INSERT on that schema. A separate cleanup query confirmed zero remaining tables. The owned server shut down through authenticated `mysqladmin shutdown` and was reaped with exit code `0`; independent checks confirmed its exact PID and loopback listener were absent. Private data, credentials and TLS keys were removed. Missing-host and non-loopback-host controls both failed with exit code `1` before connecting.

Prepare the private service and environment using the [write-mode instructions](../../tests/database/README.md#real-plugin-dsos-against-real-services), then run:

```sh
cmake --build database-build --target ark_database_public_plugin_service_tests --parallel 1
./database-build/tests/database/ark_database_public_plugin_service_tests mysql-transactions
```

This is single-node, two-connection commit-visibility acceptance. It does not verify every isolation level, transaction cancellation, crash durability or restart recovery. Normal server shutdown is cleanup, not failure-recovery evidence. No new database protocol, connection-pool policy, retry or forced shutdown was added. `check-database` compiles this opt-in mode without starting a service; compilation alone does not reproduce this result.

## Real plugin request cancellation

Both modes of `ark_database_public_plugin_cancellation_tests` passed on **2026-10-26**, each with exit code `0`. The host uses only public interfaces and loads the actual hidden-symbol database plugin DSOs.

| Environment | Value |
| --- | --- |
| Machine | Mac16,11 · Apple M4 Pro · 14 cores · 48 GiB |
| System | macOS 27.0.1 (26A434) · arm64 |
| Build | Apple Clang 21.0.0 · C++20 · Boost.Asio · Debug · SSL enabled · OpenSSL 3.6.4 |
| Executed ark | `dba34762dd58348784e46a2518f7505e52063d55` |
| arknet | CI pin `a0dfe09cd543e75cfb5996e537ce2fa49f2a758f` |
| Services | Redis 7.4.2 over authenticated TCP; MySQL Community Server 8.4.13 with required verified TLS |

Cancellation is requested through the existing `task_group::cancel(id)` only after an independent pool observes the uniquely identified operation on the server. Polling timers only throttle observations; elapsed time is not the active-operation barrier.

| Mode | Server observation before cancellation | Original native result and observed effect |
| --- | --- | --- |
| `redis` | `CLIENT LIST` identified the named pipeline blocked in BLPOP after its preceding INCR counter became `1` | `client_errc::cancelled`, `command_may_have_run=true`, no transport error and retained integer partial reply `1`. Explicit PING recovered; the observed block retired and the exclusive counter remained `1`. |
| `mysql` | `PROCESSLIST` identified the tagged UPDATE in `User sleep` | `connection_errc::cancelled`, `outcome_unknown=true` and `operation_aborted` transport status. An explicit query recovered; the observer waited for the tagged statement to disappear across all states and a bounded quiet window before reading the final counter, which was `1`. |

The cancelled values remain service results, not coroutine exceptions. Request and observer roots each complete once on the owner thread/shared executor. Repeated cancellation of a pending task is accepted; its retired ID rejects cancellation. Both controlled tests complete natural managed/native shutdown within their observation and pumping bounds; repeated stop succeeds, and owning cancelled results remain readable after plugin/module/manager/runtime destruction. These are test guards, not a hard or preemptive production deadline: `manager.stop()` remains cooperative and cannot interrupt blocking hooks. No production timeout or cancellation policy is added, and `cancel_all` or forced frame destruction is not used for teardown.

The final counters describe only the observed operations and finite observation window. The MySQL case permits `0` or `1` while the outcome is unknown; this run observed `1`. Cancellation does not establish rollback, global exactly-once execution or every possible retry race.

Use the [standalone C++ cancellation guide](../../tests/database/cancellation.md) to prepare exclusively owned services, private authentication and the required database privileges. These modes write exclusive Redis keys or a private MySQL counter table; do not use shared services. With the SSL-enabled database build configured:

```sh
cmake --build database-build --target ark_database_public_plugin_cancellation_tests --parallel 1
./database-build/tests/database/ark_database_public_plugin_cancellation_tests redis
./database-build/tests/database/ark_database_public_plugin_cancellation_tests mysql
```

`check-database` compiles the program without running the real-service modes. Both owned servers were shut down normally using authenticated commands; the exact processes were independently confirmed absent and private data, credentials and TLS keys were removed. Server termination was not a test action.

## MySQL sent transaction-operation cancellation

On **2026-10-29**, the `mysql-transaction` mode of
`ark_database_public_plugin_cancellation_tests` passed with exit code `0`.
The program source was byte-identical at the executed revision, delivery head and
original integration tree; that documentation integration was not another service run.

| Identity / environment | Value |
| --- | --- |
| Executed ARK program source | `28edd1a88c5e7179668aa387a9158d3db32a15f0` |
| Delivery head, program unchanged | `5ce07533495a2b2cbb6fc31f57131dab3952c15a` |
| Program Git blob | `d833ed13b68cc7c84e518ad094efa22381989fa0` |
| arknet | CI pin `a0dfe09cd543e75cfb5996e537ce2fa49f2a758f`; local source matched the pinned archive |
| Build | macOS 27.0.1 (26A434) arm64; Apple Clang 21.0.0; C++20 Debug; Boost.Asio 1.90.0; OpenSSL 3.6.4 |
| Private service | MySQL Community Server 8.4.13; `127.0.0.1:29889`; authentication, required trusted TLS and IP identity verification |
| Schema privileges | CREATE, DROP, SELECT, INSERT and UPDATE, confined to the private schema |
| Original transaction / observer connection IDs | `13` / `14` |

The public coroutine host loaded the actual hidden-symbol MySQL plugin DSO.
Its transaction completed an UPDATE and retained its own read of `n=1`, while
an independent observer read `n=0`. Before requesting `task_group::cancel(id)`,
the observer identified the exact original connection and tagged slow UPDATE in
`User sleep`. Repeated cancellation while pending was accepted; the retired task
ID rejected cancellation.

The original result retained native `connection_errc::cancelled`,
`outcome_unknown=true` and `operation_aborted`. The transaction status remained
`unconfirmed` with `discard_requested=true`, no rollback reply or rollback code,
and no cleanup, cancellation or lease-return exception. An explicitly new query
through the original named pool succeeded on a different connection. The observer
waited for the whole original session to disappear, then required another 500 ms
without that session or tagged SQL reappearing before reading final `n=0`.
A separate query of the private server general log counted one pre-write UPDATE
and one tagged slow UPDATE. These are bounded observations of the server effect;
they do not turn the API result into a confirmed rollback response or prove
persistence. No transaction replacement or automatic write replay was added.

Request and observer roots each completed once on the owner thread/shared
executor without coroutine errors. Natural retirement and idempotent stop
succeeded. The successful pre-write read remained `1`, and the cancelled reply
kept its original diagnostics after later operations, table removal and
module/manager/runtime destruction. Independent queries found no remaining test
tables. Authenticated shutdown reaped the owned MySQL process with exit code `0`;
its exact PID/listener and private data, credentials and TLS keys were removed.

The earlier `mysql` and `redis` modes also passed, each observing final counter
`1`; Redis used a separate authenticated 7.4.2 instance and had no remaining keys.
Wrong-password and untrusted-CA controls failed as intended. SSL-disabled builds
rejected both MySQL modes before service work. Targeted native regressions passed
23 cases and 1147 assertions, including public MySQL DSO lifecycle. These results
retain the original local macOS Boost Debug scope; they do not establish hosted
CI, another platform, AddressSanitizer, UndefinedBehaviorSanitizer or
ThreadSanitizer acceptance. Raw logs and source/binary hashes remain local.

Run the existing executable's `mysql-transaction` mode using the
[private service and cancellation instructions](../../tests/database/cancellation.md#mysql-transaction-operation-cancellation).
This record covers one sent transaction query. Cancellation during acquire,
BEGIN, prepare/execute/statement close, commit or rollback, other races,
transaction recovery, crash durability, high availability and capacity remain
outside it. Earlier query-only cancellation and commit-visibility records retain
their own tested identities and exclusions.

## MySQL prepared execute cancellation

On **2026-10-29**, the public DSO C++ runner's `mysql-statement` mode passed
with exit code `0`. The executed program source was
`30f963cade5162d8b3b474646dc8c123b987a5be` (Git blob
`2a880b3fd0476f045928082a29f01560e4a66803`); the subsequent documentation
change did not alter the program. The base was
`988dfbc048425cdb56cbe5f14314320fe698d07d`, with arknet CI pin
`a0dfe09cd543e75cfb5996e537ce2fa49f2a758f`.

The local macOS 27.0.1 arm64 C++20 Debug build used Apple Clang 21.0.0,
Boost.Asio 1.90.0 and OpenSSL 3.6.4. The exclusively owned MySQL Community
Server 8.4.13 at `127.0.0.1:29891` required authentication and trusted TLS with
IP identity verification. CREATE, DROP, SELECT, INSERT and UPDATE privileges
were confined to a fresh private schema.

The host loaded the real hidden-symbol plugin and called only public coroutine
APIs. A transaction completed a pre-write and retained its read of `n=1`, while
the independent observer read `n=0`. Only after session `10` was observed from
session `11` with the tagged parameterized UPDATE, `User sleep` and
`COMMAND='Execute'` did the host cancel the task, twice while pending.
The original result retained `connection_errc::cancelled`, `outcome_unknown=true`
and `operation_aborted`; transaction status remained `unconfirmed` and
`discard_requested=true`, without statement-close or rollback replies/codes or
initiation, statement-close, cleanup, cancellation or lease-return exceptions.

An explicit query through the original pool succeeded on a different connection.
The whole original session disappeared and neither it nor the tagged SQL returned
through another 500 ms; only then did the observer require final `n=0` and drop
the table. A separate general-log query counted one Prepare, one Execute and one
pre-write for this unique tag. These finite observations do not confirm rollback
through the API, durability or global exactly-once execution. No automatic retry
or transaction replacement was introduced.

Both roots completed once on the owner thread/shared executor, retired naturally
and passed idempotent stop. The retained read and original cancelled diagnostics
survived later operations, table removal and host destruction. The existing
`mysql-transaction` mode passed again (sessions `13`/`14`, final `n=0`).
Wrong-password and untrusted-CA controls rejected the request. SSL-disabled builds
rejected the new mode before service work; seven configuration/build-mode
rejection controls passed. Focused native regressions passed 23 cases and 1157
assertions, plus the public MySQL DSO lifecycle check. Independent queries
found zero remaining tables. Authenticated shutdown reaped exact owned PID
`17059` with exit `0`; its listener, private data, credentials and keys were removed.

Run the [existing C++ mode](../../tests/database/cancellation.md#mysql-prepared-execute-cancellation).
This covers one sent transaction prepared execution, not acquire, BEGIN, prepare,
statement close, commit or rollback cancellation, other execute races, crash
recovery, high availability or capacity. It is local macOS Boost Debug evidence;
other platforms, sanitizers, hosted CI and publication require separate acceptance.

## Real plugin server termination

Both modes of `ark_database_public_plugin_server_termination_tests` passed on **2026-10-26**. Each C++ host and independent fault actor exited `0`; each owned server's intentional `SIGKILL` exit was `137`.

| Environment | Value |
| --- | --- |
| Machine | Mac16,11 · Apple M4 Pro · 14 cores · 48 GiB |
| System | macOS 27.0.1 (26A434) · arm64 |
| Build | Apple Clang 21.0.0 · C++20 · Boost.Asio · Debug · SSL enabled · OpenSSL 3.6.4 |
| Executed ark | `0722b8bdd2e2c240657a3ceea63428cbbff0f821` |
| arknet | CI pin `a0dfe09cd543e75cfb5996e537ce2fa49f2a758f` |
| Services | Redis 7.4.2 over authenticated TCP; MySQL Community Server 8.4.13 with required verified TLS |

The host uses public interfaces and actual hidden-symbol plugin DSOs. An independent service connection must observe the exact active request before releasing a private fault barrier; a polling delay does not establish activity.

| Mode | Observed read-only request before the fault | Original native result after server termination |
| --- | --- | --- |
| `redis` | `CLIENT LIST` identified the exact named client blocked in `XREAD` | `client_errc::disconnected`, `command_may_have_run=true`, and EOF transport status |
| `mysql` | `PROCESSLIST` identified the tagged `SELECT SLEEP(60)` connection in `User sleep` | `connection_errc::transport`, `outcome_unknown=true`, and TLS stream truncation |

Before signaling the single owned PID, the separate shell actor resolves private paths physically and rejects escapes or symlink coordination files. It rechecks ownership, PID/start time, executable, private data working directory, PID file and that PID's loopback listener; MySQL also checks its private defaults-file argument. These guards apply to the disposable instances prepared for this test, not shared or installed services.

Both modes reject task cancellation, operation timeout, SQL server error and coroutine exceptions. Request and observer roots complete once on the owner thread/shared executor and retire before cooperative stop. The controlled tests complete natural shutdown within their observation and pumping bounds; repeated stop succeeds, and owning native results/errors remain readable after module/manager/runtime destruction and native DSO close. `manager.stop()` still cannot interrupt blocking hooks; no hard or preemptive production deadline is added. Native transport details are platform-specific.

The parent reaped the owned server, actor and host children, independently confirmed the exact processes absent, and removed private data, credentials and TLS keys. There was no server restart/recovery, persistence or write-effect assertion.

Use the [standalone C++ termination guide](../../tests/database/server_termination.md) for private service/actor coordination and endpoint variables. The actor requires `lsof`, `ps` and `rg`. Build the opt-in program with the SSL-enabled database configuration, then run a mode only with its owned fault actor prepared as described in that guide:

```sh
cmake --build database-build --target ark_database_public_plugin_server_termination_tests --parallel 1
./database-build/tests/database/ark_database_public_plugin_server_termination_tests redis
./database-build/tests/database/ark_database_public_plugin_server_termination_tests mysql
```

The independent actor guards passed twenty invalid-path/marker cases and one intercepted valid-path control. This separate entry starts no server and inspects or signals no real process:

```sh
bash tests/database/owned_server_fault_actor_guards.sh
```

`check-database` compiles the opt-in program without executing its service modes or signaling servers. Compile-only CI success is not another real-service run.

## Real plugin Redis TLS

The three Redis TLS modes of `ark_database_public_plugin_service_tests` passed once each on **2026-10-26** at ark `6cf58973c0aad587b6eb88cede768a9283eb2147` and CI arknet pin `a0dfe09cd543e75cfb5996e537ce2fa49f2a758f`. The host was Mac16,11 / Apple M4 Pro / 14 cores / 48 GiB, macOS 27.0.1 (26A434) arm64, Apple Clang 21.0.0, C++20 Debug with Boost.Asio and OpenSSL 3.6.4. The isolated Redis 7.4.2 runtime was built separately from the verified official source archive with `BUILD_TLS=yes`, using that OpenSSL toolchain.

The private loopback instance required a password, disabled the plaintext port and persistence, and used a fresh CA/server certificate with only a `127.0.0.1` IP SAN. Client-certificate authentication was disabled; mutual TLS was not tested. OpenSSL controls confirmed the trusted chain/IP, the unrelated CA rejection and the `localhost` hostname mismatch before the DSO modes.

| Mode | Observed result |
| --- | --- |
| `redis-tls` | Authenticated binary ECHO command, then an ordered binary/empty ECHO and PING pipeline; owning command and pipeline replies survive later operations and host destruction. Independent server counters increased by three ECHOs and one PING. |
| `redis-tls-untrusted-ca` | Native `client_errc::connection_failed` with an OpenSSL certificate-verification transport error, `command_may_have_run=false` and no replies. PING/ECHO counters were unchanged across its observation window. |
| `redis-tls-wrong-host` | Same certificate-verification rejection using the trusted CA and `localhost` against the same IP-only certificate; PING/ECHO counters were unchanged across its observation window. |

Each C++ mode exited `0`. Configuration remains owned after the caller mutates its host/password/CA values. Every continuation and the once-only root completion use the owner thread/shared executor. Accepted work finishes under ordinary `manager.stop()/update()` natural drain, followed by native retirement and idempotent stop. Owning replies/errors remain readable after module/manager/runtime destruction and native DSO close. The controlled test bounds do not add a hard or preemptive production shutdown deadline.

Independent `INFO commandstats` observations cover only PING/ECHO in these isolated windows; neither the client error nor those counters proves that no RESP command reached a real server. The existing bounded protocol-peer TLS regression separately counts received RESP commands. The owned Redis was shut down through authenticated `SHUTDOWN NOSAVE` and reaped with exit `0`; its exact process was independently confirmed absent, and private data, credentials and keys were removed.

Use the [standalone C++ Redis TLS guide](../../tests/database/redis_tls.md) for the SSL-enabled build, private endpoints/CA/hostname controls and counter observations. The existing target is compiled by `check-database`; service execution remains opt-in:

```sh
./database-build/tests/database/ark_database_public_plugin_service_tests redis-tls
./database-build/tests/database/ark_database_public_plugin_service_tests redis-tls-untrusted-ca
./database-build/tests/database/ark_database_public_plugin_service_tests redis-tls-wrong-host
```

This record does not cover mutual TLS, certificate rotation/revocation or TLS cancellation, server faults and recovery. The earlier authenticated plain-TCP and other database records retain their original tested identities and scopes.

## Acceptance boundaries

The DSO workflows exercise normal plugin teardown and native close calls. Successful `FreeLibrary` or `dlclose` does not prove that the operating system physically removed the library image. Request cancellation, server termination and MySQL write/commit-visibility acceptance are limited to the recorded operations on this host. Crash durability, complete isolation-level coverage, transaction recovery, cancellation during acquire/BEGIN/prepare/statement close/commit/rollback and other prepared execute races, mutual TLS and certificate rotation/revocation, Redis TLS cancellation/fault recovery, other cancellation/fault races, OS image removal, other platforms/deployment environments and cross-machine capacity remain unverified. These records do not establish arbitrary hot-update safety or production capacity.

[Explicit restart recovery](../guides/database-restart.md) and [two server-default isolation levels](../guides/database-isolation.md) have separate local acceptance records. Their tested revisions and scopes do not extend the earlier scenarios above.
