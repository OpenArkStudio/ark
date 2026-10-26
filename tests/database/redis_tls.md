# Real Redis TLS plugin acceptance

[简体中文](redis_tls_CN.md)

This opt-in standalone C++ program loads the actual hidden-symbol Redis plugin DSO through `plugin_manager`, uses only public Redis/manager/task interfaces, and makes no key writes. It never starts or stops a server. `check-database` compiles the runner without executing these real-service modes; the existing `redis_service_tls.cpp` protocol-peer tests remain a separate automatically run regression.

## Build and owned-service requirements

Use the [database build](README.md#build) with `ARK_BUILD_DATABASE_TESTS=ON`, `ARKNET_ENABLE_SSL=ON` and the pinned local arknet source. Then build:

```sh
cmake --build database-build --target ark_database_public_plugin_service_tests --parallel 1
```

Prepare an exclusively owned disposable Redis instance with a fresh private data directory, a random loopback TLS port, authentication, persistence disabled and the plaintext port disabled (`port 0`). Build Redis with `BUILD_TLS=yes` and the same available OpenSSL toolchain. Do not change shared or installed services. Keep credentials, private keys and service configuration private; pass the password through the environment, never command arguments or logs. Verify the child PID, executable, private directory, PID file and PID-specific listener before use and before authenticated `SHUTDOWN NOSAVE`; reap the child and remove private data and secrets on every outcome.

For the hostname control, issue the server certificate from the test CA with an **IP SAN for `127.0.0.1` only**, without a `localhost` DNS SAN or matching common name. Confirm its chain and IP with OpenSSL before the tests. `localhost` must resolve to that same loopback server; an ordinary connection refusal does not pass the rejection mode. Prepare a second valid but unrelated CA certificate for the untrusted-CA mode. Client certificate authentication is not tested (`tls-auth-clients no`); password authentication remains required over verified TLS.

Set these variables in the private test environment:

| Variable | Meaning |
| --- | --- |
| `ARK_DATABASE_REDIS_HOST` | `127.0.0.1`, matching the server certificate IP SAN |
| `ARK_DATABASE_REDIS_PORT` | Owned instance's TLS port |
| `ARK_DATABASE_REDIS_PASSWORD` | Required nonempty password |
| `ARK_DATABASE_REDIS_USER` | Optional ACL username |
| `ARK_DATABASE_REDIS_CA` | Trusted CA PEM file |
| `ARK_DATABASE_REDIS_UNTRUSTED_CA` | Valid unrelated CA PEM file |
| `ARK_DATABASE_REDIS_WRONG_HOST` | `localhost`, resolving to the same server but absent from its certificate |

```sh
./database-build/tests/database/ark_database_public_plugin_service_tests redis-tls
./database-build/tests/database/ark_database_public_plugin_service_tests redis-tls-untrusted-ca
./database-build/tests/database/ark_database_public_plugin_service_tests redis-tls-wrong-host
```

## Assertions and observation limits

The positive mode requires an authenticated TLS command with an owning binary ECHO reply, followed by an ordered binary/empty ECHO and PING pipeline. Configuration is copied before the caller mutates its host/password/CA values; the earlier command result must remain readable after the pipeline. Both owning results remain readable after module/manager/runtime destruction and native DSO close.

Each rejection mode attempts PING but requires native `client_errc::connection_failed`, an OpenSSL certificate-verification transport error, `command_may_have_run=false` and no replies. Connection refusal, timeout, SQL/Redis server errors or a coroutine exception cannot pass. The owning error is checked again after teardown. The host's task completes once on the owner thread/shared executor; normal `manager.stop()/update()` drains accepted work, retires native services and supports repeated stop. A test observation bound does not make cooperative stop a hard or preemptive production deadline.

For independent business-command observation, take authenticated `redis-cli --tls --cacert` `INFO commandstats` snapshots immediately before and after each rejection, without issuing PING/ECHO or running other clients inside that window. Compare `cmdstat_ping` and `cmdstat_echo` call counts; INFO itself changes other statistics. Unchanged counts support only the observed PING/ECHO window. The C++ runner does not read server counters and does not by itself prove that no RESP command reached a real server. The bounded protocol-peer regression separately counts all received RESP commands.

These modes do not verify mutual TLS, certificate rotation/revocation, TLS cancellation/server faults/recovery, persistence, cross-machine capacity or other platforms. Successful native close does not prove physical OS library image removal or arbitrary hot-update safety.
