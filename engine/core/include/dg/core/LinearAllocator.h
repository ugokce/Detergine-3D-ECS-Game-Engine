#pragma once

#include "dg/core/Memory.h"
#include "dg/core/Types.h"

#include <cstddef>
#include <span>

namespace dg {

// Bump-pointer arena with a fixed capacity (it never grows). Memory comes either from a caller-provided buffer, which
// the arena borrows, or from a single block taken from a backing allocator and returned in the destructor.
//
// Individual deallocation is a no-op; memory is reclaimed in bulk with rewind() or reset(). Exhaustion returns
// nullptr without asserting, because callers such as frame allocators pick their own fallback. Not thread-safe: use
// one arena per thread.
class LinearAllocator final : public IAllocator {
public:
    // Position in the arena, used to free everything allocated after it.
    struct Marker {
        usize offset = 0;
    };

    // Empty arena (capacity 0): every allocation fails.
    LinearAllocator() = default;
    // Borrows buffer; it must outlive the arena.
    explicit LinearAllocator(std::span<std::byte> buffer);
    // Owns a capacity-byte block from backingAllocator (aligned to kDefaultAlignment). If that allocation fails the
    // arena is empty, which callers can detect with capacity() == 0.
    explicit LinearAllocator(usize capacity, IAllocator& backingAllocator = defaultAllocator());
    ~LinearAllocator() override;

    LinearAllocator(const LinearAllocator&) = delete;
    LinearAllocator& operator=(const LinearAllocator&) = delete;
    // The moved-from arena is left empty.
    LinearAllocator(LinearAllocator&& other) noexcept;
    LinearAllocator& operator=(LinearAllocator&& other) noexcept;

    // Returns nullptr when size is 0, the alignment is invalid, or the remaining space (including alignment padding)
    // is too small.
    [[nodiscard]] void* allocate(usize size, usize alignment = kDefaultAlignment) override;
    // No-op: arenas free in bulk.
    void deallocate(void* pointer, usize size, usize alignment) noexcept override;

    [[nodiscard]] Marker mark() const { return Marker{offset}; }
    // Frees everything allocated after marker. The marker must come from this arena and not be newer than the
    // current position.
    void rewind(Marker marker);
    void reset() { offset = 0; }

    [[nodiscard]] usize used() const { return offset; }
    [[nodiscard]] usize capacity() const { return capacityBytes; }
    [[nodiscard]] usize remaining() const { return capacityBytes - offset; }

private:
    void release() noexcept;

    std::byte* base = nullptr;
    usize capacityBytes = 0;
    usize offset = 0;
    IAllocator* backing = nullptr; // non-null only when the arena owns base
};

} // namespace dg
