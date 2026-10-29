# MySQL consistent-read isolation through the public plugin

`ark_database_public_plugin_isolation_tests` loads the real hidden-symbol MySQL
plugin DSO and uses the public coroutine, manager and task interfaces. It checks
`REPEATABLE READ` and `READ COMMITTED` on owned MySQL 8.4 services with explicitly
configured server defaults. Public `begin()` has no isolation-level parameter.

## Run the acceptance program

Configure the [SSL-enabled database build](README.md), then
build the opt-in program:

```sh
cmake --build database-build --target ark_database_public_plugin_isolation_tests --parallel 1
# With the private server started using --transaction-isolation=REPEATABLE-READ:
./database-build/tests/database/ark_database_public_plugin_isolation_tests repeatable-read
# With a separate private server using --transaction-isolation=READ-COMMITTED:
./database-build/tests/database/ark_database_public_plugin_isolation_tests read-committed
```

Use a fresh private server data directory, dedicated schema and authenticated
account with CREATE, DROP, SELECT, INSERT and UPDATE on that schema. Require TLS
and provide its trusted CA. Set `ARK_DATABASE_MYSQL_HOST=127.0.0.1` and the existing
`ARK_DATABASE_MYSQL_PORT`, `ARK_DATABASE_MYSQL_USER`, `ARK_DATABASE_MYSQL_PASSWORD`,
`ARK_DATABASE_MYSQL_DATABASE` and `ARK_DATABASE_MYSQL_CA` variables. Missing values,
an empty password and any other host fail before connection. The program never
starts or stops a server. Successful cases drop their unique InnoDB table; failed
cases attempt rollback and table removal while the connections remain usable.
Dispose of the private schema and service after any failure.

Run each command against the matching private server configuration. The argument
declares an expectation; it does not configure the server or transaction. Inside
both public transactions the program verifies connection IDs and the session and
global isolation levels. Before `begin()`, it deliberately sets the reader's
session to the opposite level through a named query. Returning that query lease
uses [`COM_RESET_CONNECTION`](https://dev.mysql.com/doc/c-api/8.4/en/mysql-reset-connection.html);
the later transaction must again use the server
default on the same connection. Setting a session level with a standalone
`query()` cannot configure a later public `begin()` across this reset boundary.
This preserves the current API and pool behavior. See MySQL's
[transaction scope rules](https://dev.mysql.com/doc/refman/8.4/en/set-transaction.html).

Two named pools have one connection each. The reader's transaction establishes a
snapshot before the writer updates an existing row and inserts a new row. The
reader sees the original row and no inserted row before the writer commits.
After that commit, `REPEATABLE READ` retains the original snapshot until its
transaction ends; `READ COMMITTED` sees both committed changes on its next plain
`SELECT` within the same transaction. A read after ending the reader transaction
sees both committed changes for either level. Connection IDs verify independent
sessions and the pinned connection used for each transaction read.

The accepted workflow enters natural manager drain before its first poll.
Continuations and its single completion must run on the owner thread/shared
executor. Owning initial-snapshot and post-transaction replies remain valid after
later operations, table removal and module/manager/runtime destruction.
`check-database` compiles this program without starting services or running it.

## SQL and scope

The [MySQL 8.4 isolation documentation](https://dev.mysql.com/doc/refman/8.4/en/innodb-transaction-isolation-levels.html)
defines a transaction snapshot for `REPEATABLE READ` consistent reads and a fresh
snapshot for each `READ COMMITTED` consistent read. This program uses plain
nonlocking reads; it does not exercise locking reads, `SERIALIZABLE`, transaction
cancellation, crash durability, restart recovery, replication, high availability,
other platforms or production capacity. No isolation API, pool policy or default
isolation level is changed.

## Local acceptance

On **2026-10-29**, both invocations passed using ark source
`093dfcb9a430beedcd86de19eb0d70c8aae9160e` and arknet
`a0dfe09cd543e75cfb5996e537ce2fa49f2a758f`. The executed program source remained
unchanged when this record was added. The environment was macOS 27.0.1 arm64,
Apple Clang 21.0.0, C++20 Debug, Boost.Asio 1.90.0 and OpenSSL 3.6.4. Each case used
a fresh MySQL Community Server 8.4.13 instance at `127.0.0.1:29879`, with its
matching explicit server isolation setting, authentication and verified TLS/IP
identity. In each case the independently observed reader/writer connection IDs
were `11`/`10`.

The opposite expected isolation argument failed at the session/global-level
assertion against each real server. Missing host, nonloopback host, empty password,
missing argument and unknown isolation arguments also failed as intended.
Independent cleanup queries found zero remaining tables. Both owned servers
shut down through authenticated shutdown and were reaped with exit code `0`;
separate checks confirmed the exact PIDs, listener and private directories absent.
Private data, credentials and TLS keys were removed. Raw logs and binary/source
hashes remain local. This is macOS Boost Debug service acceptance; it does not
establish hosted CI or cross-platform acceptance.
