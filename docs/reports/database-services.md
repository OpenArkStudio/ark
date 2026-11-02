# Database service acceptance records

[Acceptance index](../guides/database-validation.md)

Results apply only to the source, binary identities and operations recorded below. The [resource lifecycle contract](../project/lifecycle-closeout.md) separates business completion from process cleanup; older scopes retain their own revisions.

## Shutdown-candidate resource cleanup

On **2026-11-02**, **17 native invocations passed in four scopes**. The 12 positive/explicit-cancellation/TLS executions, 2 pipeline/autocommit stops, 2 transaction stops and 1 Redis TLS stop retain separate source and binary identities below. Each result is attributed to its recorded build.

### Positive workflows, explicit cancellation and TLS (12 invocations)

The **12 invocations** used fresh, exclusively owned single-node MySQL and Redis servers. Positive business workflows finish while `running`, then the host stops resources. Cancellation cases explicitly cancel an observed task on the owner thread, finish native/observer delivery, then stop the host; they do not establish real-service cancellation initiated by process stop while requests remain pending.

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

#### Positive DSO workflows and public failure controls

| Native entry point and modes | Result / observation |
| --- | --- |
| `ark_database_public_plugin_service_tests`: `redis`, `mysql`, `mysql-transactions` | 3 passed. Actual hidden-symbol plugin DSOs, owner-thread/shared-executor delivery, once-only completion, owning results after destruction and idempotent stop. MySQL covers binary prepared inserts, autocommit, explicit commit/rollback and independent-connection visibility |
| `ark_database_public_failure_service_tests`: `redis`, `mysql` | 2 passed through actual public module implementations: wrong credentials, server-observed request timeouts and explicit recovery; MySQL also rejects an unrelated CA |

<a id="real-plugin-request-cancellation"></a>
<a id="mysql-sent-transaction-operation-cancellation"></a>
<a id="mysql-prepared-execute-cancellation"></a>

#### Server-observed explicit task cancellation

All four modes of `ark_database_public_plugin_cancellation_tests` passed. An independent connection identifies the tagged operation as active before repeated `task_group::cancel(id)` requests; elapsed delay is not the activity barrier. Roots complete once on the owner thread/shared executor, retired IDs reject cancellation, explicit new requests recover, and retained results survive resource shutdown.

| Mode | Native outcome and finite server observation |
| --- | --- |
| `redis` | Named pipeline observed in BLPOP after INCR; `client_errc::cancelled`, `command_may_have_run=true`, partial integer reply `1`. The block retires and the counter remains `1` |
| `mysql` | Tagged UPDATE observed in `User sleep`; `connection_errc::cancelled`, `outcome_unknown=true`, `operation_aborted`. The operation disappears and final counter is `1` |
| `mysql-transaction` | Exact transaction session observed in the slow query after an uncommitted pre-write; unknown/unconfirmed result, discard requested, no rollback reply. Original session disappears, recovery uses a fresh connection, no replay appears in the bounded quiet window, final counter `0` |
| `mysql-statement` | Exact transaction session observed in parameterized Execute; same unknown/unconfirmed/discarded outcome, no statement-close or rollback reply. Original session disappears, fresh-connection recovery and bounded no-replay checks pass, final counter `0` |

An observed zero does not confirm rollback through the API. These finite observations establish neither durability nor global exactly-once execution. Cancellation, native completion and subsequent idempotent resource stop are covered; process-stop-initiated cancellation of a pending real-service request is not.

<a id="real-plugin-redis-tls"></a>

#### Redis TLS

All three TLS modes of `ark_database_public_plugin_service_tests` passed against a private password-protected, TLS-only server. Its fresh certificate had only a `127.0.0.1` IP SAN; OpenSSL controls confirmed the trusted chain/IP and `localhost` mismatch.

| Mode | Observed result |
| --- | --- |
| `redis-tls` | Binary ECHO and ordered binary/empty ECHO plus PING pipeline succeed; owned inputs/configuration and results survive later operations and host destruction |
| `redis-tls-untrusted-ca` | Native `client_errc::connection_failed`, certificate-verification transport error, `command_may_have_run=false`, no replies |
| `redis-tls-wrong-host` | Same verification rejection using the trusted CA and `localhost` against the IP-only certificate |

PING/ECHO counters did not increase during either negative control. That observation does not prove that no RESP command reached the server; received-command counting is separate protocol-peer evidence. Mutual TLS, certificate rotation/revocation and TLS cancellation/recovery were not exercised.

<Collapsible>
<CollapsibleTrigger>12-invocation build fingerprints</CollapsibleTrigger>
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

<a id="manager-stop-initiated-cancellation"></a>

### Manager-stop-initiated cancellation (2 invocations)

