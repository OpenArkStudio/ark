# Database integration tests

[简体中文](README_CN.md)

These development targets cover native services, public coroutine modules and
actual plugin registration/shutdown. `ARK_BUILD_DATABASE_TESTS` is `OFF` by
default; enabling it does not enable the application runtime plugin options.
See the public [MySQL](../../docs/reference/mysql.md) and
[Redis](../../docs/reference/redis.md) APIs for application usage.

The public module fixtures construct an owning interface through the plugin-private
factory; the concrete classes live only in their implementation `.cpp` files.
These fixtures retain direct lifecycle and protocol-peer coverage. The separate
`ark_database_public_plugin_service_tests` consumes only public API targets and
loads the actual hidden-symbol DSOs; it does not link those factories or private
implementation files. Public headers remain available through `ark::mysql` and
`ark::redis` with runtime plugins disabled; see the [consumer check](../consumer/README.md).

## Build

Initialize the existing dependencies described in [network builds](../../docs/networking.md).
Use an explicit local arknet checkout containing the native Redis/MySQL clients
and `arknet/base/manual_runtime.hpp`;
these tests never change the production dependency pin or fetch a feature branch.

```sh
cmake -S . -B database-build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  "-DVCPKG_MANIFEST_FEATURES=boost;database" \
  -DARK_BUILD_APPLICATIONS=OFF -DARK_BUILD_PROTO=OFF \
  -DARK_BUILD_TESTS=OFF -DARK_BUILD_CORE_TESTS=OFF \
  -DARK_BUILD_DATABASE_TESTS=ON -DARKNET_SOURCE_DIR=/absolute/path/to/arknet \
  -DASIO_STANDALONE=OFF -DARKNET_ENABLE_SSL=ON
cmake --build database-build --target ark_database_queue_tests \
  ark_database_task_tests ark_database_task_contract_tests \
  ark_database_redis_stop_tests ark_database_redis_tls_tests \
  ark_database_redis_tests ark_database_mysql_tests \
  ark_database_service_awaitable_tests --parallel 2
```

For Standalone Asio, set `ASIO_STANDALONE=ON`,
`VCPKG_MANIFEST_NO_DEFAULT_FEATURES=ON` and `VCPKG_MANIFEST_FEATURES=database`;
provide `ARKNET_ASIO_INCLUDE_DIR` when needed. MySQL is built only when
`ARKNET_ENABLE_SSL=ON`: OpenSSL is required for authentication even with an
explicit unencrypted MySQL connection. Redis's plain connection tests can be built
without this option. No new test framework is downloaded: queue tests use the
existing `tests/network/doctest.h`, as core/network tests do; real-service checks
are standalone C++ executables.

## Run local regressions

```sh
cmake --build database-build --target check-database
# Queue, stop and coroutine regressions; programs can also run directly.
bash scripts/test_databases.sh database-build local
./database-build/tests/database/ark_database_queue_tests --list-test-cases
```

The queue cases cover count/retained-input admission, completed-but-undispatched
operations, concurrent producers, owner-thread dispatch, callback exceptions,
reentrancy and stop-capture release. The Redis stop regression verifies that a
capture destructor can destroy its service during `stop()`; it requires no
running database and uses only IPv4 loopback port 1.
The state regression also verifies failed-acquire error/reply mapping using explicit
input states; it does not reproduce or claim a native connection-pool fault.

The real-service targets are not run by `check-database`. Missing endpoint
configuration is an error, not a silently skipped successful check.

## Private service retirement

`ark_database_service_retirement_tests` runs without a database server. It covers
the owning MySQL, plain Redis and TLS Redis services when SSL is enabled; without
SSL it covers plain Redis. A rejected native acquire runs on the real pool actor,
and explicit retirement verifies its callback/captures and owned worker have
finished. Empty-host validation exercises two accepted coroutine requests without
a network connection. These cases do not validate a database protocol handshake,
active child connections or deployment capacity.

For DRAIN, first stop top-level task admission and keep services and bridges open
for later requests in accepted workflows. Continue pumping results. Only after
all owning workflows, bridges and transaction participants have drained and the
outer coroutine/update execution stacks have returned, call the service's
`stop_accepting()`. `logically_drained()` then includes sealed admission, its
shared activity guards and completion-queue quiescence. It cannot inspect an
external runtime or replace the caller's outer execution gate.

