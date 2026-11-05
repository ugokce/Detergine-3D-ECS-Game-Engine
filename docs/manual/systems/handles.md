---
title: Generational handles
section: Systems
order: 30
status: implemented
summary: Handle<Tag> and HandlePool<T, MaxGeneration>, a deterministic generational slot map with FIFO slot reuse, stale-handle detection and stable object addresses.
sources: engine/core/include/dg/core/Handle.h, tests/core/HandleTest.cpp
---

## Overview

Systems refer to each other's objects by **handle**, not by pointer (CLAUDE.md, Architecture rules). A handle is a
small value that stays safe to hold after the object it names is destroyed: looking it up then fails cleanly instead
of touching freed memory.

[Handle.h](engine/core/include/dg/core/Handle.h) provides two templates in namespace `dg`:

* **`Handle<Tag>`**: an 8-byte value holding a slot `index` and a **generation**, the number of times that slot had
  been used when the handle was issued. `Tag` only makes handles to different kinds of objects distinct types.
* **`HandlePool<T, MaxGeneration>`**: a **generational slot map** that owns objects of type `T`, issues
  `Handle<T>` values for them, and resolves handles back to objects.

`HandlePool` will back the sim entity allocator (see the planned [ECS](../planned/ecs.md)), so the handle values it
issues are part of the sim state and must be identical on every machine. Its guarantees:

* **Deterministic handle values.** Which index and generation `create()` returns depends only on the sequence of
  `create`/`destroy` calls, never on memory addresses or allocator behaviour.
* **Stale handles never alias.** A destroyed object's handle stays invalid forever. A slot whose generation would
  overflow is **retired** instead of wrapping.
* **Stable addresses.** Objects live in fixed-size pages that never move. A `T*` from `get()` stays valid until that
  object is destroyed, however many other objects are created.
* **No requirements on `T` beyond being destructible.** `T` needs no default constructor and no move or copy support.

The pool is not thread-safe.

## How to use

### Creating, resolving and destroying

```cpp
#include "dg/core/Handle.h"

#include <string>

struct Enemy {
    std::string name;
    int health;
};

void Example() {
    dg::HandlePool<Enemy> enemies;

    dg::Handle<Enemy> grunt = enemies.create("grunt", 30); // arguments go to Enemy's constructor
    if (!grunt.isValid()) {
        return; // out of memory or index space
    }

    if (Enemy* enemy = enemies.get(grunt)) {
        enemy->health -= 10;
    }

    enemies.destroy(grunt);              // returns true
    bool again = enemies.destroy(grunt); // false: the handle is stale now
    (void)again;
    Enemy* gone = enemies.get(grunt);    // nullptr
    (void)gone;
}
```

| Call | On a stale or invalid handle |
|---|---|
| `get(handle)` | Returns `nullptr` |
| `contains(handle)` | Returns `false` |
| `destroy(handle)` | Returns `false` and does nothing |

`Handle::isValid()` only checks the structure (index is not the invalid index and generation is not `0`). Whether the
object still exists is a question for the pool: use `contains()` or `get()`.

### Invalid handles and zero-initialization

A default-constructed handle and `Handle<T>::invalid()` are invalid. So is an **all-zero** handle, because generation
`0` is never issued. That matters for sim components, which are zero-initialized (determinism rule 8): a handle field
in a fresh component is invalid without any extra initialization.

```cpp
#include "dg/core/Handle.h"

struct Target {};

struct Homing {
    dg::Handle<Target> target{}; // index = kInvalidIndex, generation = 0: invalid
};

static_assert(sizeof(dg::Handle<Target>) == 8);
static_assert(!dg::Handle<Target>{}.isValid());
static_assert(!dg::Handle<Target>{0, 0}.isValid()); // the zero-initialized case
```

### Comparing and sorting handles

Handles have defaulted `==` and `<=>`, ordering by `index` and then `generation`. The order is independent of memory
addresses, so you can sort handles or use them as keys in ordered containers inside sim code (determinism rule 4).

### Iterating

`forEach` visits every live object in **ascending slot index**. The callback takes either the object or the handle and
the object:

```cpp
#include "dg/core/Handle.h"
#include "dg/core/Types.h"

dg::i64 TotalScore(const dg::HandlePool<dg::i64>& scores) {
    dg::i64 total = 0;
    scores.forEach([&](const dg::i64& score) { total += score; });
    return total;
}

void Damage(dg::HandlePool<int>& health) {
    health.forEach([](dg::Handle<int> handle, int& hp) {
        hp -= static_cast<int>(handle.index % 3);
    });
}
```

Slot order is not creation order: an object created into a reused slot is visited at that slot's position. It is
deterministic all the same, because slot assignment is.

