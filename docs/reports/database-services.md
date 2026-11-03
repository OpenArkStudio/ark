# Database service acceptance records

[Acceptance index](../guides/database-validation.md)

Results apply to the recorded source, binaries and operations. The [resource lifecycle contract](../project/lifecycle-closeout.md) separates business completion from process cleanup; restart, isolation and other scopes retain their own revisions below.

<a id="repeated-public-plugin-workflows"></a>
<a id="shutdown-candidate-resource-cleanup"></a>

## Public database service workflows

On **2026-11-03**, all **17 modes** passed with one executed build identity. The three positive modes completed **1,351 cycles / 4,053 invocations in 300 seconds**; each of the other **14 modes ran once**. Every native invocation exited `0`.

| Build and environment | Recorded identity |
| --- | --- |
| Executed build | `3948d881`; executable and DSO fingerprints were unchanged before/after execution |
| Source equivalence | Database, core, configuration, dependency and build inputs match `10e385b4`; the complete source difference consists of HTTP startup code/tests and documentation |
| Machine / client | Mac16,11 · Apple M4 Pro · 14 logical CPUs · 48 GiB · macOS 27.0.1 (26A434), arm64 · Apple Clang 21 · C++20 Debug · Boost 1.92.0 · Boost.Asio with SSL · client OpenSSL 4.1.0 |
| Services | Fresh exclusive Redis 7.4.2 authenticated TCP, MySQL Community 8.4.13 arm64 with authenticated verified TLS, and Redis 7.4.2 TLS-only with server OpenSSL 3.5.9; IPv4 loopback only |
| Cleanup | Redis databases and MySQL schemas empty; owned servers stopped/reaped and owned ports closed; private data, keys and credential files removed |

<a id="real-plugin-dsos-against-real-services"></a>
<a id="mysql-dso-writes-and-two-connection-commit-visibility"></a>
<a id="reproduce-the-positive-dso-workflows"></a>

| Native entry point | Modes | Execution and result | Tested boundary |
| --- | --- | --- | --- |
| `ark_database_public_plugin_service_tests` | `redis`, `mysql`, `mysql-transactions` | 1,351 cycles, 4,053 invocations; all exit `0` | Actual hidden-symbol plugin DSOs; binary Redis commands/pipelines, MySQL queries/prepared inserts, autocommit, explicit commit/rollback and independent-connection visibility |
| `ark_database_public_failure_service_tests` | `redis`, `mysql` | Each once; 2/2 exit `0` | Real public-module factories/implementations linked into the host; wrong credentials, server-observed timeouts and explicit recovery; MySQL unrelated-CA rejection. These two modes do not use plugin DSOs |
| `ark_database_public_plugin_cancellation_tests` | `redis`, `mysql`, `mysql-transaction`, `mysql-statement` | Each once; 4/4 exit `0` | Actual DSOs; independent observation of the exact tagged active request precedes explicit task cancellation |
| `ark_database_public_plugin_service_tests` | `redis-tls`, `redis-tls-untrusted-ca`, `redis-tls-wrong-host` | Each once; 3/3 exit `0` | Actual Redis DSO; binary TLS operations and certificate-verification controls |
| `ark_database_public_plugin_cancellation_tests` | `redis-stop`, `mysql-stop` | Each once; 2/2 exit `0` | Actual DSOs; outer-owner `manager.stop()` cancels a pending pipeline/autocommit request |
| `ark_database_public_plugin_cancellation_tests` | `mysql-transaction-stop`, `mysql-statement-stop` | Each once; 2/2 exit `0` | Actual MySQL DSO; exact active transaction/Execute observation and uncommitted-write visibility precede stop |
| `ark_database_public_plugin_cancellation_tests` | `redis-tls-stop` | Once; exit `0` | Actual Redis DSO; verified TLS, unrelated-CA preflight and pending-pipeline manager stop |

