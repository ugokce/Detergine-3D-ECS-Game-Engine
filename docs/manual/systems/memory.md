---
title: Memory allocators
section: Systems
order: 20
status: implemented
summary: The IAllocator contract and the three allocators built on it (heap, linear arena, fixed-size pool), with a platform-independent default alignment of 16.
sources: engine/core/include/dg/core/Memory.h, engine/core/src/Memory.cpp, engine/core/include/dg/core/LinearAllocator.h, engine/core/src/LinearAllocator.cpp, engine/core/include/dg/core/PoolAllocator.h, engine/core/src/PoolAllocator.cpp, tests/core/MemoryTest.cpp
---

## Overview

Engine code does not call `new`/`delete` or `malloc` directly (CLAUDE.md, C++ conventions). It allocates through
**`IAllocator`**, a small interface in `dg::core` with three implementations:

| Allocator | Header | Strategy | Frees | Thread-safe |
|---|---|---|---|---|
| `HeapAllocator` | [Memory.h](engine/core/include/dg/core/Memory.h) | Aligned global `::operator new` / `delete`, with live-byte statistics | Per allocation | Yes |
| `LinearAllocator` | [LinearAllocator.h](engine/core/include/dg/core/LinearAllocator.h) | **Bump-pointer arena** of fixed capacity | In bulk: `rewind(marker)` or `reset()` | No |
| `PoolAllocator` | [PoolAllocator.h](engine/core/include/dg/core/PoolAllocator.h) | Fixed-size blocks carved from chunks, intrusive free list | Per block | No |

`defaultAllocator()` returns one process-wide `HeapAllocator`. Containers and pools that take an `IAllocator&` default
to it, and tests pass their own `HeapAllocator` to check for leaks.

Key guarantees:

* **One contract for every allocator** (see below): zero-size requests return `nullptr`, exhaustion returns `nullptr`
  and never throws, and alignment must be a power of two.
* **Layout is the same on every platform.** The default alignment is the constant **`kDefaultAlignment`** `= 16`, not
  `alignof(std::max_align_t)`, so default-aligned arena offsets, exhaustion points and pool acceptance do not differ
  between x64 and ARM64 targets. Requests aligned to more than 16 bytes get padding that depends on the block's address
  (see the tip under *When to use which*).
* **Allocate-side misuse is loud in checked builds and safe in Release.** Contract violations hit `DG_ASSERT` in Debug
  and Development. In Release, invalid `allocate` requests (non-power-of-two alignment, oversized or over-aligned pool
  requests) return `nullptr`, and `LinearAllocator::rewind` to a newer marker is ignored.
* **Deallocate-side misuse is only caught in checked builds.** Freeing a foreign pointer, freeing twice, or passing a
  size or alignment that does not match the allocation asserts in Debug and Development, and is undefined behaviour in
  Release: the call goes ahead and corrupts the free list or the statistics.

DESIGN.md §4.1 also plans frame allocators, TLSF heaps, VMA for GPU memory, `dg::Unique` / `dg::Ref` ownership types and
per-subsystem memory tags. None of those exist yet; this page documents only what is implemented.

## How to use

### The IAllocator contract

| Rule | Detail |
|---|---|
| Alignment | Must be a power of two. A bad value is a `DG_ASSERT` in checked builds and returns `nullptr` otherwise. |
| Zero size | `allocate(0, …)` returns `nullptr`; `deallocate(nullptr, …)` is a no-op. Callers need no special case. |
| Exhaustion | `allocate` returns `nullptr`. It never throws (exceptions are disabled in Shipping). |
| Matching free | `deallocate` must receive the same `size` and `alignment` that were passed to `allocate`. |
| Threads | Per implementation; documented on each class (table above). |

### Objects: create and destroy

`IAllocator::create<T>(args...)` allocates `sizeof(T)` bytes at `alignof(T)` and constructs a `T` in place.
`destroy(object)` runs the destructor and frees with the same size and alignment:

```cpp
#include "dg/core/Memory.h"

#include <string>

struct Profile {
    std::string name;
    int level = 1;
};

void Example() {
    dg::IAllocator& allocator = dg::defaultAllocator();
    Profile* profile = allocator.create<Profile>("Ada", 7);
    if (profile == nullptr) {
        return; // out of memory: create returns nullptr and constructs nothing
    }
    profile->level += 1;
    allocator.destroy(profile); // nullptr is ignored
}
```

`destroy<T>` must get the exact type that was created, not a base class, because the size it frees is `sizeof(T)`.

### Raw memory with statistics: HeapAllocator

