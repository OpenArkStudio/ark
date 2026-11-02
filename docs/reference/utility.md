# Utility plugin

`ark_utility_plugin` provides GUID, timer, schedule and compression modules. Link a consumer to `ark::utility` for public headers and usage requirements; select `ark_utility_plugin` in the application manifest to load its implementation. A CMake target dependency alone does not load modules.

## Public interfaces

| Header | Interface and behavior |
| --- | --- |
| `<ark/utility/guid_module.hpp>` | `guid_module::create_guid()` and `parse_uid()`; existing GUID modes and bus-derived worker bits are unchanged |
| `<ark/utility/timer_module.hpp>` | Single/repeating timers driven by manager timestamps; only this Utility module registers for owner-thread updates |
| `<ark/utility/schedule_module.hpp>` | Interval, target-time and cron callbacks; preserves the existing scheduler worker configuration |
| `<ark/utility/compress_module.hpp>` | `compress()` / `decompress()` for zlib and Zstandard, including binary payloads |

Obtain interfaces during module initialization, after plugin registration:

```cpp
#include <ark/utility/guid_module.hpp>
#include <ark/utility/timer_module.hpp>

auto* guids = manager.find_module<ark::guid_module>();
auto* timers = manager.find_module<ark::timer_module>();
const auto entity = guids->create_guid();
```

Interfaces borrow the plugin; timer member callbacks also borrow receivers. Keep both alive through use/callback completion or removal. Advance the manager on its owner with millisecond timestamps; creating timers does not advance time. The background scheduler retains 16 callback workers and one dispatcher. Follow the [module lifecycle](../guides/modules.md) and keep callback receivers alive through scheduler drain.

Installation remains GUID → timer → schedule → compression, reversed on uninstall; timer `pre_shut()` closes its wheel. GUID layout, compression levels/algorithms and scheduling stay unchanged. `ark_utility_plugin` retains platform suffixes and Debug `_d`.

## Scheduler shutdown

The schedule module calls nonblocking `scheduler::close()` in `ready_to_shut()` after manager-task frames finish. The dispatcher discards every timer not yet dispatched, including future one-shots, and all further registrations are sealed, including callback-generated continuations. Already-dispatched callbacks and their captures retire before readiness; `pre_shut()` then joins workers. The scheduler destructor requests the same resource close. Discarded timers never invoke their application callbacks.

An explicit low-level `scheduler::stop_accepting()` retains its natural-drain contract: admitted future one-shot `in`/`at` schedules keep their due times, and submitted/queued callbacks finish. Future recurring occurrences retire; dispatched intervals do not rearm. An accepted callback on the same scheduler may register a one-shot continuation, including from capture destruction; this does not extend to other schedulers or recurring registration. Normal interval, target-time and cron behavior is unchanged. Calendar conversion uses caller-owned `std::tm`, existing local timezone and `mktime`/DST rules. A shared helper serializes conversion within that linked instance, not independent DSOs/copies or external timezone changes.

Readiness is a nonblocking snapshot covering queued timer retirement, registration capture transfers, dispatched callbacks and capture destruction. Captures are destroyed outside the mutex before readiness publication. Callback exceptions go to stderr; exception objects are destroyed before work ownership retires. Submission/recurrence failures return untransferred owners to existing storage, retain not-ready state and do not automatically recover.

Resource close does not wait for future timer deadlines. A dispatched callback or capture destructor that does not return can still reach an optional resource deadline; timeout does not destroy running work. Direct scheduler owners stop external producers before close/readiness/join. Owners that explicitly choose natural processing can use `stop_accepting()` and wait for its retained schedules before destruction. Never destroy a scheduler from its callback or have that callback wait for its owner to join it. Destruction can synchronously wait for dispatched work; threads are never detached or forcefully terminated. This is not a general DSO unload or hot-update barrier.

**Binary compatibility:** shutdown accounting affects the public scheduler helper layout; removing `module::stop_accepting()` also changes the module vtable. Rebuild Utility, the host and every consumer constructing/embedding the helper together.

## Timer ownership and allocation

The time wheel owns every allocated timer, including cancelled entries waiting in
wheel slots. Shutdown or destruction releases the pool and wheel vectors; repeated
`shut()` is safe. Use and close the wheel on its owner thread after callbacks have
finished. Slot precision remains 10 ms, with intervals truncated to whole slots;
callback ordering, repeat counts and cancellation timing are unchanged.

`ARK_NEW` and `ARK_DELETE` overrides remain the allocation boundary. A null
allocation retains the debug assertion; with assertions disabled, initialization
leaves the existing wheel intact and `add_timer()` returns `0` if the wheel or a
new timer is unavailable. The timer module reports failed wheel initialization
as `false` to the normal startup path. Standard container allocation exceptions
can still propagate. **Breaking source change:** `time_wheel_manager` explicitly rejects
copying and moving. Direct consumers must keep it at a stable location under one
owner, for example behind a `std::unique_ptr`, rather than copy or move the wheel.
The public low-level type's object layout also changes: rebuild the library and
all direct `time_wheel_manager` consumers together. The `timer_module` virtual
interface is unchanged.