On the same machine with the client/arknet versions above, **`redis-stop` and `mysql-stop` both passed** using fresh authenticated Redis TCP and verified-TLS MySQL instances. These modes call target `manager.stop()` on the outer owner frame only after server observation confirms its request active and the target root is still pending. They do not explicitly call `task_group::cancel(id)`.

| Identity | Executed configuration |
| --- | --- |
| Tested source | Clean `39e6503683a3dcbca1169de30a488da35f7b51a7`, based on `b97f85fe3a4dd5aaaaa1423b7f8c47b4954f51d6` |
| Separate native build | New cancellation executable and actual hidden-symbol Redis/MySQL DSOs; database-only C++20 Debug, SSL enabled, applications/Protobuf disabled. The earlier 12-invocation binaries were unchanged; hashes below identify this two-mode build only |
| Native entry | `ark_database_public_plugin_cancellation_tests redis-stop` and `mysql-stop`; both exited `0` |

The observer owns a separate manager, runtime and pool and remains `running` after the target reaches `stopped`. It tracks the whole original server connection, then a further 500 ms without that connection or tagged request returning. Observer queries are not recovery through the closed target. Target roots complete once on their owner executor, pending task count reaches zero, the target module is removed and repeated stop succeeds. Owning native results remain valid after both hosts are destroyed.

| Mode | Native result and independent observation |
| --- | --- |
| `redis-stop` | Named pipeline observed in BLPOP after confirmed INCR `1`; manager stop preserves native `cancelled`, `command_may_have_run=true` and any valid partial reply. The target connection disappears and counter remains `1` through the bounded no-replay check |
| `mysql-stop` | Tagged autocommit UPDATE observed in `User sleep`; manager stop preserves native `cancelled`, `outcome_unknown=true` and `operation_aborted`. The original session disappears and final counter is `1`; no target-pool recovery or rollback reply is fabricated |

These counter observations do not establish a general cancellation write-effect or rollback guarantee. This build records pipeline/autocommit stop only; transaction/prepared manager-stop results use the separate build below. An observer DSO handle can keep the shared library image loaded until both hosts finish; resource closure does not establish physical image removal.

<Collapsible>
<CollapsibleTrigger>Two-mode manager-stop build fingerprints</CollapsibleTrigger>
<CollapsibleContent>

| Executed binary | SHA256 |
| --- | --- |
| `ark_database_public_plugin_cancellation_tests` | `2b698a5702600b9a38dd51a023b34c7ab0d1a7527d0c0e0c4c165e49e978647a` |
| `ark_database_mysql_plugin_d.dylib` | `effac22db654a276cd2ec3cd66baab908d5a4655dbfddf5b64382af5cb0abd41` |
| `ark_database_redis_plugin_d.dylib` | `96402add5a1a8a423e909f789176d72422b6b5617559e8d903c78f282d0ceafe` |

</CollapsibleContent>
</Collapsible>

<a id="manager-stop-transaction-cancellation"></a>

### Manager-stop transaction cancellation (2 invocations)

**`mysql-transaction-stop` and `mysql-statement-stop` both passed** on the same host/client/arknet versions above, using a fresh authenticated, verified-TLS MySQL 8.4.13 instance and private InnoDB tables. A separate observer manager/runtime/pool confirms the exact target session's tagged query in `User sleep`, requires `COMMAND='Execute'` for the prepared mode, and reads external counter `0` while the target retains its own uncommitted read `1`. Only then does the outer owner frame call target `manager.stop()` with its root still pending.

| Identity | Executed configuration |
| --- | --- |
| Tested source | Clean `95515ad62ef80505b4c050e795b2690dc2434fdf`, based on `91ed5e3d479c69e8d37cc6dd53685a68a8063920` |
| Separate native build | New cancellation executable and actual hidden-symbol MySQL DSO; database-only C++20 Debug, SSL enabled, applications/Protobuf disabled, parallelism `2`. No earlier service executable or DSO was rebuilt |
| Native entry / result | `ark_database_public_plugin_cancellation_tests mysql-transaction-stop` and `mysql-statement-stop`; each ran once and exited `0` |

Both target roots complete exactly once on the owner executor. They preserve native `cancelled`, `outcome_unknown=true`, `operation_aborted`, transaction `unconfirmed` and `discard_requested=true`, without statement-close/rollback replies or cleanup/cancellation/lease-return exceptions. Target tasks/module retire and repeated stop succeeds. The observer stays `running` after target stop, waits for the whole original session to disappear and a further 500 ms without that session or tagged replay, then reads final external counter `0` and removes the table. After both hosts are destroyed, the owning pre-write read still contains `1` and the cancelled result retains those uncertain diagnostics.

The external zero records this observed discard effect, not a rollback acknowledgement, crash durability or global exactly-once guarantee. No request recovers through the closed target, no transaction is replaced and no write replay is introduced. These modes cover stop during one sent transaction query or prepared execute; other cancellation stages/races remain separate.

