# Database service acceptance

Use this index to choose an existing database acceptance workflow and find its
recorded result. The [detailed service records](../reports/database-services.md)
retain each scenario's tested revisions, date, environment, observations and limits.
Restart and isolation have separate records linked below.

## Recorded results

These are local functional results. Each result applies to its recorded source
and scenario; the table does not combine them into a broader acceptance claim.

| Capability | Recorded result | Run tool / instructions | Detailed record |
| --- | --- | --- | --- |
| <a id="public-modules-against-real-services"></a>Public modules: Redis and MySQL | Normal and failure modes passed: owning results, owner delivery, authentication failures, observed timeouts and explicit-request recovery | [Public service and failure runners](../../tests/database/README.md#public-modules-against-real-services) | [Service records](../reports/database-services.md#public-modules-against-real-services) |
| <a id="real-plugin-dsos-against-real-services"></a>Real plugin DSOs: positive workflows | Both modes passed with owning results, natural drain and idempotent stop | `ark_database_public_plugin_service_tests` (`redis`, `mysql`) | [Positive DSO records](../reports/database-services.md#real-plugin-dsos-against-real-services) |
| <a id="mysql-dso-writes-and-two-connection-commit-visibility"></a>MySQL DSO writes | Binary inserts, own-write reads, independent commit/rollback visibility and retained results passed | `ark_database_public_plugin_service_tests mysql-transactions` | [Write record](../reports/database-services.md#mysql-dso-writes-and-two-connection-commit-visibility) |
| <a id="real-plugin-request-cancellation"></a>Server-observed request cancellation | Both modes passed; native cancellation outcomes and finite counter observations are retained | [C++ cancellation runner](../../tests/database/cancellation.md) | [Cancellation records](../reports/database-services.md#real-plugin-request-cancellation) |
| <a id="real-plugin-server-termination"></a>Owned-server termination | Both modes passed with sent/unknown native errors after a PID-specific fault; actor guards also passed | [C++ runner and shell actor](../../tests/database/server_termination.md) | [Termination records](../reports/database-services.md#real-plugin-server-termination) |
| <a id="real-plugin-redis-tls"></a>Redis TLS | Authenticated binary operations passed; unrelated CA and wrong-host controls rejected the connection | [Redis TLS modes](../../tests/database/redis_tls.md) | [TLS record](../reports/database-services.md#real-plugin-redis-tls) |
| Explicit same-pool restart recovery | Redis and MySQL passed new read-only requests after an owned service restarted at the same endpoint | [C++ restart runner and actor](../../tests/database/restart.md) | [Restart record](database-restart.md) |
| MySQL consistent-read isolation | REPEATABLE READ retained its transaction snapshot; READ COMMITTED saw committed changes on its next read | [C++ isolation runner](../../tests/database/isolation.md) | [Isolation record](database-isolation.md) |

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
Restart and isolation results do not extend the earlier service scenarios.

Natural drain and native DSO close do not prove physical OS library-image removal
or arbitrary hot-update safety. `manager.stop()` is cooperative and cannot
interrupt blocking hooks; test bounds add no hard production shutdown deadline.

Crash durability, persistence, complete isolation-level coverage, transaction
cancellation, mutual TLS, certificate rotation/revocation, Redis TLS cancellation
and fault recovery, and other cancellation/fault races remain outside these
records. Request cancellation does not establish rollback or global exactly-once
execution. The detailed records preserve each observed outcome and narrower limit.
