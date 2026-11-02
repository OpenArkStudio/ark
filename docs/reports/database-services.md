# Database service acceptance records

[Acceptance index](../guides/database-validation.md)

Results apply only to the source, binary identities and operations recorded below. The [resource lifecycle contract](../project/lifecycle-closeout.md) separates business completion from process cleanup; older scopes retain their own revisions.

## Shutdown-candidate resource cleanup

On **2026-11-02**, **12 native invocations passed** against fresh, exclusively owned single-node MySQL and Redis servers. Positive business workflows finish while `running`, then the host stops resources. Cancellation cases explicitly cancel an observed task on the owner thread, finish native/observer delivery, then stop the host; they do not establish real-service cancellation initiated by process stop while requests remain pending.

| Identity | Executed configuration |
| --- | --- |
| Candidate source | Clean `bc90b68199c7a8d5ccb23e70785454870e160f3e` |
| Reused database build | `7767bbe0274a1b3e013f114546f567c079d03c00`. `tests/database`, `src/core`, `src/plugins/mysql` and `src/plugins/redis` are unchanged through the candidate; this is source equivalence, not a build at the candidate commit. Binary SHA256 values were identical before/after execution |
| Machine / system | Mac16,11 · Apple M4 Pro · 14 cores · 48 GB · macOS 27.0.1 (26A434) · arm64 |
| Client | Apple Clang 21.0.0 · C++20 Debug · frozen Boost 1.92.0 · OpenSSL 4.1.0 · Boost.Asio with SSL enabled |
| arknet | Configured source `a0dfe09cd543e75cfb5996e537ce2fa49f2a758f` |
| Servers | MySQL Community 8.4.13 arm64 with authenticated, verified TLS; Redis 7.4.2 with authenticated TCP; Redis TLS source `a0a6f23d997b024689ba157916837f493a593a34`, server OpenSSL 3.5.9 |

<a id="real-plugin-dsos-against-real-services"></a>
<a id="mysql-dso-writes-and-two-connection-commit-visibility"></a>
<a id="reproduce-the-positive-dso-workflows"></a>

### Positive DSO workflows and public failure controls

| Native entry point and modes | Result / observation |
| --- | --- |
| `ark_database_public_plugin_service_tests`: `redis`, `mysql`, `mysql-transactions` | 3 passed. Actual hidden-symbol plugin DSOs, owner-thread/shared-executor delivery, once-only completion, owning results after destruction and idempotent stop. MySQL covers binary prepared inserts, autocommit, explicit commit/rollback and independent-connection visibility |
| `ark_database_public_failure_service_tests`: `redis`, `mysql` | 2 passed through actual public module implementations: wrong credentials, server-observed request timeouts and explicit recovery; MySQL also rejects an unrelated CA |

<a id="real-plugin-request-cancellation"></a>
<a id="mysql-sent-transaction-operation-cancellation"></a>
<a id="mysql-prepared-execute-cancellation"></a>

### Server-observed explicit task cancellation

All four modes of `ark_database_public_plugin_cancellation_tests` passed. An independent connection identifies the tagged operation as active before repeated `task_group::cancel(id)` requests; elapsed delay is not the activity barrier. Roots complete once on the owner thread/shared executor, retired IDs reject cancellation, explicit new requests recover, and retained results survive resource shutdown.

| Mode | Native outcome and finite server observation |
| --- | --- |
| `redis` | Named pipeline observed in BLPOP after INCR; `client_errc::cancelled`, `command_may_have_run=true`, partial integer reply `1`. The block retires and the counter remains `1` |
| `mysql` | Tagged UPDATE observed in `User sleep`; `connection_errc::cancelled`, `outcome_unknown=true`, `operation_aborted`. The operation disappears and final counter is `1` |
| `mysql-transaction` | Exact transaction session observed in the slow query after an uncommitted pre-write; unknown/unconfirmed result, discard requested, no rollback reply. Original session disappears, recovery uses a fresh connection, no replay appears in the bounded quiet window, final counter `0` |
| `mysql-statement` | Exact transaction session observed in parameterized Execute; same unknown/unconfirmed/discarded outcome, no statement-close or rollback reply. Original session disappears, fresh-connection recovery and bounded no-replay checks pass, final counter `0` |

An observed zero does not confirm rollback through the API. These finite observations establish neither durability nor global exactly-once execution. Cancellation, native completion and subsequent idempotent resource stop are covered; process-stop-initiated cancellation of a pending real-service request is not.

<a id="real-plugin-redis-tls"></a>

### Redis TLS

All three TLS modes of `ark_database_public_plugin_service_tests` passed against a private password-protected, TLS-only server. Its fresh certificate had only a `127.0.0.1` IP SAN; OpenSSL controls confirmed the trusted chain/IP and `localhost` mismatch.

| Mode | Observed result |
| --- | --- |
| `redis-tls` | Binary ECHO and ordered binary/empty ECHO plus PING pipeline succeed; owned inputs/configuration and results survive later operations and host destruction |
| `redis-tls-untrusted-ca` | Native `client_errc::connection_failed`, certificate-verification transport error, `command_may_have_run=false`, no replies |
| `redis-tls-wrong-host` | Same verification rejection using the trusted CA and `localhost` against the IP-only certificate |

PING/ECHO counters did not increase during either negative control. That observation does not prove that no RESP command reached the server; received-command counting is separate protocol-peer evidence. Mutual TLS, certificate rotation/revocation and TLS cancellation/recovery were not exercised.

