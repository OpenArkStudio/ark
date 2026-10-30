# Redis coroutine plugin

`redis_module` uses arknet's native Redis pool. This replaces the former bredis
synchronous and callback APIs; migrate `connect`, `set`, `get` and command-specific
wrappers to named configuration and `co_await command` / `pipeline`. There is no
blocking compatibility facade. Applications must rebuild against this interface.

| Interface use | Contract |
| --- | --- |
| Single source | `src/plugins/redis/redis_module.hpp`, beside implementation/private assembly |
| Build | `ark::redis` maps `ark/redis/redis_module.hpp` at configure time, even with runtime plugins disabled; handwritten `-I <source>/include` alone is insufficient |
| Private implementation | Concrete class only in `.cpp`; `redis_module_impl.hpp` and plugin assembly header stay private |
| Installed consumption | `find_package(ark CONFIG REQUIRED COMPONENTS redis)`, still linking `ark::redis`; dependencies/relocation in [installation](../get-started/installation.md#install-and-consume-the-cmake-package) |

Rebuild host, plugins and affected consumers together. Installation promises neither old-binary compatibility nor runtime-module loading.

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

Configuration owns strings, credentials, pool/protocol/response limits and TLS settings; owner-thread only, without dynamic replacement. `configure` returns false for duplicate/sealed admission and throws for empty names/invalid limits. It may precede module `init`: the installed manager supplies the runtime. No manifest `config` or database XML reader; leave `config` empty and pass owning options through a business module’s `configure()`. With `ARKNET_ENABLE_SSL`, use `options.tls = true` and `options.pool` certificate verification; requesting TLS without SSL throws. See [build/runtime activation](build-options.md#native-database-plugins) and [managed task lifecycle](../guides/tasks.md).

Command factories own inputs before lazy start. Returned tasks pin connection state even if the facade is destroyed before an unstarted task is used.

| Request boundary | Result |
| --- | --- |
| Executor | Exact manager task executor required; foreign executors throw before native submission |
| Unknown connection / sealed admission | Native `not_connected` / `closed` |
| Bridge admission / invalid deadline | Admission throws; invalid deadlines return native `invalid_command` |
| Native error / replies | Retains `arknet::redis::result`; error replies stay in ordered `replies`. True reports operation status, not success of every reply |

`pipeline(name, commands, timeout)` returns one native result containing ordered
replies. Its timeout covers pool acquisition and execution. `max_operations`
bounds bridge records and service operations retained until owner delivery;
`max_input_bytes` bounds service-retained command storage, including allocated
capacity. Input rejection reports native `queue_full`. Configure native pool,
waiter and response bounds separately in `options.pool`. These are per-connection
limits, not a whole-process memory limit.

Managed-group cancellation reaches the native service. Cancelled waiting acquires are unsent; after sending, `command_may_have_run == true` is possible. No write replay or synthetic success. Caller-owned native results, including partial replies, survive pool retirement.

| Update / shutdown | Contract |
| --- | --- |
| Delivery | Plugin runs `service.update()` then bridge dispatch; manager centrally polls the shared runtime and resumes coroutines on its owner. arknet owns native IO/workers |
| Drain | Keep updating while `stop()` returns false; accepted workflows may submit later commands. Readiness observes logical activity without sealing early |
| Native retirement | After tasks/delivery stacks return, host-frame `pre_shut` seals admission and calls `retire_native()`. False/exception retains resources and prevents `shut` release; `service.stop()` is not this barrier |
| Reentrancy | Recursive lifecycle teardown from a running task is unsupported; manager defers it to the host frame |

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
