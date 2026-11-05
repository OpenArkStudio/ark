# Entity replication protocol

`<ark/kernel/nested_wire.hpp>` converts owning [nested publications](kernel.md#nested-baselines-and-deltas)
to and from the canonical [`entity_sync.proto`](../../src/proto/proto/entity_sync.proto).
Enable `ARK_BUILD_PROTO=ON` and link `ark::kernel` and `ark::proto`.
Generated C++ messages use `ark::protobuf::entity_sync`; generated C# messages use
`Ark.Client.Protocol.EntitySync`.

<Aside type="note" title="Codec and client acceptance">
The codec validates conversion against an application binding. Using it does not
by itself verify Godot transport, room business rules, rendering or a running
multi-client session. The application supplies these integrations and their checks.
</Aside>

## Binding before state

The application supplies a fixed `nested_stream_binding` before any remote
baseline: Schema ID, root GUID/incarnation, stream generation and an authorized
`nested_entity_layout`. Layouts include the field types of empty tables and the
individual child GUIDs allowed at each container. State messages never define or
expand this binding. Session authentication and projection authorization remain
application responsibilities.

```cpp
#include <ark/kernel/nested_wire.hpp>

using namespace ark::entity_sync;

// trusted and initial come from the application's authorized capture path.
nested_wire::limits budget;
nested_publisher sender(trusted, initial, budget.stream);
nested_replica receiver(trusted, budget.stream);

std::string body = nested_wire::encode(*sender.baseline(), trusted, budget);
auto baseline = nested_wire::decode_baseline(body, trusted, budget);
auto result = receiver.apply(baseline);
```

| API in `ark::entity_sync::nested_wire` | Result |
| --- | --- |
| `encode(native, proto_output, trusted, limits)` | Baseline/delta overloads validate and stage a generated message, then swap it into the output |
| `decode(proto_input, trusted, limits)` | Baseline/delta overloads return a new owning native publication |
| `encode(native, trusted, limits)` | Returns the Protobuf body as `std::string`; adds no transport header |
| `decode_baseline(bytes, trusted, limits)` / `decode_delta(bytes, trusted, limits)` | Parse a bounded `std::string_view` body and validate the resulting publication |

The `limits` argument is optional. The caller chooses baseline versus delta from
its transport contract. The codec does not authenticate a session, manage a sender
queue or apply a publication. A returned publication acknowledges no delivery.

## Values and structure

`scalar_field.value` is an explicit oneof. Its selected alternative carries zero
and false values without losing presence. C++ variant indices and
`nested_scalar_type` ordinals are not a wire ABI.

| Native value | Protobuf alternative | Constraint |
| --- | --- | --- |
| `bool` | `bool_value` | Explicit false is a present value |
| `int32_t` / `uint32_t` | `int32_value` (`sint32`) / `uint32_value` | Preserve signedness and width |
| `int64_t` / `uint64_t` | `int64_value` (`sint64`) / `uint64_value` | Preserve the full native range |
| `float` / `double` | `float_value` / `double_value` | Finite values only |
| `std::string` | `string_value` (`bytes`) | Raw bytes, including NUL and non-UTF-8; presentation validates its own text rules |
| `guid_value` | `guid_value` (`sint64`) | Signed 64-bit wire range and native `guid_t` range must both fit |

Entity identities also use `sint64`; they must be nonzero. Scalar GUID values may
be zero. Default native GUIDs are signed 64-bit; 32-bit builds reject narrowing
on decode, and wider builds reject values that cannot fit the wire on encode.

| Publication | Contents |
| --- | --- |
| `nested_baseline` | Generation, nonzero revision and complete `nested_snapshot` |
| `nested_snapshot` | Schema ID and root Entity snapshot |
| Entity snapshot | GUID/incarnation, class name, complete selected fields/tables/containers; selected empty tables and containers remain present |
| Table snapshot | Member index, row class and rows keyed by positive row number, each with incarnation and complete selected columns |
| `nested_delta` | Schema ID, generation, required revision, next revision, root patch and optional root-removal flag |
| Entity patch | GUID/incarnation and changed scalar, table-row and container records |

Indexed collections use repeated records. Decoders reject duplicate field/table/
container indices, row numbers and slots before installing any state. Indices are
class-local; table columns resolve in the row class. Complete roots reject repeated
Entity GUIDs, shared children and cycles. Unknown Protobuf fields may be ignored
within the same byte/recursion budgets; they grant no authorization.

## Row and container changes

| `row_delta` incarnations | Meaning |
| --- | --- |
| `required_incarnation == 0`, `incarnation > 0` | Add an absent row with every selected column |
| `required_incarnation > 0`, `incarnation == 0` | Remove the matching row; fields must be empty |
| Equal nonzero incarnations | Patch a nonempty subset of selected columns |
| New incarnation greater than the required nonzero incarnation | Replace the matching row with every selected column |

Row identity includes its owner lifetime, table index, row number and incarnation.
Deleting and reusing a row number, even with identical values between flushes,
requires a new incarnation. The replica checks required identities and remembers
each table's incarnation watermark; retired row lifetimes cannot be reintroduced.

`container_delta.update` selects exactly one representation:

| Alternative | Meaning |
| --- | --- |
| `replacement` | Complete final authorized slot membership with owning child snapshots; an empty list clears the container |
| `patch` | Nonempty changed-slot list with recursive patches; each slot's existing child GUID/incarnation must match |

The publisher chooses replacement whenever a slot, child GUID or incarnation
changes. Otherwise it recursively patches child values. Slot moves/swaps and
changes across multiple containers commit together within the root. Detaching a
child removes a relationship; it does not declare the source Entity destroyed.

## Commit and recovery

The codec validates binding, envelope, types, structure and publication budgets.
It cannot decide whether a delta fits a receiver's current revision, reconstructed
state or lifetime history; pass the result to `nested_replica::apply()`.

| Receiver condition | Result |
| --- | --- |
| First complete matching baseline | Install once |
| New delta with `required_revision == current_revision` and `revision == required_revision + 1` | Validate a private complete candidate and its history, then commit the entire root once |
| Structurally valid delta at or below the current revision | `duplicate`; state never moves backwards |
| Older baseline / equal-revision identical active baseline | `duplicate`; a newer baseline is not an in-place reset |
| Gap, mismatched lifetime/binding or illegal state transition | `rejected`; hide state and invalidate that replica permanently |
| Ordered root tombstone | Remove state while retaining valid duplicate handling; later traffic cannot revive the root |

The application closes an affected stream on malformed input, overflow, encoding
failure or admission/send rejection. Recovery uses a new connection, independently
trusted binding and complete baseline. Fixed projections do not support an in-place
generation reset or permission expansion. Atomicity covers one selected root;
there is no cross-root transaction, ACK or replay log.

## Independent limits and failures

| Budget | Default / scope |
| --- | --- |
| `limits.stream.snapshot` | Entity depth 8 with root depth 0; 1024 Entities, 65536 rows, 65536 selected fields/table/container members, 1 MiB of Schema IDs, class names and byte-string values; binding and complete state checked separately |
| Patch work | 65536 Entity/table/container/row operations, 65536 scalar values and 1 MiB of Schema IDs, class names and byte-string values, including replacement subtrees |
| `max_lifetime_entries` | 132096 remembered Entity/table/last-visible-row records; enforced at baseline validation and by the stateful publisher/replica |
| `max_message_bytes` | 4 MiB of encoded body, independently of native state and transport pending queues |
| `max_recursion_depth` | 128 Protobuf message levels, including record wrappers and unknown groups; distinct from Entity depth |

Byte parsing uses `CodedInputStream` with the configured total-byte and recursion
limits. Generated-message overloads also check nesting and body size. Native
state/patch validation does not measure exact heap use: parsed messages, old state,
candidate state and copied history can coexist. Applications separately bound
pending frames/bytes/age, retained publications, simultaneous work and peak memory.
When encoding into an arena-owned output, staged allocations remain subject to
that arena's lifetime, including allocations from a failed attempt.

| Failure | C++ behavior |
| --- | --- |
| Missing alternatives, duplicate indices, wrong binding/type, malformed bytes | Throw `std::invalid_argument` |
| State/patch/body limit or generated-message depth exceeded | Throw `std::length_error`; raw parser recursion failure reports `std::invalid_argument` |
| Allocation failure | Propagate `std::bad_alloc`; no partial generated output or native return is published |
| Stateful `apply()` rejects a decoded publication | Return `rejected` and discard active receiver state; previously retained immutable handles remain readable |

Encoding stages on the destination's Protobuf arena and swaps only after complete
validation. Codec exceptions do not call or invalidate a replica; the application
handles stream closure and state disposal. A replica allocation failure separately
preserves its prior committed state/revision; this local guarantee does not create
a transport retry policy.

`check-nested-wire` covers round trips, all scalar alternatives, binary values,
duplicate records, malformed structures, independent budgets, arena outputs and
stateful rejection with both default and 32-bit native GUIDs. These are local C++
correctness checks; client and network integration require their own acceptance.