## Compression buffers and concurrency

`<ark/compress/zlib.hpp>`, `<ark/compress/zstd.hpp>` and `<ark/compress/lz4.hpp>` own per-call heap buffers. Success appends output; rejected input leaves it unchanged. Supply readable input and synchronize shared output.

| Codec boundary | Contract |
| --- | --- |
| Compression | Worst-case output bound must be strictly below 1 MiB |
| Decompression | At most 1 MiB, including exactly 1 MiB; Zstandard rejects oversized, unknown-size or invalid frames before decoding |
| Failure | Invalid data returns `false`; LZ4 empty-block decompression remains `false` |

<Aside type="caution" title="Dictionary contexts still need serialization">

Normal zlib/LZ4/Zstandard calls share no writable output. Dictionary calls still mutate cached contexts: use separate `zstd_compress` instances per thread or serialize dictionary initialization/calls/moves/destruction. Moving ownership does not synchronize concurrent calls or establish plugin-unload safety.

</Aside>

`zstd_compress` owns its native contexts and dictionary pairs with RAII. Dictionary initialization publishes both handles together. A dictionary-creation failure leaves all registered keys unchanged; a map-allocation exception propagates with the same guarantee. Temporary handles and file buffers are released on both paths. Reinitializing an existing key still returns `true` after successful dictionary creation and keeps the original pair and compression level; the unused candidate is released. File-loading failures retain their existing process-exit behavior, and existing debug assertions remain.

**Breaking source and binary change:** `zstd_compress` is move-only and its class layout changes. Rebuild all code that constructs, embeds or passes it across binary boundaries. Replace copies with `std::move` when transferring ownership, or construct and initialize a separate instance when independent state is needed. Move construction and assignment transfer populated dictionaries and cached contexts; assignment releases the destination's previous resources. Moved-from instances can be initialized again. Compression formats, append behavior and size limits stay unchanged.

Move construction inherits the dictionary map's exception guarantee. On standard libraries that allocate during map moves, including Microsoft STL, it can throw `std::bad_alloc`; a failed allocation leaves the source dictionaries and contexts usable. Move assignment remains `noexcept` with the fixed standard allocator and comparator.

Enable `ARK_BUILD_COMPRESSION_TESTS=ON` and the vcpkg `compression-tests` feature
to run `cmake --build <build-dir> --target check-compression`. The target is also
part of `check-core` when enabled; Applications CI enables it explicitly. It
checks the mapped public headers, capacity boundaries, malformed frames, append
behavior and parallel round trips with separate dictionary instances. Ownership
tests check move-only type traits, populated moves, duplicate keys, resource
creation failures and dictionary insertion exceptions with balanced native-handle
and file-buffer counts. Failure injection runs in a separate executable using
the existing release-mode false-return paths. The move-construction allocation
failure case runs only when the codec's move-construction trait permits throwing;
it is skipped on libraries with nonthrowing map moves. LZ4 remains
a direct core helper; the Utility module still selects zlib or Zstandard.

## Source and build-tree SDK migration

Public headers now have their single source in `src/plugins/utility/`. CMake copies seven explicitly listed public headers into the build tree's `src/plugins/sdk-include/ark/utility/`: the four module interfaces plus `scheduler.hpp`, `ctpl.h` and `ctpl_stl.h`. Consume `ark::utility` to retain all existing `<ark/utility/...>` public/helper include paths, including with runtime plugins disabled. Do not add the private plugin directory to consumer include paths. The upstream thread-pool headers retain their original bytes and formatting rules. For installed consumption, use the [CMake package](../get-started/installation.md#install-and-consume-the-cmake-package) with `find_package(ark CONFIG REQUIRED COMPONENTS utility)` and the same `ark::utility` target. Installed public headers retain `<ark/utility/...>` paths; the runtime plugin still needs to be selected in the application manifest.

**Breaking source change:** `ark/utility/{guid,timer,schedule,compress}_module_impl.hpp` and `ark/utility/utility_plugin.hpp` are removed from the SDK. Implementations are defined in their cpp files; the plugin type and interface-returning factories are private. Replace direct implementation construction with public interface lookup or an application-owned implementation. Handwritten `-I <ark-source>/include` consumers must use target-provided include roots. Rebuild the plugin and consumers together; unchanged public virtual functions and exported entry signatures do not establish compatibility with old implementation-class binaries.

## Core string patterns

