# Object vectors and ownership

Include `<ark/container/vector.hpp>` and consume the `ark::core` CMake target.
`pointer_vector<T>` owns its objects exclusively; `shared_vector<T>` stores
`std::shared_ptr<T>`. `value_vector<T>` remains an alias for `std::vector<T>`.

## Ownership

| Operation | `pointer_vector<T>` | `shared_vector<T>` |
| --- | --- | --- |
| Copy | Disabled; move the container instead | Copies shared ownership |
| Move | Transfers ownership and empties the source | Transfers stored references and empties the source |
| Replace a slot | Deletes the previous object; the same pointer in the same slot is unchanged | Releases the previous reference |
| `erase(index)` | Deletes the object and leaves an empty slot | Releases the reference and leaves an empty slot |
| `clear()` or destruction | Deletes all owned objects | Releases all stored references |

Self-move assignment leaves the container unchanged.

A non-null raw pointer belongs to one owning slot only. Do not place it in another slot/owning container or delete pointers borrowed through `at()` or iteration. Replace/clear through container methods; iterator assignment bypasses ownership. Scalar C++ `delete` releases objects, so deletion through `T*` must be valid.

Ownership transfers only after insertion succeeds; allocation failure during append leaves it with the caller. Retain a `unique_ptr` until insertion returns:

```cpp
#include <memory>
#include <utility>

#include <ark/container/vector.hpp>

struct item
{
    int id;
};

int main() {
    ark::pointer_vector<item> objects;
    auto pending = std::make_unique<item>(item{7});
    objects.push_back(pending.get());
    pending.release();

    auto transferred = std::move(objects);
    transferred.erase(0); // Deletes item 7; the empty slot remains.

    auto external = std::make_shared<item>(item{8});
    ark::shared_vector<item> shared;
    shared.push_back(external);
    auto copy = shared;
    shared.clear();
    copy.clear(); // external still owns item 8.
}
```

## Slots and iteration

| Slot operation | Behavior |
| --- | --- |
| `size()` / `resize(n)` | Size includes empty slots; resize only grows and null-initializes, never shrinks |
| `push_back(value)` | Fills the first empty slot, otherwise appends |
| `push_back(index, value)` | Replaces an existing slot; index ≥ `size()` appends one. Use `resize()` before targeting a specific index |
| `at(index)` | Null outside the vector; `operator[]` / `erase(index)` require a valid index |
| Iteration | Forward/reverse visit null slots; read-only access uses `cbegin()` / `cend()`, `crbegin()` / `crend()` |

Caller synchronization is required.

## Migration

Replace copies of `pointer_vector` with `std::move` when transferring ownership.
Use `shared_vector` when independent containers must share objects. Its stored
element and iterator types are `std::shared_ptr<T>`; the raw variant stores `T*`.
Keep `value_vector` or `std::vector` for value storage.

Owning raw vectors and maps now use scalar C++ `delete` directly and no longer
apply `ARK_DELETE` overrides. For custom release, use a shared container with a
`std::shared_ptr` custom deleter, or a standard container of smart pointers.
The `buffer` allocation/release hooks `ARK_NEW_ARRAY` / `ARK_DELETE_ARRAY` are
unchanged; see [receive-buffer ownership](network.md#receive-buffer-ownership).

## Related object maps

Include `<ark/container/map.hpp>` for `object_map<K, T>` and
`object_hashmap<K, T>`. A successful `insert(key, pointer)` transfers ownership;
a rejected duplicate key leaves the incoming pointer with the caller.
`erase(key)`, `erase(iterator)`, `clear()` and destruction release owned objects
with scalar `delete`. Each object must belong to only one owning entry, and
pointers returned by lookup or iteration are borrowed. Assigning a raw pointer
through an iterator bypasses ownership handling.

Owning raw maps forbid copying. Moving transfers all entries and empties the
source; move assignment first releases the destination's previous objects.
Self-move assignment leaves the map unchanged. The `shared_object_map<K, T>` and
`shared_object_hashmap<K, T>` variants retain shared-copy semantics and the same
move behavior; removal releases only the stored references.

This is a source compatibility change for raw maps: replace former shallow copies
with `std::move` to transfer ownership, or use a shared map when independent maps
must retain references to the same objects. Rebuild consumers that instantiate
these public templates. No deep-copy operation is provided.
