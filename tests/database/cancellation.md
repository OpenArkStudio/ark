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

These are opt-in cancellation scenarios, separate from positive natural-drain modes and deadline/error fixtures. They do not establish Redis TLS, transaction cancellation, every failure race, external-service interoperability, cross-platform acceptance, hot update or capacity. Exact executed revision/runtime identities and cleanup evidence belong to the local handoff; this test description alone is not a passing run.
