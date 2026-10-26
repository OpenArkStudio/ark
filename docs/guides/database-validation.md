# Database service acceptance

This record separates public-module service/failure checks from positive workflows through actual plugin DSOs. Each scope has its own tested revisions and environment; a passing result applies only to that scope.

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

## Acceptance boundaries

The DSO workflows exercise normal plugin teardown and native close calls. Successful `FreeLibrary` or `dlclose` does not prove that the operating system physically removed the library image. Request cancellation, server termination, OS image removal, other deployment environments and cross-machine capacity still require separate acceptance. Neither test scope establishes arbitrary hot-update safety or production capacity.