> **Warning:** A `forEach` callback must not create or destroy objects in the same pool. Collect the handles and act on
> them after the loop.

### Bulk operations and moves

* `clear()` destroys every live object in ascending slot index, exactly as if `destroy()` were called on each in that
  order. Outstanding handles go stale, generations advance, and the pool keeps its memory.
* The destructor destroys every live object and returns all pages to the allocator.
* Pools are move-only. Moving transfers the objects, the slot state and the FIFO queue: handles issued by the source
  now resolve in the destination, and the moved-from pool is empty but usable.

### Choosing the page allocator

Pages of `kSlotsPerPage` (64) objects come from the `IAllocator` passed to the constructor, `defaultAllocator()` by
default. Pass your own allocator to account for or isolate the memory (see [Memory allocators](memory.md)):

```cpp
#include "dg/core/Handle.h"
#include "dg/core/Memory.h"

void CountedPool() {
    dg::HeapAllocator heap;
    {
        dg::HandlePool<float> pool(heap);
        (void)pool.create(1.0f);
        // heap.allocationCount() == 1: one 64-slot page
    }
    // heap.bytesInUse() == 0: the pool returned its page
}
```

Only the object pages go through the allocator. The per-slot bookkeeping is a `std::vector` with the standard
allocator.

## How to edit

### Slot assignment rules

Every rule below is load-bearing for determinism. Changing any of them changes the handle sequence, and therefore
future sim entity IDs and the golden hash.

1. **Fresh slots are taken in ascending index order.** When the free queue is empty, `create()` uses index
   `slotCount()`.
2. **Freed slots are reused FIFO**: the oldest freed slot first. FIFO also maximizes the time before a slot is reused,
   which raises the chance that a stale handle is caught rather than silently resolving after many reuses. (The design
   requires FIFO free lists for entity IDs, DESIGN.md §5.2.)
3. **Generations start at 1 and grow by one per destroy.** Generation `0` is reserved for invalid handles.
4. **Retirement:** a slot destroyed while at generation `MaxGeneration` never re-enters the free queue. A retired slot
   is skipped forever, so a stale handle can never match a newer object.
5. **Exhaustion:** if the next fresh index would equal `Handle::kInvalidIndex` (`0xFFFFFFFF`), or a page cannot be
   allocated, `create()` returns an invalid handle and constructs nothing.

Example from the tests: create six objects (indices 0 to 5), destroy 3, then 1, then 4. The next two creates get index
3 and index 1, both at generation 2. Destroy 0, and the next three creates get 4, 0, and then the fresh index 6.

`MaxGeneration` defaults to `std::numeric_limits<u32>::max()`. It is a template parameter so tests can reach retirement
in a few steps, and so a future pool that packs handles into fewer bits can cap generations to match.

### Re-entrancy

`T`'s constructor and destructor may create and destroy *other* objects in the same pool, as an entity spawning or
despawning children would. The pool guarantees:

* `create()` **reserves its slot before running the constructor.** Nested creates take the following slots, so a
  parent created first gets the lower index and its children the next ones.
* `destroy()` **marks the object dead before the destructor runs** but **queues the slot only after the destructor
  returns.** A destructor that looks up its own handle sees it as gone, and nested creates cannot be handed the slot
  that still holds the object being destroyed. Slots freed by the destructor are queued before the parent's.
* After the constructor or destructor returns, the pool re-reads its slot record, because nested calls may have grown
  (and reallocated) the slot vector. Keep that pattern when editing: never hold a `Slot&` across a call into `T`.
* If a constructor throws, the reserved slot goes back to the **front** of the free queue with its generation
  unchanged, so the next `create()` gets the same index and generation it would have.
* The pool destructor marks each object dead before destroying it, so a destructor that destroys a sibling during
  teardown cannot destroy it twice.

`forEach` callbacks are the exception: they must not create or destroy (see *How to use*).

### Pointer stability

Objects are constructed with `std::construct_at` directly in page storage and read back through `std::launder`. Pages
are allocated once (`sizeof(T) * kSlotsPerPage` bytes at `alignof(T)`) and freed only by the destructor or by move
assignment over the pool. Do not change pages to a growable array: code may hold `T*` from `get()` across creates.

### What else changes with it

* A change to the reuse order, generation rule or retirement rule must update the golden value in
  `HandlePool.ScriptedCreateDestroySequenceMatchesGoldenHash`. Once the ECS uses this pool, it also changes sim state
  hashes and is a network protocol change: bump the protocol version (CLAUDE.md, Multiplayer).