<Collapsible>
<CollapsibleTrigger>Executed binary fingerprints</CollapsibleTrigger>
<CollapsibleContent>

| Executed binary | SHA256 |
| --- | --- |
| `ark_database_public_plugin_service_tests` | `e763e25372866b45461185e51bddf6734b80022e8724335ee3f06b46ec89b531` |
| `ark_database_public_plugin_cancellation_tests` | `4effcf1c105bb5e66f124281849f2352dfcd96fd746fcac5e141cdf1e610d2da` |
| `ark_database_public_failure_service_tests` | `eb39463bc477424ca19487a8fd572c8e4909bfa8ea5503f7445973051e3a65ad` |
| `ark_database_mysql_plugin_d.dylib` | `48e8adc1f1b2e83e31ef496728b6f312f26c3fc5910e0faf368cbcabb4177293` |
| `ark_database_redis_plugin_d.dylib` | `e6bf0a2b9476d79d5b846643dfb59511418f3e6086866277a5990caa2938c13a` |

</CollapsibleContent>
</Collapsible>

Each native child had a 45-second observation bound and a further 5-second owned-child termination bound; no bound was reached. These are test controls, not production shutdown defaults. Services, ports, data, credentials and certificates were private to the batch. The schema and Redis databases were empty afterward; owned processes were stopped/reaped, and private data directories, keys and credential configurations were removed. Raw receipts, logs and server-binary fingerprints remain local.

Use the existing [positive DSO modes](../../tests/database/README.md#real-plugin-dsos-against-real-services), [failure controls](../../tests/database/README.md#public-real-service-failure-checks), [cancellation runner](../../tests/database/cancellation.md) and [Redis TLS controls](../../tests/database/redis_tls.md). Real-service execution remains opt-in; `check-database` compilation does not reproduce it.

## Other recorded scopes

These operations were not executed against the shutdown candidate. Their results remain scoped to the revisions below, with arknet `a0dfe09cd543e75cfb5996e537ce2fa49f2a758f`.

<a id="public-modules-against-real-services"></a>

| Scope / source | Retained result and limit |
| --- | --- |
| Direct public-module positive workflows · 2026-11-02 · `feff48ac49ed11f5166533a9e5e74a02ce4567ef` | Redis/MySQL owning input/results, binary pipeline or query/prepared/transaction results, owner delivery and host-stack unwinding with pending work passed. Same Mac16,11/macOS 27.0.1, Clang 21, C++20 Debug, Boost 1.92.0/OpenSSL 4.1.0 and private Redis 7.4.2/MySQL 8.4.13 environment. These direct-module runs used natural drain and are not candidate DSO or stop-cancellation evidence. [Native instructions](../../tests/database/README.md#public-modules-against-real-services) |
| Explicit same-pool restart · 2026-10-29 · ARK base `e77c2d8a1fe59d8519c447376c1f5d9a267935dd` | Interrupted read-only requests retain unknown outcomes; new requests recover after owned Redis/MySQL replacement at the same endpoint without pool reconfiguration. Runner blob `0b31502822ad0e181828b42a50859c2b093aa227`; no write replay, transaction recovery or high-availability claim. [Complete environment and record](../guides/database-restart.md) |
| MySQL isolation · 2026-10-29 · `093dfcb9a430beedcd86de19eb0d70c8aae9160e` | REPEATABLE READ retains its snapshot; READ COMMITTED sees committed changes on the next read. Opposite-level and invalid-configuration controls passed; no locking-read or SERIALIZABLE claim. [Complete environment and record](../guides/database-isolation.md) |

<Collapsible>
<CollapsibleTrigger>Earlier configuration and protocol controls</CollapsibleTrigger>
<CollapsibleContent>

The 2026-10-29 controls used macOS C++20 Debug, Boost 1.90.0 and OpenSSL 3.6.4; they are not candidate executions.

| Source | Recorded control |
| --- | --- |
| `4452acaeed6a3834d048f025f2970825803402bb` | MySQL write-mode missing-host and non-loopback-host configurations rejected with exit `1` before connection |
| `28edd1a88c5e7179668aa387a9158d3db32a15f0` | Transaction cancellation general log counted one pre-write and one tagged slow UPDATE; runner unchanged at `5ce07533495a2b2cbb6fc31f57131dab3952c15a`. SSL-disabled rejection controls belong to that record |
| `30f963cade5162d8b3b474646dc8c123b987a5be` | Prepared cancellation general log counted one Prepare, one Execute and one pre-write; seven configuration/build-mode rejections belong to that record |
| `7fde1fbe7f6607c275987ba75bcccf8630853f21` · 2026-11-02 · Boost 1.92.0/OpenSSL 4.1.0 | `ark_database_queue_tests`: 18 cases / 131 assertions; filtered queued-acquire protocol-peer case: 1 case / 146 assertions, 4 cases excluded. This is not real-service queued-acquire contention or broader cancellation-stage acceptance |

</CollapsibleContent>
</Collapsible>

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

## Acceptance boundaries

DSO workflows exercise normal plugin destruction and native close calls; successful `dlclose`/`FreeLibrary` does not prove physical OS image removal. Production capacity, arbitrary callback lifetimes and hot-update safety are unverified. The current record excludes process-stop-initiated cancellation of pending real-service requests, real-service queued-acquire contention, other cancellation stages/races, crash durability, complete isolation coverage, transaction recovery, mutual TLS, certificate rotation/revocation and Redis TLS fault recovery. AddressSanitizer, UndefinedBehaviorSanitizer, ThreadSanitizer, other platforms and hosted CI are separate scopes.