`retire_native()` runs the owned pool's permanent `close_and_wait()` barrier and
sets `native_retired()` only on success. False means retirement is incomplete;
native `last_error` is preserved. Exceptions propagate without dropping the pool
owner, including the native pool's sticky finalization failure. No automatic
retry, cancellation or bounded shutdown latency is promised. Existing `stop()`
still suppresses callbacks and requests asynchronous close. Transaction services
borrow their pools and do not receive this owning-service barrier. These private
methods implement the public plugins’ host-driven retirement phase after the
manager drains tasks and observes module readiness.

With SSL enabled, local checks also include the private transaction state tests.
The separate [MySQL transaction test](TRANSACTIONS.md) requires a disposable real
service and exercises one native lease across multiple statements and completion.

## Redis TLS regression

With `ARKNET_ENABLE_SSL=ON`, `check-database` also runs the native TLS regression.
It can be built and run separately:

```sh
cmake --build database-build --target check-database-redis-tls
# Or: ./database-build/tests/database/ark_database_redis_tls_tests
```

The private `redis_service` alias keeps its plain connection API. The private
`tls_redis_service` uses the same request, cancellation and owner-thread delivery
implementation with arknet's TLS pool. It verifies the certificate chain and the
configured connection host; an unrelated CA or wrong hostname must fail before
any RESP command, including `HELLO`, reaches the test endpoint. A successful
binary pipeline retains owning results after service destruction. A sent-request
cancellation returns its terminal result through `update()` and the module
coroutine executor, preserving `command_may_have_run`.

The executable binds a random IPv4 loopback port and needs no running Redis
service. Its bounded RESP-over-TLS protocol test endpoint checks only these
contracts; it is not a real Redis compatibility or capacity test. It reads the
public test-only certificates and key directly from
`ARKNET_SOURCE_DIR/tests/certs` (arknet, BSL-1.0; see that checkout's `LICENSE` and
certificate README). The fixtures remain in that dependency checkout and are
never copied into the application. They must never be used in deployment.
`ARKNET_ENABLE_SSL=OFF` omits this target and leaves the plain service available.

## Redis service check

Provide an isolated Redis-compatible service. The check creates a unique
`ark-private-service-...` key, verifies binary SET/GET and deletes that key. It also
checks owning results, module-thread dispatch, admission recovery, stop suppression
and service destruction from a business callback.

For example, start a disposable local service in another terminal:

```sh
redis-server --bind 127.0.0.1 --port 26791 --protected-mode yes \
  --save '' --appendonly no
```

Then run:

```sh
export ARK_DATABASE_REDIS_HOST=127.0.0.1
export ARK_DATABASE_REDIS_PORT=26791
# Set ARK_DATABASE_REDIS_USER / ARK_DATABASE_REDIS_PASSWORD when required.
bash scripts/test_databases.sh database-build redis
# Or: ./database-build/tests/database/ark_database_redis_tests
```

Stop the disposable server after testing. The runner never starts/stops services,
installs tools or selects a database endpoint for the real-service checks. Do not
point write-enabled checks at application data. MySQL settings and workload are
specified in [MYSQL.md](MYSQL.md). `all` runs the local, Redis and enabled MySQL
executables and requires both real-service configurations.

## Internal contract and lifetime

The library owns network IO. Native callbacks only move owning results into the
module's reserved completion records; business callbacks run from `update()` on
the constructing thread. The service must be submitted, updated, stopped and
destroyed on that thread. Input container/string capacities are charged while
waiting for a connection; reservations remain occupied until business callback
execution and capture destruction finish. Native result-size limits still apply;
arbitrary user callback captures are outside the input-byte quota.

The existing `submit*()` methods return false for an unaccepted request with no
callback. New private `start*()` methods return a logical request ID and report
rejection through a terminal callback even when returning ID zero. Their
`cancel(id)` requests cancellation; `stop_accepting()` closes admission while
preserving accepted terminal delivery. Accepted operations are
completed once unless `stop()` removes their not-yet-started business callbacks.
One checked deadline covers acquisition and execution. Native server/transport
errors, owning results and uncertain write outcomes are preserved; no write is
replayed. The public modules expose native owning results through coroutine
operations. The [coroutine bridge tests](COROUTINES.md) document owner-executor,
bounded delivery, logical cancellation and close/drain contracts.