`<ark/string_utils.hpp>` provides `string_utils::split()` and `replace()` through `ark::core`. An empty string delimiter appends the unchanged, nonempty input as one element; empty input appends nothing. An empty replacement pattern returns the original input unchanged, including embedded null bytes in a `std::string`. This rule applies with both unlimited and explicit operation limits, so an empty pattern cannot cause an endless loop.

Nonempty patterns retain their existing behavior: split appends to the output vector, preserves leading/interior empty elements, drops a trailing empty element and leaves the remaining suffix once the split limit is reached. The limit is compared with the total output size, so use an empty output vector to count only newly produced elements. Replacement applies nonoverlapping matches from left to right; limit `0` means unlimited. C-string arguments must be valid, null-terminated strings; string-delimiter overloads retain C-string matching semantics. This change does not add general binary substring matching.


## Core UID bit allocation

`<ark/uid_generator.hpp>` retains the default signed 64-bit UID layout: one reserved sign bit, 29 timestamp bits, 24 worker bits and 10 sequence bits. Valid field values produce the same IDs. `bits_allocator` construction and `alloc()` support C++20 constant evaluation.

Widths must be nonnegative and total 63 payload bits; invalid layouts throw `std::invalid_argument` before any shift, in both Debug and Release. Each allocation field must fit its own width and be nonnegative; otherwise `alloc()` throws `std::out_of_range`, preventing worker or sequence bits from spilling into an adjacent field. Zero-width fields accept only zero. Runtime generation still uses the existing epoch, clock checks and sequence rollover; this does not change the UID format or provide cross-process worker-ID coordination.

## Core random helper

`<ark/random/random_generator.hpp>` provides `ark::random_generator` through `ark::core`; it does not require loading the Utility plugin. The signed `random(int, int)` overload accepts any pair of `int` endpoints, including `INT_MIN` and `INT_MAX`, without overflowing the range calculation. Different endpoints produce a value in `[lower, upper)` after ordering the endpoints. Equal endpoints return that value without advancing the generator.

Range selection retains the existing modulo mapping, seed sequence and draw count. Each call with different endpoints consumes one draw; reversing the endpoints produces the same sequence from the same seed. The unsigned overloads retain their existing behavior.

## Core Murmur hashes

`<ark/hash/murmur_hash.hpp>` provides MurmurHash2 and the x86-32, x86-128 and
x64-128 MurmurHash3 variants through `ark::core`. Supply a nonnegative `int`
length, a readable input buffer (also for an empty input), and writable output
storage of 4 bytes for 32-bit variants or 16 bytes for 128-bit variants. Input
and output may have any byte alignment; word loads and stores use `std::memcpy`.
MurmurHash3 uses C++20 `std::rotl` with the original rotation counts. Seeds,
tail mixing, result word order and the existing public signatures are unchanged;
`murmur_hash3_x86_128` remains an instance method, and the other variants remain
static. MurmurHash2 retains its fixed seed zero.

The Murmur header itself uses only standard C++20 headers. It does not include
`ark/macros.hpp` or `ark/platform.hpp`; callers using their declarations or macros
must include those headers explicitly. Public macro definitions are unchanged.
This header independence does not change `ark::core` or its CMake package's
dependency requirements. `check-murmur-hash-header` compiles the logical public
header with an isolated include directory and no SDK target dependencies; it is
also included in `check-core`. See [core regressions](../../tests/core/README.md)
for the standalone check commands.

### Digest compatibility and legacy access

