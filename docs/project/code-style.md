# Code style

This convention applies to the ark reorganization and subsequent development. Public interfaces, implementation names and filenames consistently use `snake_case`.

## Naming

| Item | Convention | Examples |
| --- | --- | --- |
| Namespaces | Lowercase words separated by underscores | `ark`, `ark::detail` |
| Classes, structs and type aliases | `snake_case` | `plugin_manager`, `module`, `data_list` |
| Enumeration types and values | `snake_case` | `data_type::dt_int64` |
| Functions and methods | `snake_case` | `find_module()`, `start()`, `stop()` |
| Local variables and parameters | `snake_case` | `module_name`, `initial_buffer_size` |
| Private and protected members | `snake_case` with a trailing underscore | `module_name_`, `state_` |
| Public struct fields | `snake_case` | `session_id` |
| Compile-time constants | `snake_case` | `default_buffer_size` |
| Template parameters | `snake_case` | `module_type`, `buffer_size` |
| Preprocessor macros | `UPPER_SNAKE_CASE`, prefixed with `ARK_` | `ARK_EXPORT` |
| C++ filenames | `snake_case`; `.hpp` headers and `.cpp` implementations | `plugin_manager.hpp`, `plugin_manager.cpp` |

The public type form after migration is `ark::plugin_manager`. Name interfaces and implementations by responsibility; new interfaces do not use `AFI` or `AFC` type prefixes. Preserve identifiers belonging to dependencies.

## Formatting and include order

- Use four spaces, no tabs, and a 120-column limit.
- Group includes in this order: system libraries, third-party libraries, project headers. Separate groups with a blank line.
- Place a function's opening brace on the signature line. Expand short functions, including empty bodies.
- Use braces for conditional and loop bodies, including short branches.
- Leave blank lines between includes and code, namespaces and their definitions, and adjacent type or function definitions.
- `clang-format` handles formatting. Interface review and applicable static checks enforce naming.

This example illustrates naming and formatting:

```cpp
#include <cstddef>

namespace ark {

class session_counter
{
public:
    void add(std::size_t count) {
        count_ += count;
    }

    std::size_t count() const {
        return count_;
    }

private:
    std::size_t count_ = 0;
};

} // namespace ark
```

## Logging source locations

`ARK_FORMAT` and `ARK_FORMAT_FUNCTION` use standard C++20 `__VA_OPT__` for zero
or multiple formatting arguments, including dynamic format strings. They retain
`fmt::runtime` formatting and literal-brace handling. `ARK_FORMAT` remains an
expression; `ARK_FORMAT_FUNCTION` retains its `[function:line]` caller prefix
and historical trailing statement terminator. Existing call forms are unchanged.