`stop()` suppresses business callbacks and requests pool closure. It **does not
wait** for native callbacks, transports, TLS resources or IO threads to be destroyed.
The fix for reentrant capture destruction pins the queue and closes the native pool
before clearing captures; it does not add an IO-drain barrier. These helpers are
used by runtime plugins together with their separate native retirement barrier.
These checks do not certify OS library unloading or hot update.

## Public plugin checks

`ark_database_public_redis_tests` and `ark_database_public_mysql_tests` exercise
owning results, cancellation, TLS and graceful drain through local protocol
peers. `ark_database_public_redis_plugin_tests` and
`ark_database_public_mysql_plugin_tests` load actual hidden-symbol plugin
libraries. MySQL checks require SSL; real-server targets still require explicit endpoint settings.

The public MySQL peer also holds the sole pool lease in a transaction and fills
the one-waiter limit before cancelling the waiting query. It verifies a known-unsent
cancellation, owner-thread delivery, the still-usable transaction and query/prepared
work on the same socket during manager drain. This is a local protocol regression,
not real-server cancellation acceptance.

## Public modules against real services

`ark_database_public_service_tests` calls the public `mysql_module` and
`redis_module` interfaces with their real implementations, a registered consumer's
`task_group`, and the manager's shared `manual_runtime`. It is a separate C++
executable; `check-database` compiles it without running it or connecting to a server.

```sh
cmake --build database-build --target ark_database_public_service_tests --parallel 2
# Configure the isolated endpoints first, then choose either protocol or both.
bash scripts/test_databases.sh database-build public-redis
bash scripts/test_databases.sh database-build public-mysql
bash scripts/test_databases.sh database-build public-services
# Equivalent direct invocations:
./database-build/tests/database/ark_database_public_service_tests redis
./database-build/tests/database/ark_database_public_service_tests mysql
```

Redis requires `ARK_DATABASE_REDIS_HOST` and `ARK_DATABASE_REDIS_PORT`. Optional
`ARK_DATABASE_REDIS_USER` and `ARK_DATABASE_REDIS_PASSWORD` configure authentication;
use an authenticated disposable service to verify that path. The check mutates the
caller's configuration after registration, verifies an ordered binary ECHO/ECHO/PING
pipeline and a follow-up command, and makes no writes.

MySQL requires `ARK_DATABASE_MYSQL_HOST`, `ARK_DATABASE_MYSQL_PORT`,
`ARK_DATABASE_MYSQL_USER`, `ARK_DATABASE_MYSQL_PASSWORD` (present, possibly empty),
`ARK_DATABASE_MYSQL_DATABASE` and `ARK_DATABASE_MYSQL_CA`. TLS and peer verification
are required. Use a disposable schema and an account with CREATE, DROP, SELECT and
INSERT privileges. The check creates a unique `ark_public_service_...` InnoDB
table, exercises text queries, binary prepared results, transaction queries and
prepared inserts, then verifies committed data and the absence of rolled-back
data. It drops its table after the workflow, including assertion failures when the
connection remains usable. A failed connection, active transaction cleanup, or
forced process exit can leave the table for disposable-schema cleanup.

Both protocols assert owner-thread/shared-executor coroutine resumption, input and
configuration ownership, and result ownership after subsequent operations and
module/runtime destruction. The manager enters natural drain with accepted work
pending; follow-up requests finish before successful, idempotent stop. There is no
cancellation or service fault injection in these real-service cases. Each protocol
also repeats its workflow with deliberate host-stack unwinding while IO is pending:
named coroutine functions own their input values, the owner destructor drains the
workflow, and the outer scope verifies its completion and retained results.
Protocol-peer and DSO regressions remain separate checks; these results do not establish dynamic-library
unloading, failure recovery or capacity acceptance.

The executable requires explicit endpoints, returns nonzero for missing/invalid
configuration or failure, and does not print credentials or arbitrary native error
messages. It never starts or stops servers. `all` also includes both public
real-service modes and therefore requires an SSL-enabled build and both endpoint
configurations.

## Real plugin DSOs against real services

`ark_database_public_plugin_service_tests` loads the existing hidden-symbol Redis
or MySQL plugin DSO through the real plugin manager and calls only public
module/manager/task interfaces. Its executable does not compile or link the
module implementation objects. `check-database` builds it and its plugin DSOs
without running the real-service workflows.

```sh
cmake --build database-build --target ark_database_public_plugin_service_tests --parallel 2
./database-build/tests/database/ark_database_public_plugin_service_tests redis
./database-build/tests/database/ark_database_public_plugin_service_tests mysql
```