<Collapsible>
<CollapsibleTrigger>Transaction manager-stop build fingerprints</CollapsibleTrigger>
<CollapsibleContent>

| Executed binary | SHA256 |
| --- | --- |
| `ark_database_public_plugin_cancellation_tests` | `4c97f9e76c883b3fddee00a091256289bc2d89ad02ddd78d5722ba2fc4f1f675` |
| `ark_database_mysql_plugin_d.dylib` | `8d19d2700b218e68e9df2df3dae9d007c2266d37c8f470a6903e5b080e62cf62` |

</CollapsibleContent>
</Collapsible>

<a id="redis-tls-manager-stop-cancellation"></a>

### Redis TLS manager-stop cancellation (1 invocation)

**`redis-tls-stop` ran once and passed** against a fresh owned Redis 7.4.2 TLS instance with password authentication and private CA material. Independent setup verified the certificate chain/IP, live plaintext `port=0`, the selected `tls-port` and exactly one PID-owned TLS listener. This is TLS evidence, not an inference from the TCP modes.

| Identity | Executed configuration |
| --- | --- |
| Tested source | Clean `779f26fb6f91b18fff1d1c54c05d403eee7eb70b`, based on `a7f3a98fdc275379dbaf433addb14dbad6191089` |
| Separate native build | New cancellation executable and actual hidden-symbol Redis DSO; database-only C++20 Debug, SSL enabled, applications/Protobuf disabled, parallelism `2`. Earlier 16-invocation artifacts were unchanged |
| Service / native entry | Redis TLS source `a0a6f23d997b024689ba157916837f493a593a34`, server OpenSSL 3.5.9, client OpenSSL 4.1.0; `ark_database_public_plugin_cancellation_tests redis-tls-stop` exited `0` |

The same invocation first requires an unrelated-CA PING to fail specifically with native `connection_failed`, SSL certificate-verification transport error, `command_may_have_run=false` and no replies. Trusted target and independent observer managers then use verified TLS pools. The server must show the named pipeline blocked in BLPOP after counter `1` before outer-owner target `manager.stop()` with the root pending. The observer survives target stop, confirms whole-session retirement and another 500 ms without replay, reads counter `1` and deletes the keys.

Exactly one owner-executor completion, target task/module retirement and idempotent stop passed. After both hosts are destroyed, owning native `cancelled`/`command_may_have_run=true`, valid partial replies and the CA-verification diagnostic remain valid. No target-pool recovery or write replay is introduced. This scope does not add mutual TLS, hostname-mismatch controls, certificate rotation/revocation or TLS service-fault recovery.

<Collapsible>
<CollapsibleTrigger>Redis TLS manager-stop build fingerprints</CollapsibleTrigger>
<CollapsibleContent>

| Executed binary | SHA256 |
| --- | --- |
| `ark_database_public_plugin_cancellation_tests` | `de82abcf263f1d4238b47f42f40f38a2e79d020438fc4954175b34ac1405106d` |
| `ark_database_redis_plugin_d.dylib` | `e3e50a3eebf2f55148ad98eba0baebbad0f7dfff0cc3d3e9a26b737a5af506b7` |

</CollapsibleContent>
</Collapsible>

### Execution controls and cleanup

Each native child had a 45-second observation bound and a further 5-second owned-child termination bound; no bound was reached. These are test controls, not production shutdown defaults. Services, ports, data, credentials and certificates were private to their respective batches. The schema and Redis databases were empty afterward; owned processes were stopped/reaped, and private data directories, keys and credential configurations were removed. Raw receipts, logs and server-binary fingerprints remain local.

Use the existing [positive DSO modes](../../tests/database/README.md#real-plugin-dsos-against-real-services), [failure controls](../../tests/database/README.md#public-real-service-failure-checks), [cancellation runner](../../tests/database/cancellation.md), [Redis TLS controls](../../tests/database/redis_tls.md), [manager-stop modes](../../tests/database/cancellation.md#manager-stop-cancellation), [transaction stop modes](../../tests/database/cancellation.md#manager-stop-transaction-cancellation) and [TLS stop mode](../../tests/database/cancellation.md#redis-tls-manager-stop-cancellation). Real-service execution remains opt-in; `check-database` compilation does not reproduce it.

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

DSO workflows exercise normal plugin destruction and native close calls; successful `dlclose`/`FreeLibrary` does not prove physical OS image removal. Production capacity, arbitrary callback lifetimes and hot-update safety are unverified. Stop-initiated cancellation is limited to the five observed modes. Cancellation during acquire/BEGIN/prepare/statement close/commit/rollback, real-service queued-acquire contention and other races remain excluded, alongside crash durability, complete isolation coverage, transaction recovery, mutual TLS, certificate rotation/revocation and Redis TLS fault recovery. AddressSanitizer, UndefinedBehaviorSanitizer, ThreadSanitizer, other platforms and hosted CI are separate scopes.