**Breaking change:** default MurmurHash3 now reads full blocks as little endian
and writes canonical little-endian digest bytes on every supported platform.
This retains existing little-endian GCC/Clang results and changes the former
ordinary MSVC full-block policy. The former platform detection did not select
MSVC's `_M_IX86`, `_M_X64` or `_M_ARM64` macros
([Microsoft predefined macros](https://learn.microsoft.com/en-us/cpp/preprocessor/predefined-macros));
it fell through to an unconditional byte swap on a little-endian host.
The public `BYTESWAP32` and `BYTESWAP64` macros remain available, but no longer
control these hash methods. Callers must rebuild to use the new header behavior.

| Method family | Default result | Previous ordinary MSVC result |
| --- | --- | --- |
| MurmurHash3 x86-32 | `murmur_hash3_x86_32` | `murmur_hash3_x86_32_legacy_msvc` |
| MurmurHash3 x86-128 | `murmur_hash3_x86_128` (instance method) | `murmur_hash3_x86_128_legacy_msvc` (instance method) |
| MurmurHash3 x64-128 | `murmur_hash3_x64_128` | `murmur_hash3_x64_128_legacy_msvc` |
| MurmurHash2 | `murmur_hash2`; unchanged on little-endian platforms | Same method; no separate legacy route is needed |

The three `_legacy_msvc` methods reproduce the old MSVC result on any supported
platform: full blocks are read as big endian, tails are assembled as little
endian, and output words are serialized as little endian. Pass the original
variant, exact input bytes, length and seed. The policies agree for inputs shorter
than a full block (4 bytes for x86-32, 16 bytes for either 128-bit variant).
For bytes `00 01 02 03` and seed zero, x86-32 produces numeric word `0xf4c0ec39`
by default and `0x2cb313f3` through the legacy entry. A previous big-endian
consumer must also account for the change from native-endian output to canonical
little-endian bytes; `_legacy_msvc` specifically reproduces old MSVC behavior.

### Migrate stored keys and routing

The built-in `ark::consistent_hash` currently uses MurmurHash2, whose results remain unchanged on little-endian platforms. The routing guidance below applies to consumers that derive their ring or shard keys from the affected MurmurHash3 policy.

Identify each dataset's hash variant, seed, input encoding and previous compiler
policy before upgrading. Keep that policy with the data or in an explicit schema
version. A digest alone cannot reconstruct the original key. IDs, shard mappings,
cache keys and persisted indexes derived from old MSVC hashes require an explicit
migration; replacing the function and silently recomputing routing is unsafe.

For lookup keys, use a staged **dual-read / new-write** rollout: write new records
with the standard policy and a version tag, read that key first, and fall back to
the matching `_legacy_msvc` key for older records. Compare the original logical
key when resolving a hash hit or collision. Backfill or move old entries using
transactional/atomic application rules, define duplicate handling, and retain
legacy reads until inventory and rollback requirements permit their removal.
Old readers must understand new records before new-only writes begin. The library
does not migrate application records automatically.

For consistent hashing or shard ownership, keep all routers on one explicit ring
version while planning the transition. Compute the new ring from authoritative
node identities, compare placement, copy/rebalance affected data, and coordinate
the routing epoch and ownership cutover across participants. Keep the legacy
ring and legacy hash entry until that transfer is accepted. Mixed old/new rings
during an ordinary rolling binary update can route the same key differently;
a dual lookup alone does not migrate ownership.

`check-core` includes the [upstream SMHasher verification values](https://github.com/aappleby/smhasher/blob/master/src/main.cpp#L67-L70)
with its [prefix/seed procedure](https://github.com/aappleby/smhasher/blob/master/src/KeysetTest.cpp#L11-L38),
plus separate scalar-reference fixtures for the legacy MSVC policy. Cases cover
all seven public methods, every MurmurHash3 tail length, full-width seeds,
high-bit input bytes and rotation paths, MurmurHash2 boundary vectors, and
exact-size byte buffers at offsets 0 through 7. AddressSanitizer can detect
allocation-end overreads; UndefinedBehaviorSanitizer can detect invalid alignment.
The fixtures establish hash behavior; application data migration needs its own
acceptance evidence.

## Verification

`check-typed-examples` loads the actual Utility DSO with `timers`, `module_lifecycle` and `entity_operations`. It verifies all four interface RTTI keys and manager ownership, GUID generation/parsing, zlib/Zstandard binary round trips, a timer triggered by owner-thread time advancement, and normal/idempotent shutdown. `check-app-presets` exercises the shipped launcher preset; `check-core` includes both when applications, examples, Kernel and Utility are enabled. The independent `tests/consumer` project's `check-sdk` compiles public/helper paths with runtime plugins disabled and rejects visible implementation or assembly headers. These are correctness checks, not performance or arbitrary callback-lifetime acceptance.

`check-core` also includes standalone random-header compilation and deterministic random-range regressions for extreme, mixed-sign, reversed and equal endpoints. See [core regressions](../../tests/core/README.md) for the build commands.

`check-timer`, included in `check-core`, verifies wheel order, repeats, cancellation,
resource cleanup and allocation-failure recovery. Run it separately with
`cmake --build <build-dir> --target check-timer`. It also runs an isolated
`NDEBUG` build of the production timer module to check allocation-failure reporting
through its public interface.

`check-scheduler-drain`, included in `check-core`, runs the production scheduler
and schedule-module implementation with controlled callback/capture barriers.
It checks accepted and queued work, recurring retirement, registration/stop races,
same-instance continuations for explicit natural drain, resource-close rejection of callback continuations, callback reentry, capture and exception
destruction, a module-owned 24-hour timer discarded without running or retaining its capture, owner-driven completion and sticky manager
timeout without cleanup. Run it with
`cmake --build <build-dir> --target check-scheduler-drain`. These checks do not
establish general DSO unload, hosted platform or production capacity acceptance.
The target also runs isolated allocation-failure tests against the actual
`dispatch()`/`manage_tasks()` helpers after quiescing their initial dispatcher.
They inject persistent allocation rejection or recurrence failure, verify retained
owners and closed readiness, then use explicit test-only recovery to terminate
the fixture. This does not establish production recovery from dispatch failure.