Use the same `ARK_DATABASE_*` endpoint/authentication variables described above;
MySQL requires an SSL-enabled build, its configured schema and verified TLS CA.
Redis checks ordered binary/empty ECHO replies, PING and a follow-up command.
MySQL checks signed/text/NULL query fields, a binary prepared result and statement
close, plus transaction queries followed by confirmed commit and rollback. These
workflows perform no writes and create no tables; they do not test transaction
persistence or isolation.

The separate opt-in `mysql-transactions` mode writes only to an owned, disposable
IPv4 loopback MySQL service. Use a fresh private data directory and dedicated
schema/account with CREATE, DROP, SELECT and INSERT privileges, required TLS and
a trusted CA. It uses the same MySQL environment variables and rejects hosts
other than `127.0.0.1` before connecting:

```sh
./database-build/tests/database/ark_database_public_plugin_service_tests mysql-transactions
```

This mode creates a unique `ark_dso_transaction_...` InnoDB table. Parameterized
autocommit and transaction inserts preserve binary values containing NUL, quotes
and a backslash. Two configured public connections have distinct server connection
IDs; the observer runs in autocommit mode. Each transaction reads its own write,
while the observer sees no row before completion. The observer then sees the
committed row and no rolled-back row. Both terminal outcomes must be confirmed;
cancellation is not used. Owning query/transaction results survive later operations,
table removal and module/manager/runtime destruction. The accepted workflow runs
during natural manager drain, with owner-thread/shared-executor checks at every
continuation. Successful workflows drop their table. Assertion failures attempt
explicit rollback of an unfinished scope and table cleanup while the connection
remains usable; a service failure can leave the table for disposable-schema cleanup.
The program never starts/stops a server.
This verifies two-connection commit visibility, not every isolation level,
crash durability, restart recovery or deployment capacity. `check-database` only
builds this mode; it is not included in `all`.

A registered consumer's accepted task is pending when stop seals admission,
before the first owner poll. All operations then finish during natural drain;
every continuation and completion must use the owner thread/shared executor.
Successful, idempotent stop runs the plugin's normal module teardown. Retained
results are checked after the host scope destroys the manager and its runtime.
This is not OS dynamic-library unloading, hot-update, cancellation, failure
recovery or capacity acceptance.

Missing/invalid configuration or an unavailable protocol fails before service
work. The executable does not start servers or print credentials/native server
text. Building it and checking those failures do not establish successful
real-service DSO acceptance; that requires explicitly running each selected mode
against isolated services.

The additional opt-in `redis-tls`, `redis-tls-untrusted-ca` and `redis-tls-wrong-host` modes use a real authenticated TLS service. See the [Redis TLS DSO guide](redis_tls.md) for trusted-IP/hostname controls, private service requirements and independent PING/ECHO counter observation. They are compiled but not run by `check-database`.

## Real plugin request cancellation

`ark_database_public_plugin_cancellation_tests` is a separate opt-in runner for real Redis/MySQL plugin DSOs and owned services. It observes tagged server work before `task_group::cancel(id)`, retains native cancellation/unknown results, and checks owner-thread completion and natural retirement. The evidence covers the observed IDs, effects and bounded observation window; MySQL's final zero-or-one counter is not proof of rollback or every possible retry race. `check-database` compiles this runner without starting services. See the [cancellation guide](cancellation.md) for build commands, trusted TLS, exclusive write data and precise limits.

## Public real-service failure checks

The separate `ark_database_public_failure_service_tests` executable checks wrong
Redis/MySQL passwords, an untrusted MySQL CA, and real slow-command deadlines.
`check-database` compiles it without running it. Choose a failure mode explicitly;
these modes are not included in `all`.

```sh
cmake --build database-build --target ark_database_public_failure_service_tests --parallel 2
bash scripts/test_databases.sh database-build public-redis-failures
bash scripts/test_databases.sh database-build public-mysql-failures
# Or invoke the executable with redis or mysql.
```

Use the same isolated endpoint variables as the public service checks above.
The Redis failure mode additionally requires a nonempty
`ARK_DATABASE_REDIS_PASSWORD` and permission for `CLIENT LIST`. MySQL requires SSL,
a trusted `ARK_DATABASE_MYSQL_CA` and access to the current account's sessions in
`information_schema.processlist`; no PROCESS privilege or extra schema is needed.
The unrelated CA fixture is `ARKNET_SOURCE_DIR/tests/certs/wrong.pem`, already used
by the local TLS regressions. No new environment variable or service management is
introduced. Missing configuration, observer permission errors, or a requested
protocol unavailable in the build fail with nonzero status.