DSO modes verify owner-thread/shared-executor delivery, once-only completion, owning results after host destruction and idempotent resource stop. The 300-second run is bounded correctness/resource-lifetime evidence, without throughput, long-term stability, persistence or production-capacity conclusions.

<Collapsible>
<CollapsibleTrigger>Source and executed binary fingerprints</CollapsibleTrigger>
<CollapsibleContent>

| Source | Revision |
| --- | --- |
| Executed build | `3948d881ccf2486231cb401b73888b8a0426f2bb` |
| Equivalent database/core/configuration/build inputs | `10e385b49dab960a075a0b317b6d2d4d8638959c` |
| arknet | `a0dfe09cd543e75cfb5996e537ce2fa49f2a758f` |
| Redis TLS server | `a0a6f23d997b024689ba157916837f493a593a34` |

| Executed binary | SHA256 |
| --- | --- |
| `ark_database_public_plugin_service_tests` | `262fff76884d51e987ff0ca80e96f5358436234c791078c2d07c7a9446567f10` |
| `ark_database_public_plugin_cancellation_tests` | `05a5baef32736b9d223c519c46015c5f1ec6f865be9880d101b938e3c01d4bb9` |
| `ark_database_public_failure_service_tests` | `62d4faa2c660d6acf5e8b3121d40294b69c3aa82ae398159916f688f209ce1da` |
| `ark_database_mysql_plugin_d.dylib` | `1c70d7a96dbebb9ff0b0dbee16ef966674f53788d13c1e7c6d6fec4a26ab6a4e` |
| `ark_database_redis_plugin_d.dylib` | `41031ee20933400f7949d2db1d606e4c5ad324808effaa4be95ec71dbec5e3de` |

</CollapsibleContent>
</Collapsible>

<a id="real-plugin-request-cancellation"></a>
<a id="mysql-sent-transaction-operation-cancellation"></a>
<a id="mysql-prepared-execute-cancellation"></a>
<a id="manager-stop-initiated-cancellation"></a>
<a id="manager-stop-transaction-cancellation"></a>
<a id="redis-tls-manager-stop-cancellation"></a>

## Cancellation outcomes

Explicit task cancellation follows independent server observation, uses repeated `task_group::cancel(id)`, rejects retired IDs and verifies explicit new-request recovery. Manager-stop modes instead call target `manager.stop()` on the outer owner frame with its root still pending. A separate observer manager/runtime/pool remains `running` after the target stops, confirms the whole original connection disappears and watches a further 500 ms without tagged replay. Target tasks/module retire, repeated stop succeeds and owning results survive both hosts. Observer queries do not recover the closed target pool.

| Cancellation modes | Observed operation and native outcome |
| --- | --- |
| `redis`, `redis-stop`, `redis-tls-stop` | Named pipeline in BLPOP after INCR `1`; native `cancelled`, `command_may_have_run=true` and valid partial replies. Target connection retires; final counter remains `1` |
| `mysql`, `mysql-stop` | Tagged autocommit UPDATE in `User sleep`; native `cancelled`, `outcome_unknown=true`, `operation_aborted`. Original session retires; final counter `1` |
| `mysql-transaction`, `mysql-statement` | Exact session in a tagged query or parameterized Execute after uncommitted pre-write; unknown/unconfirmed result, `discard_requested=true`, no statement-close/rollback reply. Original session disappears, fresh-connection recovery and bounded no-replay checks pass; final counter `0` |
| `mysql-transaction-stop`, `mysql-statement-stop` | Exact tagged query/Execute active, target own-read `1` and observer external-read `0` before stop. Native `cancelled`/`outcome_unknown=true`, `operation_aborted`, transaction `unconfirmed`/discard requested, no statement-close/rollback reply. Observer confirms session retirement, bounded no-replay and final external counter `0`; owning pre-write read remains `1` |

