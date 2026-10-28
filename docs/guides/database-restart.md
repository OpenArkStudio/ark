# Redis restart recovery through the public plugin

The public-plugin runner verifies explicit new read-only requests after an owned
Redis service restarts at the same endpoint. It loads the real plugin DSO and
retains the original named pool throughout. Build commands, private service
requirements and fault coordination are in the
[restart operations manual](../../tests/database/restart.md).

## Recovery behavior and results

`configure()` rejects duplicate names and does not replace a pool. The pinned
arknet pool discards disconnected leases; a later acquire can create a connection.
Recovery below uses new commands submitted explicitly by the caller, with no
replay of the interrupted operation or new business retry policy.

| Stage | Verified result |
| --- | --- |
| Server-observed read interrupted by the fault script | `disconnected`, native transport error, `command_may_have_run=true` |
| New PING while the service is stopped | `connection_failed`, native transport error, `command_may_have_run=false` (known unsent) |
| Replacement service authenticated at the same endpoint | New PING and binary ECHO succeed through the original pool |
| Natural shutdown | Owner-executor completion once, zero pending tasks, idempotent stop and retained results |
| Cleanup | Both owned PIDs and listener absent; private credentials, data and markers removed |

## Local acceptance record

Passed on 2026-10-29 using authenticated plain TCP on local IPv4 loopback:
Mac mini, Apple M4 Pro (14 cores), 48 GiB RAM; macOS 27.0.1 arm64;
AppleClang 21.0.0.21000334, CMake 4.3.3, Boost.Asio, OpenSSL 3.6.4 and Redis 7.4.2.

| Source | Identity |
| --- | --- |
| ARK base | `7815493a2cc8c0413372f99277481b653f871133` |
| arknet pin | `a0dfe09cd543e75cfb5996e537ce2fa49f2a758f` |
| Runner Git blob | `c58095a676af7896685fc3dc29e41caca1b56865` |

The local arknet source matched the pinned reference. Raw logs and source/binary
hashes remain local. This single TCP restart covers read-only recovery; TLS,
MySQL, write outcomes, persistence, production high availability and capacity
remain outside this result. Local service execution is separate from hosted CI
and deployed-service acceptance.