Credential/TLS failures must retain their native error and known-unsent business
command state. Subsequent explicit requests use the valid configuration. The
warmed primary then runs BLPOP on an absent unique `ark_failure_...` key (30-second
server block) or a tagged `SELECT SLEEP(10)`, with a two-second client deadline.
BLPOP never receives producer data; the test first checks that its key is absent.
Both deadlines must be delivered within five seconds.

An independently configured connection observes Redis's named blocked client via
`CLIENT LIST`, or MySQL's tagged sleeping session via `processlist`. Only this
server-visible observation establishes that the request was sent; polling timers
only throttle observations. The timeout must retain its unknown-execution flag.
An explicit new PING/SELECT must recover through the same public connection name.
The observer rejects a second slow client/session, then checks for 500 ms after
the original disappears. This bounds the no-replay evidence to these tagged
slow requests during the observed recovery window; it does not prove write-replay
behavior or absence of later activity outside the window.

All coroutines and task completions must stay on the owner thread/shared manual
runtime. Accepted slow/observer tasks finish during natural manager drain before
native retirement and idempotent stop. These cases do not test cancellation,
server termination, dynamic-library unloading or capacity. Slow-query results
remain owned after module destruction. Native/server error text and credentials
are not printed.

## Local public-service acceptance

The recorded machine, revisions and outcomes are in [database service acceptance](../../docs/guides/database-validation.md).

## Sample plugin assembly

`check-database-samples` is part of `check-database`. It builds the real Redis sample module and plugin assembly together as `ark_database_redis_sample_check`, then runs `ark_database_redis_sample_tests`. With `ARKNET_ENABLE_SSL=ON`, the same path builds and runs `ark_database_mysql_sample_check` and `ark_database_mysql_sample_tests`. SSL-disabled builds retain the existing MySQL exclusion.

```sh
cmake --build database-build --target check-database-samples --parallel 2
```

Each runner loads the actual DSO through the manager's normal entry and exit symbols. A fixture registers the public database interface and rejects its first configuration request. The sample must resolve that interface, preserve its `primary` (MySQL) or `sample` (Redis) connection name, and report a controlled init failure whose diagnostic names the concrete sample module. The real plugin exit must return successfully, the manager must reach its stopped state, and repeated stop must succeed. The runner uses public manager APIs; it does not inspect the module registry after shutdown or independently prove its erased state. Compile-time checks preserve typed bases and inherited default update behavior; assembly explicitly selects `module_update::disabled`.

These tests replace module-only compilation: plugin entry/exit sources are required dependencies even when application/examples are disabled. They preserve original source licenses and do not enable other examples. The rejecting provider does not run sample coroutines or contact services; live MySQL/Redis workflow and deployment acceptance remain separate.

## Owned-server termination acceptance

The opt-in [public-plugin server-termination guide](server_termination.md) covers
server-observed blocking read-only work, a separately verified owned-PID fault
actor, native transport failure and exactly-once local retirement. Ordinary
checks compile the program without executing a server fault.

## Redis restart recovery acceptance

The opt-in [restart operations manual](restart.md) covers private service setup,
fault coordination and cleanup. The [short guide](../../docs/guides/database-restart.md)
summarizes existing same-pool recovery and local results. Ordinary checks only
compile the runner.

## MySQL consistent-read isolation

The SSL-only `ark_database_public_plugin_isolation_tests` program verifies public
`begin()` transactions against the explicitly configured server defaults
`REPEATABLE-READ` and `READ-COMMITTED`, including query-lease session reset,
independent pinned connections and committed snapshot visibility. It requires an
owned authenticated loopback service with verified TLS and a disposable schema.
`check-database` builds it without running service work. See the
[isolation guide](../../docs/guides/database-isolation.md) for both invocations,
privileges, cleanup and limits.

The same cancellation runner's SSL-only `mysql-transaction` mode cancels a sent
query inside a public transaction after independent server observation. It retains
an unconfirmed/discarded transaction result, separately checks the original
session's disappearance and final uncommitted-write visibility, and recovers with
an explicit new request through the existing pool. See the
[transaction cancellation details](cancellation.md#mysql-transaction-operation-cancellation).