An external zero records the observed write effect, without an API rollback acknowledgement. Finite no-replay windows do not establish crash durability or global exactly-once execution. No transaction is replaced and no write replay or recovery through a stopped target is introduced. An observer DSO handle can keep the library image loaded until both hosts finish.

<a id="real-plugin-redis-tls"></a>

## Redis TLS observations

The private password-protected server exposed one PID-owned `127.0.0.1` TLS listener and plaintext `port=0`. Its certificate had only an IP SAN; OpenSSL controls verified the trusted chain/IP and `localhost` mismatch.

| Mode | Observed result |
| --- | --- |
| `redis-tls` | Binary ECHO and ordered binary/empty ECHO plus PING pipeline succeed; owning inputs/configuration/results survive later operations and host destruction |
| `redis-tls-untrusted-ca`, `redis-tls-wrong-host` | Native `connection_failed`, certificate-verification transport error, `command_may_have_run=false`, no replies; PING/ECHO counters unchanged during each control |
| `redis-tls-stop` | Unrelated-CA PING first fails with the same native verification diagnostics; trusted target/observer pools then verify pending-pipeline manager stop as recorded above |

Unchanged command counters do not prove that no RESP command reached the server; received-command counting is separate protocol-peer evidence. Mutual TLS, rotation/revocation, other TLS cancellation stages and service-fault recovery remain outside this scope.

Each single-invocation child retained the existing 45-second guard plus 5-second forced-exit grace; no bound was reached. These controls add no production shutdown deadline. Raw receipts/logs and service-runtime fingerprints remain local. Use the existing [positive DSO modes](../../tests/database/README.md#real-plugin-dsos-against-real-services), [failure controls](../../tests/database/README.md#public-real-service-failure-checks), [cancellation runner](../../tests/database/cancellation.md) and [Redis TLS controls](../../tests/database/redis_tls.md). Real-service execution is opt-in; `check-database` compiles these programs without running their service modes.


## Other recorded scopes

These scopes retain the revisions below and are separate from the 2026-11-03 executions, with arknet `a0dfe09cd543e75cfb5996e537ce2fa49f2a758f`.

<a id="public-modules-against-real-services"></a>

| Scope / source | Retained result and limit |
| --- | --- |
| Direct public-module positive workflows · 2026-11-02 · `feff48ac49ed11f5166533a9e5e74a02ce4567ef` | Redis/MySQL owning input/results, binary pipeline or query/prepared/transaction results, owner delivery and host-stack unwinding with pending work passed. Same Mac16,11/macOS 27.0.1, Clang 21, C++20 Debug, Boost 1.92.0/OpenSSL 4.1.0 and private Redis 7.4.2/MySQL 8.4.13 environment. These direct-module runs used natural drain and are not DSO or stop-cancellation evidence for the build above. [Native instructions](../../tests/database/README.md#public-modules-against-real-services) |
| Explicit same-pool restart · 2026-10-29 · ARK base `e77c2d8a1fe59d8519c447376c1f5d9a267935dd` | Interrupted read-only requests retain unknown outcomes; new requests recover after owned Redis/MySQL replacement at the same endpoint without pool reconfiguration. Runner blob `0b31502822ad0e181828b42a50859c2b093aa227`; no write replay, transaction recovery or high-availability claim. [Complete environment and record](../guides/database-restart.md) |
| MySQL isolation · 2026-10-29 · `093dfcb9a430beedcd86de19eb0d70c8aae9160e` | REPEATABLE READ retains its snapshot; READ COMMITTED sees committed changes on the next read. Opposite-level and invalid-configuration controls passed; no locking-read or SERIALIZABLE claim. [Complete environment and record](../guides/database-isolation.md) |

<Collapsible>
<CollapsibleTrigger>Earlier configuration and protocol controls</CollapsibleTrigger>
<CollapsibleContent>

The 2026-10-29 controls used macOS C++20 Debug, Boost 1.90.0 and OpenSSL 3.6.4; they are separate from the 2026-11-03 executions.

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
