# Public database plugin cancellation acceptance

[简体中文](cancellation_CN.md)

`ark_database_public_plugin_cancellation_tests` loads the actual hidden-symbol database plugin through its DLL entry points. Its host links public interfaces only. The existing `check-database` target compiles this program but never starts services or executes its real-service modes.

Use only owned, authenticated services bound to `127.0.0.1`, fresh private directories and exclusive keys/schema. Verify executable path, child PID, PID file and loopback listener before use and before normal authenticated shutdown. Do not run these writes against a shared or production service. Credentials belong in private files/environment, never command arguments or evidence uploads.

```sh
cmake -S . -B database-build -G Ninja \
  -DARKNET_SOURCE_DIR=/path/to/the-pinned-arknet \
  -DARK_BUILD_APPLICATIONS=OFF -DARK_BUILD_PROTO=OFF \
  -DARK_BUILD_CORE_TESTS=OFF -DARK_BUILD_TESTS=OFF \
  -DARK_BUILD_CONFIG=OFF -DARK_BUILD_DATABASE_TESTS=ON \
  -DARKNET_ENABLE_SSL=ON
cmake --build database-build --target ark_database_public_plugin_cancellation_tests --parallel 1
# After supplying private ARK_DATABASE_* environment variables:
database-build/tests/database/ark_database_public_plugin_cancellation_tests redis
database-build/tests/database/ark_database_public_plugin_cancellation_tests mysql
```

Redis uses `ARK_DATABASE_REDIS_HOST`, `PORT`, `PASSWORD`, and optional `USER`, each with the `ARK_DATABASE_REDIS_` prefix. MySQL uses `ARK_DATABASE_MYSQL_HOST`, `PORT`, `USER`, `PASSWORD`, `DATABASE`, and `CA`. MySQL requires a trusted CA covering the IP, TLS enabled in the build/server, and an isolated account requiring SSL with `CREATE`, `INSERT`, `UPDATE`, `SELECT` and `DROP` rights confined to the fresh schema. Its observer sees the same account's connections; no global PROCESS privilege is needed.

The Redis task sends one `INCR`/`BLPOP` pipeline. An independent named pool observes `CLIENT LIST` with the unique client name, blocking flag and command, and confirms the counter is one before calling the existing `task_group::cancel(id)`. The test requires the original `client_errc::cancelled`, `command_may_have_run=true`, no synthesized transport error, and valid retained partial replies if any. An explicit new PING proves recovery; the tagged command must retire and the counter must remain one through the bounded observation. Exclusive keys are then deleted.

MySQL first creates a private one-row counter table. Its task executes one tagged `UPDATE ... SET n=n+1 WHERE id=1 AND SLEEP(10)=0`. The independent pool observes that statement in `information_schema.processlist` with `STATE='User sleep'` before cancelling. The original result must remain `connection_errc::cancelled`, `outcome_unknown=true` and `operation_aborted` transport status. The observer waits for the original server statement to retire before checking its final effect: zero or one is legitimate, more than one fails the no-replay check. This does not infer rollback from cancellation. An explicit new query verifies recovery; the private table is dropped afterward.

Both cases require one owner-thread/shared-executor continuation and exactly one root completion with no coroutine exception, retirement of the managed task, bounded natural manager/native shutdown, repeated stop, and an owning cancelled result surviving plugin/module/manager destruction. Repeated cancellation of a pending task succeeds; cancellation of its retired ID fails. Throttling timers do not establish that a command is active: only server observations do. No forced frame destruction, `cancel_all`, server-termination mode, automatic write retry or production policy change is introduced. Native close success does not prove physical image unmapping.

These are opt-in cancellation scenarios, separate from positive natural-drain modes and deadline/error fixtures. The earlier query modes do not establish Redis TLS, transaction cancellation or every failure race, external-service interoperability, cross-platform acceptance, hot update or capacity. Exact executed revision/runtime identities and cleanup evidence belong to the local handoff; this test description alone is not a passing run.

## MySQL transaction-operation cancellation

The additional SSL-only `mysql-transaction` mode uses the existing public
`begin()` and `query(transaction, ...)` interfaces; no new target is needed:

```sh
database-build/tests/database/ark_database_public_plugin_cancellation_tests mysql-transaction
```

