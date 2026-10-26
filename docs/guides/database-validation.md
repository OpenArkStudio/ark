# Database service acceptance

This record separates public-module service/failure checks, positive plugin-DSO workflows, server-observed request cancellation and owned-server termination. Each scope has its own tested revisions and environment; a passing result applies only to that scope.

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

The DSO workflows exercise normal plugin teardown and native close calls. Successful `FreeLibrary` or `dlclose` does not prove that the operating system physically removed the library image. Request cancellation and server-termination acceptance are limited to the recorded operations on this host. Restart/recovery, persistence and write effects, transaction cancellation, Redis TLS, other cancellation/fault races, OS image removal, other platforms/deployment environments and cross-machine capacity remain unverified. These records do not establish arbitrary hot-update safety or production capacity.
