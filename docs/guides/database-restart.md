# Database restart recovery through public plugins

The public-plugin runner verifies explicit new read-only requests after an owned
Redis or MySQL service restarts at the same endpoint. It loads the real plugin
DSO and retains the original named pool throughout. Build commands, private
service requirements and fault coordination are in the
[restart operations manual](../../tests/database/restart.md).

## Recovery behavior and results

`configure()` rejects duplicate names and does not replace a pool. The pinned
arknet pool discards disconnected leases; a later acquire can create a connection.
Recovery below uses new commands submitted explicitly by the caller, with no
replay of the interrupted operation or new business retry policy.

| Stage | Verified result |
| --- | --- |
| Redis: server-observed read interrupted by the fault script | `disconnected`, native transport error, `command_may_have_run=true` |
| Redis: new PING while the service is stopped | `connection_failed`, native transport error, `command_may_have_run=false` (known unsent) |
| Redis: replacement service authenticated at the same endpoint | New PING and binary ECHO succeed through the original pool |
| MySQL: server-observed `SELECT SLEEP` interrupted | Native `transport`, transport error, `outcome_unknown=true`, no rows/server error |
| MySQL: new `SELECT 1` while stopped | Native `transport` and `connection_refused`, `outcome_unknown=false` (known unsent) |
| MySQL: replacement service authenticated with trusted TLS at the same endpoint | Explicit new query returns integer 42 and an embedded-NUL string through the original pool |
| Natural shutdown | Owner-executor completion once, zero pending tasks, idempotent stop and retained results |
| Cleanup | Both owned PIDs and listener absent; private credentials, data and markers removed |

## Local acceptance record

Passed on 2026-10-29 on local IPv4 loopback: Redis 7.4.2 with authenticated plain
TCP and MySQL 8.4.13 with required trusted TLS and a SELECT-only account.
Mac mini, Apple M4 Pro (14 cores), 48 GiB RAM; macOS 27.0.1 arm64;
AppleClang 21.0.0.21000334, CMake 4.3.3, Boost.Asio and OpenSSL 3.6.4.
The MySQL observer confirmed the tagged query's `User sleep` state before the
owned PID was killed. Both replacement services used the same private data/config
and endpoint; new read-only requests succeeded without reconfiguring either pool.

| Source | Identity |
| --- | --- |
| ARK base | `e77c2d8a1fe59d8519c447376c1f5d9a267935dd` |
| arknet pin | `a0dfe09cd543e75cfb5996e537ce2fa49f2a758f` |
| Runner Git blob | `0b31502822ad0e181828b42a50859c2b093aa227` |

The local arknet source matched all 921 pinned files. Raw logs and source/binary
hashes remain local. These single-endpoint restarts cover read-only recovery.
Redis TLS restart, write outcomes, transaction recovery, persistence, production
high availability and capacity remain outside these results. Local service
execution is separate from hosted CI and deployed-service acceptance.
