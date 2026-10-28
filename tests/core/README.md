# Core regressions

[简体中文](README_CN.md)

The doctest executable covers string growth and equality, hash keys, object ownership,
constant iteration, conversion templates, typed module lookup and member callbacks.
Initialize the pinned dependencies as described in [network builds](../../docs/networking.md).

```sh
cmake -S . -B core-build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DARK_BUILD_APPLICATIONS=OFF -DARK_BUILD_TESTS=OFF -DARK_BUILD_CORE_TESTS=ON
cmake --build core-build --target check-core
```

Run `core-build/tests/core/ark_core_tests --help` for doctest filters. `check-network`
also runs these tests when `ARK_BUILD_CORE_TESTS=ON`, including in the existing CI
matrix. No database service is required.

String-pattern regressions exercise empty delimiters and replacement patterns with unlimited and explicit limits, empty input, existing output elements and embedded nulls. They also preserve nonempty delimiter, suffix, replacement-count and deletion behavior. Run `ark_core_tests --test-case="*String utilit*"` to select them. See [string-pattern semantics](../../docs/reference/utility.md#core-string-patterns).

The public header checks include `<ark/error_code.hpp>` as the first include and
include it twice. Compile-time checks preserve its unsigned 32-bit underlying type
and every declared error value.


Murmur regressions include independent upstream SMHasher verification constants,
legacy swapped-block fixtures, all tail lengths, full-width seeds and the original
rotation paths. They also check MurmurHash2 golden words, exact-size input
allocations and input/output offsets 0 through 7 with output guard bytes.
Run the core executable with `--test-case="*Murmur*"` to select these cases.
See [Murmur compatibility](../../docs/reference/utility.md#core-murmur-hashes)
for canonical little-endian output and the explicit legacy MSVC migration path. AddressSanitizer and
UndefinedBehaviorSanitizer builds exercise overread and alignment checks.

The existing mapped public-header check remains. An additional compile-only check
stages just `<ark/hash/murmur_hash.hpp>` and uses C++20 without project, platform,
spdlog or Boost include paths or SDK target dependencies. It is part of
`check-core` and can also run without configuring the full dependency graph:

```sh
cmake -S tests/core/hash_header -B hash-header-build -G Ninja
cmake --build hash-header-build --target check-murmur-hash-header
```

Header independence does not remove the dependencies required by `ark::core` or
its CMake package.

With `ARK_BUILD_PROTO=ON`, additional cases verify Protobuf parse failures, actor
identity, binary messages, legacy wire payloads and external JSON field names.
They also exercise the new `ark::protobuf` descriptor and enum names.
Initialize Protobuf as described for application builds.

The C++20 language regressions check the exact six built-in integer classifications, qualifier and reference handling, constant values, exception contracts and fixed UID allocation outputs. The standalone array-map header check also rejects reintroduced legacy language macros.

UID regressions also check compile-time allocation, invalid/extreme widths, field overflow and the three 63/0/0 width permutations. `ark_core_tests --test-case="UID*"` selects them. Invalid layouts throw `std::invalid_argument`; invalid field values throw `std::out_of_range`, independently of assertion settings. See [UID bit allocation](../../docs/reference/utility.md#core-uid-bit-allocation).

The random-generator regressions include the public header first and twice. They preserve fixed raw/signed/unsigned sequences, final seeds and draw counts, and check half-open signed ranges at `INT_MIN`/`INT_MAX`, mixed-sign and adjacent endpoints, reversed endpoints and equal endpoints. Equal endpoints return their value without consuming a draw. These cases are part of `ark_core_tests` and `check-core`; a build with `-fsanitize=undefined -fno-sanitize-recover=all` also checks the range arithmetic with UndefinedBehaviorSanitizer. See [random-helper behavior](../../docs/reference/utility.md#core-random-helper).

With `ARK_BUILD_CONFIG=ON` (the default for core tests),
`check-application-manifest` checks TOML/XML equivalence, defaults, strict fields,
invalid input, diagnostics and owning results. It is also part of `check-core`.
See [application manifests](../../docs/service-manifests.md).

When the application is also built, `check-app-manifest-failures` runs the real
launcher against missing, malformed and mistyped manifests. It requires normal
exit code 1 with a manifest diagnostic, including on Windows, and is included in
`check-core`.

`check-app-startup-failures` uses the real launcher and a test DSO for all four startup phases, each returning false or throwing, with both TOML and XML manifests. It also checks a non-standard exception, a throwing plugin entrypoint, a missing library and a loadable DSO with no `DllEntryPlugin`. Before each controlled module failure or entrypoint exception, the registered test module starts and retains a real owned loopback TCP listener on an ephemeral port. The missing-entry fixture confirms native library initialization occurred before failure. All 24 children must report one exact phase/module-or-plugin diagnostic, preserve available exception details, and exit normally with code 1 without later startup calls, update, normal shutdown or uninstall. Core tests check retained failure context, unchanged exception propagation, first failure across multiple modules and owner-thread access. The listener makes ordinary static cleanup block if failed-start module ownership is retained, so the exit check covers a real active IO pool. These tests establish failure reporting and process termination, not partial-start resource cleanup or same-process recovery.

`check-kernel-initialization` loads real metadata and static data from
`resource/meta/`. It checks four kernel/config initialization orders with both
resource-relative paths and legacy paths beginning with `/`, for eight combinations.

The same target runs `kernel_delayed_sync.cpp`: real container creation, removal
and destruction pass through delayed collection, Kernel update, Protobuf serialization
and the existing mask delivery callback. It covers full payloads, entity references,
empty-slot/destruction markers, self-only masks and empty/scalar/table/mixed controls.
A narrow test-only friend observes that callback; no transport or client is supplied.

`check-logging` runs the independent C++ logging path regressions and is included
in `check-core`. It checks relative and nested filenames, extensions and hidden
files, dotted directory names, both hourly sink factories, and the configured
logger directory. An empty `output_path` uses the working directory. Hourly logs
retain the `directory/YYYYMMDD/basename_hh.ext` layout; only the filename's final
extension moves after the hour suffix. Files without an extension and names ending
in a dot keep that basename. All test writes use temporary directories.

```sh
cmake --build core-build --target check-logging
```

## Compression helpers

`ARK_BUILD_COMPRESSION_TESTS=ON` adds `check-compression` to `check-core` and
also supports running it independently. Install the vcpkg `compression-tests`
feature for zlib, Zstandard and LZ4; the option defaults to `OFF` so core-only
builds keep their existing dependency requirements. Applications CI explicitly
enables it. Tests compile each mapped `<ark/compress/...>` header independently
and cover binary/empty inputs, append semantics, strict compression bounds,
exactly 1 MiB decompression, oversized/unknown/corrupt frames, integer-size
rejection and concurrent round trips. Dictionary workers use separate instances.
See [Utility compression](../../docs/reference/utility.md#compression-buffers-and-concurrency)
for the caller-owned output and dictionary-context synchronization requirements.

```sh
cmake --build core-build --target check-compression --parallel 2
```

## Native library close results

`check-native-unload` is included in `check-core` and reuses the existing OSS SDK entry probe without an OSS service or runtime application build. Deterministic tests inject a failed native result using valid loaded handles through private per-call adapters; no production global hook or public virtual mock API is added. They verify the platform's success convention, retained failed handles/wrappers, stopping before later closes, exactly-once plugin exit and sticky manager failure. Successful close clears the handle, and repeated empty/successful unload does not call the OS again. A separate normal path runs the real plugin entry/exit and native close APIs. These checks do not establish OS image removal, real database acceptance or arbitrary hot-update safety.

```sh
cmake --build core-build --target check-native-unload --parallel 1
```

## Public task interface

`check-task-sdk` is included in `check-core`. It compiles `<ark/task.hpp>`
without an earlier include and runs an ordinary `ark::core` consumer. Cases
cover typed owning values, stopped admission, explicit cancellation and owner
thread checks. The controlled child checks require exit 77 for pending-task and
wrong-thread destruction; they do not abandon suspended frames in the test owner.
Private database task tests retain forwarding aliases to this same public class.
See [Coroutine tasks](../../docs/guides/tasks.md) for the independently runnable
`ark_task_example` and explicit lifetime requirements.

## Shared business runtime

`check-business-runtime`, included in `check-core`, compiles a standalone
`<ark/plugin_manager.hpp>` consumer and checks one shared executor, the total
handler budget, module completion publication before polling, sequential awaits
after admission stops, late results, owner/reentrancy checks, pending-module
unregister, stable group references and failure diagnostics. The real unregister
macro must throw before deleting a pending task's module. Four controlled child
processes cover pending, wrong-thread, update-stack and final-completion-stack
destruction. These tests require no database service.

```sh
cmake --build core-build --target check-business-runtime
```

The manager tests establish managed business-task drain. The launcher fixture below also checks one controlled native worker retirement barrier; neither establishes generic dynamic-library unload safety. See [module task integration](../../docs/guides/tasks.md#shared-business-executor-for-modules).

When applications and configuration are built, `check-app-business-runtime` loads a test-only native plugin in the real launcher. POSIX cases request SIGTERM; Windows supplies `exit` to the real command thread. In the managed-task cases, the accepted task receives two controlled private-bridge results during draining, rejects a new root task with spare group capacity, and finishes its callback before owner-frame stop, native worker exit/join and shutdown/uninstall. Default unlimited and positive-deadline successful drains both exit 0. Separate false/throwing-hook, standard/non-standard throwing-update, stalled-task and delayed task/native-worker children must diagnose failure once, retain their module and remain alive until the owned test observation timeout. Throwing-update traces require an observed throw followed by a resumed owner update and accepted completion; Windows must not route these handled C++ exceptions into the native crash dialog. The native-readiness deadline case starts with an empty managed task group and a held native worker, so coroutine scheduling cannot consume its 50 ms deadline; its trace and diagnostic require zero pending tasks and the readiness phase. Late completion after the configured application deadline must not resume unloading. Malformed, negative and overflowing CLI deadlines are rejected before manifest loading. The application deadline is cooperative and cannot interrupt a blocking user update/readiness/shutdown call. The owned test observation timeout terminates only the test child and is not a successful shutdown or a production policy. No real database is involved.

With applications, examples, Kernel and Utility enabled, `check-typed-examples` loads the actual Utility DSO and verifies all four interfaces, interface RTTI keys, GUID creation/parsing, zlib/Zstandard binary round trips, a timer advanced by manager timestamps, and normal/idempotent shutdown. `check-app-presets` also loads the shipped Timers runtime; both are dependencies of `check-core`. The task/scheduler include-order probes link `ark::utility` for the mapped scheduler header, while the separate public task-only checks retain `ark::core` alone.

With Net, Bus, Utility and Protobuf enabled, `check-net-plugin` loads actual DSOs and checks both Net interfaces and RTTI keys, manager-thread TCP/HTTP callbacks, repeated shutdown and listener release. It is also a `check-core` dependency and requires no external service. The independent `tests/consumer/net` project links only `ark::net` and compiles every retained public header separately.

With Bus, Net, Utility and Protobuf enabled, `check-bus-plugin` loads actual DSOs and checks Bus/message interface keys, configured queries, routed Protobuf headers/body, transport failure propagation and repeated normal shutdown. `check-core` includes this target. The independent `tests/consumer/bus` project links only `ark::bus` and compiles both public module headers separately. Existing parser/configuration cases use an interface factory while retaining their assertions, and still support Protobuf-disabled builds.

On POSIX hosts with applications, Bus, Net, Utility and Kernel enabled, `check-server-startup-game` launches the actual Utility/Bus/Net/Kernel/Game DSOs with a private `Player` JSON schema and Bus identity `1.1.31.1`. Its healthy case requires a real TCP connection, cooperative signal shutdown, peer closure, listener refusal and exit code 0. Its occupied-endpoint case requires a complete Game/`post_init` startup failure diagnostic and exit code 1. The cases join `check-server-startup`, `check-server-plugins` and `check-core` when the Kernel target is present. They retain the existing bounded waits and own only their child process and loopback endpoints. They do not check Game callback registration, entities, gameplay, shipped presets, discovery or production readiness. Windows retains the separate construction tests. See [server architecture](../../docs/concepts/server-architecture.md#role-startup-errors).

With the Game, Kernel, Bus and Utility targets enabled, `check-game-initialization` runs the production Game factory/assembly and real Kernel, Metadata, Map, Config, Guid and Bus implementations. It calls Game `init()` before Kernel `init()`, then checks both Game/Kernel `post_init()` orders and observes the actual Player class callback through metadata event dispatch. A separate valid schema without Player must fail before listener creation. Only listener creation is controlled; no Kernel interface is replaced. These cases are included in `check-kernel-initialization`, `check-server-plugins` and `check-core`. Actual socket startup/shutdown remains covered by the POSIX DSO cases above; gameplay and partial-start rollback remain outside these tests.

The same `check-typed-examples` target also loads the entity operations DSO with its shipped manifest and example metadata. It verifies four Player entities, original field/custom values and item rows, a bounded startup with no timed-loop output, the compile-time module/update contract, bag callback dispatch, four destruction events, updates and normal/idempotent shutdown. The explicit `check-entity-benchmark-smoke` target is separate and optional; it never becomes a dependency of `check-core`.

With `ARK_BUILD_PLUGIN_HTTP=ON`, `check-http-plugin` loads the actual HTTP DSO and sends real loopback requests, including controlled active-handler retirement during owner-thread shutdown and endpoint reuse. It is a `check-core` dependency when the optional HTTP target exists. The independent `tests/consumer/http` project links only `ark::http`, with applications and Protobuf disabled.

Applications CI explicitly enables `ARK_BUILD_PLUGIN_HTTP=ON` and runs `check-core`, so its existing source and full matrices include the real HTTP DSO fixture. Local HTTP-enabled native validation has passed; hosted full-matrix acceptance for this change remains pending. The permanent CI policy test guards this configuration-to-DSO dependency chain.

Kernel maps its 38 existing headers from `src/plugins/kernel/` through `ark::kernel`. `check-core` retains data/node regressions; `check-kernel-initialization` retains initialization/configuration-copy assertions. The independent `tests/consumer/kernel` project consumes 38/35 headers with Protobuf enabled/disabled; `container_meta.hpp` retains its existing `ark/define.hpp` and `ark/enum.hpp` include prerequisites. See [Kernel reference](../../docs/reference/kernel.md).

The registered Net HTTP client now comes from a private interface factory. `check-net-plugin` additionally checks future completion without manager updates and pending-request cancellation through `pre_shut()` while the DSO remains loaded. It consumes those futures before normal plugin exit; no arbitrary post-unload future lifetime is established. `check-network` retains request/timeout/callback/shutdown coverage through the same private factory and public client interface.

`ARK_BUILD_HTTP_EXAMPLE_TESTS` defaults to `ON` in core-test configurations with
Boost.Asio and all three API targets (`ark::http`, `ark::net`, `ark::etcd_client`),
and to `OFF` otherwise. Explicit `OFF` is respected. When selected, it adds
`check-http-examples` to `check-core`, compiling the actual legacy HTTP example
modules and their shared-library entries. Existing Applications CI exercises this
dependency; the default application-example selection remains unchanged.
The fixture checks interface factories/RTTI/configuration, original POST/lease
arguments and timeout defaults, owner-update callback delivery, listener arguments,
lease destruction and repeated stop/reload using isolated public service doubles.
No external HTTP/etcd request is made. See [HTTP examples](../../docs/reference/http.md#legacy-http-example-construction)
for the preserved fixed demonstration endpoints and the real-wire coverage boundary.

With Protobuf and the Kernel/Utility runtime targets enabled, `check-kernel-plugin` loads their actual DSOs through an independent public-interface runner. It checks the four original interface keys and manager pointers, metadata/static record loading, map/entity registration and callbacks, manager updates, shutdown destruction and repeated stop. It is part of `check-core`, alongside the existing direct initialization/entity-default tests; no post-unload registry inspection or arbitrary hot-update safety is claimed. The Kernel public-header consumer checks all retained types and their typed bases/update declarations.

The XXTEA cases cover exact-size short-key allocations, 16-byte and embedded-zero
keys, binary round trips and output appending. See the [key contract](../../docs/reference/xxtea.md).

With the optional Kafka runtime enabled (`ARK_BUILD_APPLICATIONS=ON`, `ARK_BUILD_PROTO=ON`,
`ARK_BUILD_CORE_TESTS=ON`, `ARK_BUILD_PLUGIN_KAFKA=ON`) and its existing driver dependencies,
`check-core` includes `check-kafka-plugin` and `check-kafka-construction`. The focused checks
compile the real factory/assembly, verify interface RTTI metadata and enabled updates, run
production entry/exit code with a controlled factory, and load the actual production DSO to
inspect exports. They create no producer or broker connection. Kafka stays off by default;
disabled-plugin core builds do not discover its dependencies. See the
[construction migration](../../docs/reference/directory-structure.md#typed-kafka-construction)
for the public-layout rebuild requirement and service-validation limits.
