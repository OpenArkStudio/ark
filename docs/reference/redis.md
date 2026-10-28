# Redis coroutine plugin

`redis_module` uses arknet's native Redis pool. This replaces the former bredis
synchronous and callback APIs; migrate `connect`, `set`, `get` and command-specific
wrappers to named configuration and `co_await command` / `pipeline`. There is no
blocking compatibility facade. Applications must rebuild against this interface.

The public interface's single source is `src/plugins/redis/redis_module.hpp`, beside the implementation and private plugin assembly. Link `ark::redis` to obtain its configure-time build-tree copy at `ark/redis/redis_module.hpp`, even when the runtime plugin is disabled. A handwritten `-I <source>/include` alone is insufficient. The implementation class lives only in `.cpp`; the former `redis_module_impl.hpp` and plugin assembly header are private implementation inputs. Rebuild the host, plugins and affected consumers together; installed consumption uses `find_package(ark CONFIG REQUIRED COMPONENTS redis)` and the same `ark::redis` target. See [installation](../get-started/installation.md#install-and-consume-the-cmake-package) for dependencies and relocatable packages. Installation does not provide an old-binary compatibility guarantee or load a runtime module.

```cpp
// Consumer members: ark::redis_module* redis_{}; ark::task_group* tasks_{};
// In init(), after module registration:
redis_ = find_module<ark::redis_module>();
if (!redis_) {
    throw std::runtime_error("Redis plugin is not registered");
}
tasks_ = &get_plugin_manager()->create_task_group(*this);
ark::redis_options options;
options.pool.connection.host = "127.0.0.1";
options.pool.connection.port = "6379";
options.pool.max_connections = 8;
options.max_operations = 64;
options.max_input_bytes = 1024 * 1024;
if (!redis_->configure("cache", std::move(options))) {
    throw std::runtime_error("Redis configuration unavailable");
}

// Start this member coroutine once in post_init() through tasks_->start(workflow(), on_complete).
// on_complete handles std::exception_ptr; see the managed task guide.
ark::task<> workflow() {
    arknet::redis::command read{"GET", "key"};
    auto result = co_await redis_->command("cache", std::move(read));
    if (!result) {
        // Inspect result.error, transport_error and command_may_have_run.
        co_return;
    }
    // result.replies owns its strings, arrays and binary data; inspect error replies too.
}
```

Configuration owns its strings, credentials, pool, protocol, response limits and
TLS settings. `configure` returns false for a duplicate name or sealed admission;
empty names and invalid limits throw. It may run before the Redis module's own
`init`: the installed manager already supplies its task runtime. The official plugin
does not read database options from the application manifest's `config` path or from
a database XML preset. Keep that plugin's `config` empty and call `configure()` with
owning options from a business module. Configuration is owner-thread only and is
not dynamically replaced. Set `options.tls = true` and configure certificate
verification through `options.pool` when building with `ARKNET_ENABLE_SSL`;
requesting TLS in a build without SSL throws. See [build and runtime activation](build-options.md#native-database-plugins)
and [managed task lifecycle](../guides/tasks.md).

Command factories take owning values and preserve inputs before the lazy task
starts. The returned task also pins its connection state, including when the
facade is destroyed before an unstarted task is used. Run operations on the exact
manager task executor. A foreign executor throws before native submission.
Unknown connection names return native `not_connected` (`closed` after sealing).
Local bridge admission failures throw; invalid deadlines return native
`invalid_command`. Native transport/database failures remain `arknet::redis::result`.
Redis error replies remain values in the ordered `replies` vector. A true result
reports operation status; it does not mean every Redis reply is successful.

`pipeline(name, commands, timeout)` returns one native result containing ordered
replies. Its timeout covers pool acquisition and execution. `max_operations`
bounds bridge records and service operations retained until owner delivery;
`max_input_bytes` bounds service-retained command storage, including allocated
capacity. Input rejection reports native `queue_full`. Configure native pool,
waiter and response bounds separately in `options.pool`. These are per-connection
limits, not a whole-process memory limit.

Cancellation requests flow from the manager's managed task group into the native
service. A cancelled waiting acquire is known unsent; cancellation after sending
can have `command_may_have_run == true`. No write replay or synthetic success is
introduced. The native result, including partial replies, stays owned by its
caller after pool retirement.

The Redis plugin runs `service.update()` and then bridge dispatch during manager
updates. The manager polls its shared task runtime centrally and resumes consumer
coroutines on its owner thread. arknet owns native
IO and worker threads. Application code must keep updating the manager while
stopping it: `stop()` can return false while accepted workflows finish, including
later commands in the same workflow. Readiness only observes logical activity;
it does not seal the service early. After all managed tasks and delivery stacks
have returned, `pre_shut` seals admission and calls native `retire_native()` from
the host lifecycle frame. A false return or exception preserves resources and
prevents `shut` from releasing them. `service.stop()` is not the retirement
barrier. Calling lifecycle functions recursively from a running task is not a
supported teardown path; manager shutdown defers that work to its host frame.

`tests/database/public_redis_module.cpp` covers the public facade against a
bounded loopback RESP protocol peer: owning input/results, ordered pipeline
replies, foreign executor rejection, quotas, cancellation, verified TLS and graceful
manager stop. This peer is not a Redis implementation. Private TLS regressions
cover additional underlying-service failure cases. These local checks do not establish real Redis
deployment acceptance, production capacity or a general leak-free guarantee.

`ark_database_public_redis_plugin_tests` additionally builds and loads the actual
plugin library, exercising macro registration, coroutine delivery and host
shutdown. This does not certify OS library unloading, hot update or real Redis
server acceptance.

`ark_database_public_service_tests` uses the real module implementation,
a consumer task group and the manager's shared runtime. It covers authentication
configuration, an ordered binary pipeline, input/configuration/result ownership,
owner-thread resumption, natural drain and host-stack unwinding with work pending.
`ark_database_public_failure_service_tests` checks a wrong password and a server-observed BLPOP
deadline, preserving native errors and `command_may_have_run`, then requires an
explicit new PING to recover.

```sh
bash scripts/test_databases.sh database-build public-redis
bash scripts/test_databases.sh database-build public-redis-failures
```

Use the isolated endpoint settings in the [database test guide](../../tests/database/README.md#public-modules-against-real-services).
The failure mode requires a nonempty password and permission for `CLIENT LIST`.
Missing configuration fails; it is not a successful skip. `check-database`
compiles both real-service executables without running them, and failure modes
require explicit selection. These cases do not test request cancellation, server
termination, dynamic-library unloading or capacity. The observed no-replay window
covers tagged slow requests, not general write-replay behavior; see the
[failure-check limits](../../tests/database/README.md#public-real-service-failure-checks).

The recorded machine, revisions and outcomes are in [local public-service acceptance](../guides/database-validation.md).
The [Redis restart acceptance](../guides/database-restart.md) records recovery through explicit new commands on the existing pool.
