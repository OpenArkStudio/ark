# MySQL consistent-read isolation

Public `begin()` uses the server's default isolation level. The acceptance program verifies two explicitly configured private MySQL services through the public plugin coroutine API.

## Results

| Reader observation | REPEATABLE READ | READ COMMITTED |
| --- | --- | --- |
| Writer has not committed | Original row; inserted row absent | Original row; inserted row absent |
| Writer committed; same reader transaction | Original row; inserted row absent | Updated row and inserted row visible |
| Reader transaction ended; new read | Both committed changes visible | Both committed changes visible |

Both transactions retain independent, pinned connections. Owning query results remain valid after table removal and host teardown; accepted tasks complete once on the owner's shared executor.

## Run

```sh
cmake --build database-build --target ark_database_public_plugin_isolation_tests --parallel 1
# Server default: --transaction-isolation=REPEATABLE-READ
./database-build/tests/database/ark_database_public_plugin_isolation_tests repeatable-read
# Separate private server default: --transaction-isolation=READ-COMMITTED
./database-build/tests/database/ark_database_public_plugin_isolation_tests read-committed
```

The argument declares the expected level; it does not set it. Use private authenticated services with verified TLS. See the [operations manual](../../tests/database/isolation.md) for configuration, permissions, cleanup and complete verification details. `check-database` compiles the program without starting services.

Returning a query lease executes `COM_RESET_CONNECTION`. Setting a session level with a standalone `query()` cannot configure a later `begin()`; the program verifies that reset boundary. See the [MySQL transaction rules](https://dev.mysql.com/doc/refman/8.4/en/set-transaction.html).

## Local acceptance

| Item | Configuration / outcome |
| --- | --- |
| Date and machine | 2026-10-29; Mac mini, Apple M4 Pro, 14 cores, 48 GiB RAM |
| System / build | macOS 27.0.1 arm64; Apple Clang 21.0.0; C++20 Debug |
| Dependencies | MySQL 8.4.13; Boost.Asio 1.90.0; OpenSSL 3.6.4 |
| Source | ark `093dfcb9a430beedcd86de19eb0d70c8aae9160e`; arknet `a0dfe09cd543e75cfb5996e537ce2fa49f2a758f` |
| Positive cases | Both levels passed against fresh private services at `127.0.0.1:29879` |
| Negative controls | Opposite level rejected on each server; five invalid configurations rejected |
| Cleanup | No remaining tables, owned processes, listener, private data or credentials |

Program source is unchanged from the tested revision. Raw evidence stays local. These plain nonlocking reads do not establish locking-read or `SERIALIZABLE` behavior, cancellation, durability, high availability, other-platform acceptance or capacity. [Restart recovery](database-restart.md) has its own record.