```cpp
#include "dg/core/Memory.h"

void HeapExample() {
    dg::HeapAllocator heap;
    void* buffer = heap.allocate(256, 64); // 256 bytes, 64-byte aligned
    // heap.bytesInUse() == 256, heap.allocationCount() == 1
    heap.deallocate(buffer, 256, 64);
    // both counters are back to 0
}
```

A `HeapAllocator` instance is a useful leak detector: give it to the code under test and check that `bytesInUse()` and
`allocationCount()` are `0` afterwards. The counters are statistics only; they never synchronize anything.

### Scratch memory: LinearAllocator

An arena hands out memory by bumping an offset. Individual `deallocate` calls do nothing; you free everything after a
**marker** with `rewind`, or everything with `reset`:

```cpp
#include "dg/core/LinearAllocator.h"
#include "dg/core/Types.h"

void ScratchExample() {
    dg::LinearAllocator arena(64 * 1024); // owns a 64 KiB block from defaultAllocator()
    if (arena.capacity() == 0) {
        return; // the backing allocation failed
    }

    auto* positions = static_cast<dg::f32*>(arena.allocate(1024 * sizeof(dg::f32), alignof(dg::f32)));

    const dg::LinearAllocator::Marker beforeTemp = arena.mark();
    void* temp = arena.allocate(4096); // default 16-byte alignment
    (void)temp;
    arena.rewind(beforeTemp); // frees `temp`, keeps `positions`

    (void)positions;
    arena.reset(); // frees everything; capacity is kept
}
```

An arena can also borrow memory you already have, for example a stack buffer. The buffer must outlive the arena:

```cpp
#include "dg/core/LinearAllocator.h"

#include <array>
#include <cstddef>
#include <span>

void BorrowedExample() {
    alignas(16) std::array<std::byte, 512> storage{};
    dg::LinearAllocator arena{std::span<std::byte>(storage)};
    void* a = arena.allocate(100); // at storage.data()
    void* b = arena.allocate(1);   // at storage.data() + 112 (100 rounded up to 16)
    (void)a;
    (void)b;
}
```

Alignment is applied to the absolute address, so a borrowed buffer that is itself only byte-aligned still yields
correctly aligned results. When the arena is full, `allocate` returns `nullptr` without asserting: frame allocators and
other callers choose their own fallback.

### Many equal-sized objects: PoolAllocator

A pool serves blocks of one size. It grows by whole chunks of `blocksPerChunk` blocks taken from a backing allocator and
returns them only in its destructor or when another pool is move-assigned over it:

```cpp
#include "dg/core/PoolAllocator.h"

struct Particle {
    float position[3];
    float velocity[3];
    float age;
};

void PoolExample() {
    dg::PoolAllocator pool(sizeof(Particle), alignof(Particle), 256); // 256 blocks per chunk
    Particle* p = pool.create<Particle>();                            // one block
    if (p == nullptr) {
        return;
    }
    p->age = 0.0f;
    pool.destroy(p); // block goes back to the free list
    // pool.chunkCount() == 1, pool.capacityBlocks() == 256, pool.usedBlocks() == 0
}
```

Within a fresh chunk, blocks are handed out lowest address first. Freed blocks are recycled **LIFO**: the most recently
freed block is returned by the next `allocate`, which is the one most likely to still be in cache. A request larger
than `blockSize()` or more aligned than `blockAlignment()` is a programmer error.

> **Note:** The pool's destructor releases its chunks but does not run destructors. Destroy the objects that still
> live in it first.

### When to use which

| Situation | Use |
|---|---|
| General long-lived allocations of varying size, or anything shared across threads | `HeapAllocator` / `defaultAllocator()` |
| Temporary data with a clear lifetime scope (per frame, per tick, per load step, per function) | `LinearAllocator`, with a marker per scope |
| Many objects of one type created and destroyed independently (nodes, components, events) | `PoolAllocator` |
| Objects you refer to across systems by ID, with stale-reference detection | `HandlePool` ([Generational handles](handles.md)), which takes its pages from any `IAllocator` |
| Checking a subsystem for leaks in a test | A local `HeapAllocator` passed as the backing allocator |

Use one `LinearAllocator` or `PoolAllocator` per thread; neither is thread-safe.

> **Tip:** Allocation order and addresses must never influence sim results (determinism rule 4). `LinearAllocator`
> aligns the absolute address, not the offset, so its offsets, `used()` and exhaustion point are a pure function of
> the request sequence only when every request's alignment is at most `kDefaultAlignment` and the block is owned or
> borrowed at 16-byte alignment. A 32- or 64-byte-aligned request, or a byte-aligned borrowed buffer, makes the padding
> depend on the address, even between runs on one machine. Sim code must also order its data by entity or index, never
> by pointer.

## How to edit

### Where the code lives