* A change to `Handle`'s layout (8 bytes: two `u32`) changes serialized data and snapshots.
* Update this page, and the glossary entries for handle, generation and retirement once the glossary has them.
* A new file for handles under `engine/core/` or `tests/core/` must be added to this page's `sources`.

## Reference

### Handle

| Member | Description |
|---|---|
| `static constexpr u32 kInvalidIndex` | `std::numeric_limits<u32>::max()` |
| `u32 index = kInvalidIndex` | Slot index |
| `u32 generation = 0` | Generation of the slot when the handle was issued; `0` means invalid |
| `constexpr bool isValid() const` | `index != kInvalidIndex && generation != 0` (structural check only) |
| `static constexpr Handle invalid()` | `Handle{}` |
| `operator==`, `operator<=>` | Defaulted: by `index`, then `generation` |

### HandlePool

`template <typename T, u32 MaxGeneration = std::numeric_limits<u32>::max()> class HandlePool`. `MaxGeneration` must be
at least 1 and `T` must be destructible (both `static_assert`ed).

| Member | Description |
|---|---|
| `using HandleType = Handle<T>` | The handle type the pool issues |
| `static constexpr u32 kSlotsPerPage = 64` | Objects per page |
| `static constexpr u32 kMaxGeneration = MaxGeneration` | Highest generation a slot reaches before retirement |
| `explicit HandlePool(IAllocator& allocator = defaultAllocator())` | Pages come from `allocator`, which must outlive the pool |
| Move constructor / move assignment | Transfers objects and handles; source is left empty and usable. Not copyable |
| `template <class... Args> HandleType create(Args&&... args)` | Constructs a `T` in the next slot; invalid handle on exhaustion |
| `bool destroy(HandleType handle)` | Destroys; `false` if invalid or stale |
| `T* get(HandleType handle)` / `const T* get(HandleType handle) const` | Object pointer, or `nullptr` if invalid or stale |
| `bool contains(HandleType handle) const` | The handle refers to a live object |
| `usize size() const`, `bool empty() const` | Live object count |
| `usize slotCount() const` | Slots ever used (live, free or retired); exclusive upper bound of issued indices |
| `void clear()` | Destroys all live objects in ascending index; keeps memory |
| `template <class Fn> void forEach(Fn&& fn)` (and `const`) | Calls `fn(T&)` or `fn(HandleType, T&)` per live object in ascending index |

## Testing

The handle tests are in [HandleTest.cpp](tests/core/HandleTest.cpp), part of `dg-core-tests` (labels `unit`,
`determinism`):

```bash
./dg test -L determinism
ctest --preset macos-arm64-dev -R HandlePool
```

| Test | Checks |
|---|---|
| `Handle.DefaultZeroAndInvalidHandlesAreNotValid` | Default, all-zero and `invalid()` handles; 8-byte size; lookups of invalid handles |
| `HandlePool.HandlesGoStaleAfterDestroy` | Stale handles, double destroy, generation 2 on reuse |
| `HandlePool.ReusesFreedSlotsInFifoOrder` | The FIFO example above |
| `HandlePool.RetiresSlotWhoseGenerationWouldWrap` | Retirement with `MaxGeneration` of 3 and 1, including through `clear()` |
| `HandlePool.ForEachVisitsLiveObjectsInAscendingSlotIndex` | `forEach` order with reused slots |
| `HandlePool.ObjectsNeverMoveAndNonMovableTypesWork` | Address stability across 500 creates; 64-byte over-aligned types |
| `HandlePool.DestroyClearAndDestructorReleaseNonTrivialObjects` | Non-trivial `T`, `clear()` order, all pages returned |
| `HandlePool.ConstructorsAndDestructorsMayReenterThePool` | Nested create/destroy, slot-vector reallocation during a destructor, teardown with siblings (regression test) |
| `HandlePool.ThrowingConstructorLeavesTheSlotReusable` | Exception safety; compiled only when exceptions are enabled |
| `HandlePool.MovingAPoolTransfersItsObjectsAndHandles` | Move construction and assignment |
| `HandlePool.ScriptedCreateDestroySequenceMatchesGoldenHash` | Golden hash `0x3a7ea63a09700779` |

The golden-hash test drives 4000 create/destroy steps from a fixed LCG, hashes every issued handle and the final pool
contents with `Fnv1a64`, and compares the result with `0x3a7ea63a09700779`, recorded on macOS ARM64. It depends only
on the slot rules, so every platform must reproduce it; CI checks it on macOS ARM64, Windows x64 and Linux x64. A
mismatch means the handle allocation order changed. If that change is intended, re-record the value and follow
*What else changes with it*. See [Testing](../tooling/testing.md) for golden hashes in general.