`ARK_LOG_TRACE`, `ARK_LOG_DEBUG`, `ARK_LOG_INFO`, `ARK_LOG_WARN`, `ARK_LOG_ERROR`
and `ARK_LOG_CRITICAL` retain their format/argument call forms and log levels.
They use C++20 `std::source_location` at the macro caller and `__VA_OPT__` for
optional formatting arguments. Output keeps the `[basename:line]` prefix;
basename extraction recognizes both `/` and `\`, and the source function name
remains empty. Formatting, argument evaluation, message ownership and logger
configuration/lifecycle are unchanged.

MSVC needs its standard-conforming preprocessor (`/Zc:preprocessor`) for
`__VA_OPT__`. The public `ark::core` target propagates this option to consumers.
When compiling ark headers without that target, enable this option together with
C++20 mode.

**Source compatibility note:** ark no longer defines the implementation helpers
`SPDLOG_STRRCHR` and `SPDLOG_FILE_BASENAME`. Existing externally supplied macros
are left defined, but a custom `SPDLOG_FILE_BASENAME` no longer changes
`ARK_LOG_*` filenames. Code using those old helper definitions must provide its
own basename operation; use `std::string_view::find_last_of("/\\")` when both
separator kinds are needed. This does not require changing ordinary `ARK_LOG_*`
call sites.

## Implementation conventions

- Use C++20 and target-scoped dependencies. Public headers explicitly include the standard-library headers they need.
- Manage resources through RAII and explicit ownership. Document the lifetime of borrowed pointers, references and views.
- Use `override`, `final` and `const` to express interface constraints.
- Write English comments describing constraints, reasoning and non-obvious behavior. Document public API lifetime, threading and error contracts.
- Review formatting separately from behavior changes. Migrate a module together with its consumers, examples and tests.

## Local checks and generated code

Use clang-format 18 and the checked-in configuration:

```sh
./scripts/format.sh
./scripts/format.sh --check
```

For semantic naming checks, configure a compilation database before running
clang-tidy (the existing validation uses version 22):

```sh
cmake -S . -B style-build -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build style-build
./scripts/clang_tidy.sh style-build
./scripts/clang_tidy.sh style-build src/application/main.cpp
```

The default clang-tidy invocation checks source files present in that build's
compilation database; enable optional plugins to check their translation units.
Plugin entry symbols `DllEntryPlugin` and `DllExitPlugin` retain their required
spelling alongside third-party APIs and reflected strings.

The clang-tidy command needs the configured build's compilation database and a compatible compiler toolchain. Set `CLANG_FORMAT` or `CLANG_TIDY` when selecting a nondefault executable. Formatting covers tracked and new ark-owned C++ and Protobuf sources; vendored implementations and generated build outputs retain their own rules.

Protocol definitions use `snake_case` and the `ark::protobuf` C++ namespace, with `consul` and `osslog` retaining their existing packages. The generator's `.pb.h` suffix remains unchanged. Configuration keys, reflection strings, external JSON names and the C# generator's business-facing names keep their required spelling. See [Protobuf messages](../reference/protobuf.md) for binary and source compatibility boundaries.

Edit `.proto` sources and regenerate their C++ output through CMake. Generated
headers use paths such as `<ark/proto/common_messages.pb.h>`. The naming migration
preserves message field numbers, field types, cardinality, enum numeric values
and external JSON field names. Generated C++ names, descriptor names, `Any` type
URLs and JSON enum strings change; rebuild consumers and update code depending
on those names. `tests/core/protobuf_messages.cpp` retains golden-byte regressions
for binary payload compatibility.

The configuration generator follows the same C++ naming convention and formats
`meta_define.hpp` before copying it into `include/ark`. Configuration data,
reflection strings and C# output retain their existing names.

## C++20 compatibility migration

**Breaking source change:** ark no longer defines `ARK_CPP11_OR_GREATER`,
`ARK_CPP14_OR_GREATER`, `ARK_CPP17_OR_GREATER`, `ARK_NOEXCEPT`, `ARK_CONSTEXPR`,
`ARK_FINAL` or `MAKE_BUILDIN_TYPE`. The required language version remains C++20.

| Removed name | Migration |
| --- | --- |
| `ARK_CPP11_OR_GREATER`, `ARK_CPP14_OR_GREATER`, `ARK_CPP17_OR_GREATER` | Remove obsolete guards in ark consumers; use application-owned feature checks if supporting other language standards independently. |
| `ARK_NOEXCEPT` | Write `noexcept` directly. |
| `ARK_CONSTEXPR` | Write `constexpr` directly. |
| `ARK_FINAL` | Write `final` directly. |
| `MAKE_BUILDIN_TYPE(T)` | Keep any intended custom classification through an explicit `ark::is_buildin_type<T>` specialization declaring the existing `yes` and `no` enum values. |

The C++11 `std::make_unique` fallback is removed; include `<memory>` and use the
standard C++20 library. Platform export, assertion and ownership macros keep their
existing behavior.

`is_buildin_type` still recognizes exactly `uint16_t`, `uint32_t`, `uint64_t`,
`int16_t`, `int32_t` and `int64_t`, including native type aliases of those types.
It does not recognize all integral types or remove cv/reference qualifiers.
Its `yes`/`no` enum values and `check_buildin_type` signature, exception contract
and argument deduction remain unchanged: a const integer lvalue can deduce the
unqualified type through `const T&`, while explicitly checking a const-qualified
trait remains false. Constant types and values, including UID bit allocation and
network timeouts, remain unchanged.

## POD string iterator contract

`<ark/string_pod.hpp>` is supplied by `ark::core`. `pod_string_iterator::operator++(int)` returns the prior iterator **by value**; prefix `++it` still returns the advanced iterator by reference. Store the old position with `auto previous = it++;`, rather than binding a non-const lvalue reference. A saved iterator borrows its node; it does not copy the mapped value.

**Breaking source change; rebuild affected consumers and plugins:** update reference bindings or explicit function signatures that relied on the old postfix reference. Emitted template instantiations can use a different calling convention despite unchanged class fields; unchanged layout does not establish binary compatibility.

`get_data()` retains its declared `mapped_type*` return type and returns the address of the stored mapped value: `std::size_t*` for a scalar, or `T**` for a pointer mapping. Assigning through that address changes the stored value/pointer slot without transferring pointee ownership. The named `pod_string::get_data(key, out)` lookup is unchanged.

Check `it != values.end()` before incrementing or calling `get_key()` / `get_data()`. Empty ranges have `begin() == end()`; do not dereference or increment end. Advancing one iterator does not change a saved prior position, but removing its node, clearing or destroying its container ends that borrowed lifetime. The custom iterator and const-iteration APIs are otherwise unchanged. Core header checks instantiate scalar and pointer mappings; `check-core` covers saved postfix values, storage mutation, empty/end comparisons and the existing `array_map` lookup path.

## POD array storage and failure contract

`<ark/array_pod.hpp>` retains its `void` mutation APIs and inline/heap storage layout. Use it for trivial scalar or pointer elements with a positive inline capacity; it does not manage nontrivial element lifetimes. `push_back`, fill `resize` and `insert` snapshot an element argument before growing or shifting storage, so an argument such as `values[0]` keeps its original value. Growth can still invalidate borrowed element pointers and references.

Impossible element/byte capacities throw `std::length_error` before allocation; a null allocation result throws `std::bad_alloc`. Allocator exceptions propagate. Failed array growth or copy assignment preserves the existing storage, size and values, and failed copy construction leaves the source unchanged. Callers that handle allocation failure must catch exceptions rather than expect a status return; rebuild affected header consumers and plugins to use the checks. Public signatures and class fields are unchanged.

`array_map<std::string, T>` keeps borrowed `T*` values and never deletes pointees. `add_element` appends the pointer before inserting its name and rolls back the append if index allocation throws. Existing entries, lookup results and count remain consistent; spare pointer capacity or index bucket capacity may have grown. The default `pod_string_allocator` also converts null allocation results to `std::bad_alloc` so index failures can propagate. This insertion guarantee does not extend to custom string allocators or all other `pod_string` operations. Duplicate-name behavior is unchanged. Permanent Core tests cover scalar/pointer aliases, arithmetic limits, null/throwing allocators, copying and pointer/index failure recovery through `check-core`.

## Standard utility migration

**Breaking source change:** `ARK_TO_STRING`, `ARRAY_LENTGH`, `ARK_SRATIC_ASSERT`,
`ARRAY_CLEAR`, `MEMORY_CLEAR` and `MEMORY_CLEAR_POINTER` are no longer defined.

| Removed name | Migration |
| --- | --- |
| `ARK_TO_STRING(value)` | Include `<string>` and call `std::to_string(value)`. This is the same standard function previously used by the alias, including its C++20 floating-point formatting. |
| `ARRAY_LENTGH(array)` | Include `<iterator>` and use `std::size(array)` for bounded arrays. If code needs a type's first array extent, include `<type_traits>` and use `std::extent_v<T>`; `std::size` does not accept a raw pointer. |
| `ARK_SRATIC_ASSERT` | Write `static_assert` directly. |
| `ARRAY_CLEAR`, `MEMORY_CLEAR`, `MEMORY_CLEAR_POINTER` | These unused helpers are removed without a replacement alias. Choose value initialization, typed assignment or an explicit byte operation according to the object's storage contract; there is no universal safe object-clearing operation. `MEMORY_CLEAR_POINTER` also contained the invalid token `0xx`. |

The existing data and endpoint conversions still call `std::to_string` with the
same argument types. Numeric output and precision remain unchanged. Array-count
call sites keep their existing `int` variables, lookup order, enum mappings and
range handling. Remaining allocation, ownership, runtime-assertion, platform,
plugin-registration and byte-order macros retain their existing behavior.

## Sleep migration

**Breaking source change:** `ark/macros.hpp` no longer defines `ARK_SLEEP`. The
repository has no callers; its three platform definitions were unused. Existing
standard-library sleeps, scheduling and timeouts remain unchanged.

For a finite, nonnegative integer delay in milliseconds, include `<chrono>` and
`<thread>` and replace an external call with:

```cpp
std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
```

The argument expression is evaluated once. Check that its value fits
`std::chrono::milliseconds::rep`; no intermediate multiplication by 1000 is
needed. Review callers that depended on the former platform-specific integer
conversion or overflow, Windows `INFINITE`/zero-delay scheduling semantics, or
POSIX `usleep` return values and signal interruption. `sleep_for` returns `void`
and does not preserve those contracts.

See the [C++ draft's `sleep_for` contract](https://eel.is/c++draft/thread.thread.this),
[Windows `Sleep`](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-sleep)
and [Apple's `usleep` manual](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man3/usleep.3.html).