| File | Contents |
|---|---|
| [Memory.h](engine/core/include/dg/core/Memory.h) | `kDefaultAlignment`, `isPowerOfTwo`, both `alignUp` overloads, `IAllocator`, `HeapAllocator`, `defaultAllocator()` |
| [Memory.cpp](engine/core/src/Memory.cpp) | `HeapAllocator` implementation and the never-destroyed default allocator |
| [LinearAllocator.h](engine/core/include/dg/core/LinearAllocator.h), [LinearAllocator.cpp](engine/core/src/LinearAllocator.cpp) | The arena |
| [PoolAllocator.h](engine/core/include/dg/core/PoolAllocator.h), [PoolAllocator.cpp](engine/core/src/PoolAllocator.cpp) | The block pool |
| [MemoryTest.cpp](tests/core/MemoryTest.cpp) | Unit and death tests for all of the above |

### Why kDefaultAlignment is 16

`alignof(std::max_align_t)` is 8 on Apple ARM64 and MSVC x64 but 16 on Linux, Android and x86_64 macOS. Using it as the
default would make an arena's `used()` value, the point at which it is exhausted, and whether a pool accepts a
default-aligned request depend on the target. A fixed 16 keeps those identical everywhere and also covers every SIMD
vector type the engine uses. A `static_assert` keeps it at least `alignof(std::max_align_t)`.

### Implementation notes

* **`defaultAllocator()`** returns a `HeapAllocator` held in a `constinit` union whose destructor does nothing. It is
  constant-initialized (no static-init-order problems) and never destroyed, so objects released during static
  destruction can still free through it.
* **`HeapAllocator`** uses `::operator new(size, std::align_val_t, std::nothrow)` and the unsized aligned
  `::operator delete`, because sized deallocation is not enabled by default on every target toolchain.
* **`alignUp(T*, alignment)`** advances the original pointer instead of casting an integer back to a pointer, which
  keeps pointer provenance intact for ASan, UBSan and the optimizer. It is a template so that `alignUp(0, n)` is never
  ambiguous with the integer overload.
* **`LinearAllocator::allocate`** checks space as `padding > available || size > available - padding`, so huge sizes
  cannot overflow. An owning arena takes its block at `kDefaultAlignment` and returns it in the destructor or when
  move-assigned over.
* **`PoolAllocator`** pads blocks to at least pointer size and alignment so a free block can hold the next-pointer.
  Each chunk starts with a header that links the chunks, followed by the blocks at
  `alignUp(sizeof(ChunkHeader), alignment)`. `owns()` walks the chunk list with integer comparisons, because relational
  operators between pointers into different allocations are unspecified.
* **Invalid configuration:** a `PoolAllocator` built with a non-power-of-two alignment or a zero size or chunk count
  asserts in checked builds; in Release it is left empty and every `allocate` returns `nullptr`.

### Invariants a change must keep

* Every implementation honours the contract table in *How to use*. Add a test for any new allocator that checks all
  four rules.
* `kDefaultAlignment` stays a platform-independent constant. Changing it changes arena layouts; check that the
  `DefaultAlignment.IsTheSameOnEveryPlatform` test still describes the intended layout.
* `allocate` never throws and never asserts on plain exhaustion. Asserts are for misuse only.
* `HandlePool` takes its pages from an `IAllocator` (see [Generational handles](handles.md)); a change to the interface
  touches it too.
* Do not add RTTI or exceptions to the allocators (CLAUDE.md, C++ conventions).

### Adding an allocator

1. Derive `final` from `IAllocator` and implement `allocate(usize size, usize alignment = kDefaultAlignment)` and
   `deallocate(void*, usize, usize) noexcept`.
2. Assert power-of-two alignment with `DG_ASSERT_MSG(isPowerOfTwo(alignment), "…")` and still return `nullptr` for it,
   so Release builds stay safe.
3. Take an `IAllocator& backingAllocator = defaultAllocator()` if it needs memory from somewhere, and document its thread
   safety on the class.
4. Add unit tests, death tests (inside `#if DG_ASSERTS_ENABLED`) and an unchecked-build test (inside
   `#if !DG_ASSERTS_ENABLED`) to `tests/core/MemoryTest.cpp`, and update this page.
5. Add every new source and header file to the `sources` front matter of this page. Coverage is checked per file, so a
   new file under `engine/core/` that no page lists fails `./dg docs --check`.

## Reference

### Free functions and constants (Memory.h)