Use the same private MySQL environment and schema-scoped privileges above, with a
nonempty password. The mode creates a unique InnoDB counter table. The request
begins a transaction, records its connection ID, increments the counter to one,
and retains its own successful read of that uncommitted value. It then submits a
tagged slow UPDATE within the same transaction. A distinct observer session must
see that exact connection and tagged statement in `User sleep`, and must still
read counter zero, before cancelling the managed request task.

A sent cancellation must retain `connection_errc::cancelled`,
`outcome_unknown=true` and `operation_aborted`. Its transaction status must remain
`unconfirmed` with `discard_requested=true`, no rollback reply/code and no cleanup,
cancellation or lease-return exception. This path discards the uncertain
connection; it does not issue a fictitious rollback through that closed connection.
The caller does not restart or replace the transaction with SQL or retry a write.

An explicit new query through the original named pool must succeed on a different
server connection. The observer tracks both the original connection ID and tagged
SQL until the whole original session disappears, then requires a further 500 ms
without its return or tagged replay. Only after that retirement does it require
counter zero and remove the table. This verifies the observed server-side discard
effect, including the earlier completed-but-uncommitted write. It does not turn
the API's unknown result into a confirmed rollback response or a durability claim.
A successful retained read must still contain one after cancellation, table
removal and module/manager/runtime destruction; the cancelled reply must preserve
its original diagnostics. The common single-completion, owner-executor, repeated
cancellation and natural retirement checks still apply.

The transaction mode covers cancellation of one sent transaction query. It does
not cover cancellation during acquire, BEGIN, prepare/execute/statement close,
commit or rollback, every cancellation race, crash recovery or automatic retry.
The independent observation window bounds its no-replay claim. The earlier
`mysql` autocommit case retains its zero-or-one outcome rule; this mode does not
change that contract. SSL-disabled builds reject `mysql-transaction` before
service work.

### Local transaction acceptance

On **2026-10-29**, `mysql-transaction` passed with program source
`28edd1a88c5e7179668aa387a9158d3db32a15f0` and pinned arknet
`a0dfe09cd543e75cfb5996e537ce2fa49f2a758f`, using macOS 27.0.1 arm64,
Apple Clang 21.0.0, Boost.Asio 1.90.0, OpenSSL 3.6.4 and a private authenticated,
verified-TLS MySQL Community Server 8.4.13. The primary/observer IDs were `13`/`14`;
the final counter was zero. A separate private-server general-log query counted
exactly one pre-write and one tagged slow UPDATE. The original `mysql` and `redis`
modes also passed, with final counters one; Redis used a separate authenticated
7.4.2 instance. Wrong-password and untrusted-CA controls failed; SSL-disabled
builds rejected both MySQL modes before service work. Independent queries found
zero remaining tables/keys; both owned services exited normally and their exact
PIDs/listeners, private data, credentials and keys were removed. This is local
macOS Boost Debug acceptance, separate from hosted or cross-platform checks.

## MySQL prepared execute cancellation

The SSL-only `mysql-statement` mode uses the existing public
`execute(transaction, sql, parameters)` API, whose implementation owns the
prepare/execute/close lifecycle. Use the same private MySQL configuration above:

```sh
database-build/tests/database/ark_database_public_plugin_cancellation_tests mysql-statement
```

It shares the transaction mode's pre-write, retained own-write read, independent
visibility, cancellation diagnostics, fresh-connection recovery, original-session
retirement, 500 ms quiet window and natural shutdown checks. The slow UPDATE
binds its increment and row ID as parameters. Before cancellation, the observer
must see the exact original session, unique SQL tag, `STATE='User sleep'` and
`COMMAND='Execute'`; this establishes that execute, rather than prepare, is active.
The cancelled reply must have neither a statement-close reply/exception nor a
rollback reply/code. Final counter zero is an observed effect of discarding this
connection, not an API-confirmed rollback or global exactly-once guarantee.

This mode covers one sent prepared execution in a transaction. Cancellation during
acquire, BEGIN, prepare, statement close, commit or rollback and other execute races
remain outside it. SSL-disabled builds reject this mode before service work.
The [prepared cancellation record](../../docs/reports/database-services.md#mysql-prepared-execute-cancellation)
contains its exact local execution identity and controls; earlier records keep
their own tested revisions and limits.
