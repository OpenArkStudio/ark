# Database service acceptance

Use this index to choose an existing workflow and find its [detailed service record](../reports/database-services.md). The **2026-11-03** results share executed build `3948d881`, with equivalent database/core/configuration/build inputs at `10e385b4`. Direct-module positive, server-termination, restart and isolation results retain their separately recorded revisions. See the [resource lifecycle contract](../project/lifecycle-closeout.md).

## Recorded results

These are local functional results, scoped to each recorded build and operation.

<a id="mysql-dso-writes-and-two-connection-commit-visibility"></a>
<a id="mysql-sent-transaction-operation-cancellation"></a>
<a id="mysql-prepared-execute-cancellation"></a>
<a id="manager-stop-initiated-cancellation"></a>
<a id="manager-stop-transaction-cancellation"></a>
<a id="redis-tls-manager-stop-cancellation"></a>

| Capability | Recorded result | Run tool / instructions | Detailed record |
| --- | --- | --- | --- |
| <a id="real-plugin-dsos-against-real-services"></a>Public-plugin positive workflows | 3 modes × 1,351 cycles: 4,053 exit-`0` invocations in 300 seconds; Redis binary commands/pipelines and MySQL query/prepared/commit/rollback visibility | `ark_database_public_plugin_service_tests`: `redis`, `mysql`, `mysql-transactions` | [Build, fingerprints and results](../reports/database-services.md#public-database-service-workflows) |
| <a id="public-modules-against-real-services"></a>Public-module failure controls | 2 modes, each once, exit `0`; real module factories, authentication rejection, observed timeouts and explicit recovery | [Failure runner](../../tests/database/README.md#public-real-service-failure-checks) | [Module and DSO boundaries](../reports/database-services.md#public-database-service-workflows) |
| <a id="real-plugin-request-cancellation"></a>Server-observed explicit request cancellation | 4 DSO modes, each once, exit `0`; Redis/MySQL and transaction/prepared execution preserve native uncertain outcomes and finite server observations | [C++ cancellation runner](../../tests/database/cancellation.md) | [Cancellation outcomes](../reports/database-services.md#real-plugin-request-cancellation) |
| Manager-stop cancellation | 5 DSO modes, each once, exit `0`: Redis TCP/TLS, MySQL autocommit and transaction/prepared execution; independent observer verifies session retirement and counter effects | [C++ manager-stop modes](../../tests/database/cancellation.md#manager-stop-cancellation) | [Pipeline/autocommit](../reports/database-services.md#manager-stop-initiated-cancellation), [transaction/prepared](../reports/database-services.md#manager-stop-transaction-cancellation), [Redis TLS](../reports/database-services.md#redis-tls-manager-stop-cancellation) |
| <a id="real-plugin-redis-tls"></a>Redis TLS controls | 3 DSO modes, each once, exit `0`; authenticated binary operations succeed; unrelated CA and wrong hostname yield native certificate rejection | [Redis TLS modes](../../tests/database/redis_tls.md) | [TLS observations](../reports/database-services.md#real-plugin-redis-tls) |
| Direct public-module positive workflows | 2026-11-02 · `feff48ac`; owning inputs/results and natural drain retain this earlier source identity | [Public service runner](../../tests/database/README.md#public-modules-against-real-services) | [Separate source and scope](../reports/database-services.md#public-modules-against-real-services) |
| <a id="real-plugin-server-termination"></a>Owned-server termination | 2026-10-26 · `0722b8bb`; 2 modes passed with sent/unknown native errors after PID-specific faults; actor guards passed | [C++ runner and shell actor](../../tests/database/server_termination.md) | [Termination record](../reports/database-services.md#real-plugin-server-termination) |
| Explicit same-pool restart recovery | 2026-10-29 · base `e77c2d8a`; Redis/MySQL new read-only requests recover after an owned service restarts at the same endpoint | [C++ restart runner and actor](../../tests/database/restart.md) | [Restart record](database-restart.md) |
| MySQL consistent-read isolation | 2026-10-29 · `093dfcb9`; REPEATABLE READ retains its snapshot; READ COMMITTED sees committed changes on the next read | [C++ isolation runner](../../tests/database/isolation.md) | [Isolation record](database-isolation.md) |


## Run a workflow

<a id="reproduce-the-positive-dso-workflows"></a>

Use the [database build configuration](../../tests/database/README.md#build) and
the instructions for the selected row. Real-service modes are standalone C++
programs; MySQL requires the SSL-enabled build. The fault actor uses shell tools.

Prepare exclusively owned services, private data and credentials, and the required
TLS trust and privileges before running a mode. Missing configuration is failure,
not a successful skip. Follow that workflow's cleanup instructions afterward.

`check-database` compiles the opt-in service programs without executing their
real-service modes. Compilation and local fixture checks do not reproduce these
service results. Machine and source identities belong to the detailed records,
not to every build of the current repository.

## Acceptance boundaries

The records describe tested operations on one host. They do not establish hosted
CI acceptance, another platform or deployment environment, or production capacity.
Restart and isolation remain limited to their own sources and operations.

Natural drain and native DSO close do not prove physical OS library-image removal
or arbitrary hot-update safety. `manager.stop()` is cooperative and cannot
interrupt blocking hooks; test bounds add no hard production shutdown deadline.

Crash durability, persistence, complete isolation-level coverage, transaction
recovery, cancellation during acquire/BEGIN/prepare/statement close/commit/rollback and other execute races,
mutual TLS, certificate rotation/revocation, other Redis TLS cancellation stages and fault
recovery, and other cancellation/fault races remain outside these records. Request
cancellation does not establish rollback or global exactly-once execution. The sent transaction-query result remains unconfirmed despite the
observed final zero. The detailed records preserve each outcome and narrower limit.