| Declaration | Description |
|---|---|
| `inline constexpr usize kDefaultAlignment = 16` | Alignment used when the caller does not pass one |
| `constexpr bool isPowerOfTwo(usize value)` | `false` for `0` |
| `constexpr usize alignUp(usize value, usize alignment)` | Rounds up to a multiple of `alignment` (a power of two); wraps near `SIZE_MAX` |
| `template <class T> T* alignUp(T* pointer, usize alignment)` | Pointer overload; works for `const T*` |
| `IAllocator& defaultAllocator()` | The process-wide `HeapAllocator` |

### IAllocator

| Member | Description |
|---|---|
| `virtual void* allocate(usize size, usize alignment = kDefaultAlignment) = 0` | See the contract |
| `virtual void deallocate(void* pointer, usize size, usize alignment) noexcept = 0` | See the contract |
| `template <class T, class... Args> T* create(Args&&... args)` | Allocate and construct; `nullptr` on failure |
| `template <class T> void destroy(T* object) noexcept` | Destroy and free; ignores `nullptr` |

### HeapAllocator

| Member | Description |
|---|---|
| `constexpr HeapAllocator()` | Not copyable |
| `usize bytesInUse() const` | Sum of the sizes of live allocations |
| `usize allocationCount() const` | Number of live allocations |

In checked builds `deallocate` asserts `"deallocate does not match a live allocation"` when the counters would go
negative, which catches freeing through the wrong `HeapAllocator`.

### LinearAllocator

| Member | Description |
|---|---|
| `struct Marker { usize offset = 0; }` | An arena position |
| `LinearAllocator()` | Empty arena: capacity 0, every allocation fails |
| `explicit LinearAllocator(std::span<std::byte> buffer)` | Borrows `buffer` |
| `explicit LinearAllocator(usize capacity, IAllocator& backingAllocator = defaultAllocator())` | Owns a block; `capacity() == 0` if that allocation failed |
| Move constructor / move assignment | The moved-from arena is empty; not copyable |
| `Marker mark() const` | Current position |
| `void rewind(Marker marker)` | Frees everything after `marker`; asserts if `marker` is newer than the current position |
| `void reset()` | Frees everything |
| `usize used() const`, `capacity() const`, `remaining() const` | Bytes used (including padding), total, and left |

### PoolAllocator

| Member | Description |
|---|---|
| `explicit PoolAllocator(usize blockSize, usize blockAlignment = kDefaultAlignment, usize blocksPerChunk = 64, IAllocator& backingAllocator = defaultAllocator())` | No memory is taken until the first `allocate` |
| Move constructor / move assignment | The moved-from pool is empty and returns `nullptr`; not copyable |
| `void* allocate(usize size, usize alignment = kDefaultAlignment)` | One block; `nullptr` for size 0, oversize or over-aligned requests (asserted), or exhaustion |
| `void deallocate(void* pointer, usize size, usize alignment) noexcept` | `size` and `alignment` are ignored; asserts on foreign pointers and on freeing with no live blocks |
| `bool owns(const void* pointer) const` | Is `pointer` the start of one of this pool's blocks; O(chunk count) |
| `blockSize()`, `blockAlignment()`, `blocksPerChunk()` | Configuration; `blockAlignment()` is at least `alignof(void*)` |
| `usedBlocks()`, `capacityBlocks()`, `chunkCount()` | Live blocks, total blocks, chunks |

## Testing

All allocator tests are in [MemoryTest.cpp](tests/core/MemoryTest.cpp), part of the `dg-core-tests` executable
(labels `unit`, `determinism`). Each `TEST` is registered with CTest as `dg-core-tests.<Suite>.<Test>`:

```bash
./dg test                                        # everything
./dg test debug                                  # runs the death tests (asserts on)
./dg test release                                # runs UncheckedAllocators (asserts off)
ctest --preset macos-arm64-dev -R LinearAllocator
```

| Suite | Covers |
|---|---|
| `AlignUp` | `isPowerOfTwo` and both `alignUp` overloads |
| `HeapAllocator` | Every alignment from 1 to 256, statistics, zero-size and `nullptr` rules, `defaultAllocator()`, `create`/`destroy` including over-aligned types |
| `LinearAllocator` | Alignment inside a buffer, markers, `rewind`, `reset`, exhaustion, padding that does not fit, owned blocks across moves |
| `PoolAllocator` | Chunk growth, LIFO recycling, every block alignment, object helpers, moves |
| `DefaultAlignment` | `kDefaultAlignment == 16` and the exact arena layout and pool acceptance it implies |
| `UncheckedAllocators` | Release only: misuse returns `nullptr` |
| `*Death` | Checked builds only: each misuse aborts with its specific `DG_ASSERT_MSG` text |

The tests write into every allocated byte, so the ASan + UBSan CI job (see [Continuous integration](../tooling/ci.md))
catches any allocator that hands out less memory than requested. See [Testing](../tooling/testing.md) for presets and
labels.
