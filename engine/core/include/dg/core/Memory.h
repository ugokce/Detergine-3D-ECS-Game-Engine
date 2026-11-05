#pragma once

#include "dg/core/Types.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>

namespace dg {

// Alignment every allocator uses when the caller does not ask for one. Fixed at 16 rather than
// alignof(std::max_align_t), which is 8 on Apple ARM64 and MSVC x64 but 16 on Linux, Android and x86_64 macOS: a
// platform-dependent default would make arena layouts (used(), exhaustion points) and pool request acceptance differ
// between targets. 16 also covers every SIMD vector type the engine uses.
inline constexpr usize kDefaultAlignment = 16;
static_assert(kDefaultAlignment >= alignof(std::max_align_t), "default alignment must satisfy every scalar type");

[[nodiscard]] constexpr bool isPowerOfTwo(usize value) {
    return value != 0 && (value & (value - 1)) == 0;
}

// Rounds value up to the next multiple of alignment (a power of two). Wraps if value is within alignment of SIZE_MAX.
[[nodiscard]] constexpr usize alignUp(usize value, usize alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

// Pointer overload. Advances the original pointer instead of casting an integer back, so pointer provenance (and
// therefore ASan/UBSan and optimizer reasoning) is preserved. A template so that a literal 0 never makes
// alignUp(0, n) ambiguous with the integer overload.
template <typename T>
[[nodiscard]] T* alignUp(T* pointer, usize alignment) {
    using Byte = std::conditional_t<std::is_const_v<T>, const std::byte, std::byte>;
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    return reinterpret_cast<T*>(reinterpret_cast<Byte*>(pointer) + (alignUp(address, alignment) - address));
}

// Allocator interface shared by every engine allocator.
//
// Contract for all implementations:
//  * alignment must be a power of two (DG_ASSERT in checked builds; an invalid alignment returns nullptr otherwise).
//  * allocate(0, ...) returns nullptr, and deallocate(nullptr, ...) is a no-op, so zero-size requests need no
//    special casing by callers.
//  * allocate returns nullptr on exhaustion; it never throws.
//  * deallocate must receive the same size and alignment that were passed to allocate.
// Thread safety is per implementation and documented on each class.
class IAllocator {
public:
    virtual ~IAllocator() = default;

    [[nodiscard]] virtual void* allocate(usize size, usize alignment = kDefaultAlignment) = 0;
    virtual void deallocate(void* pointer, usize size, usize alignment) noexcept = 0;

    // Allocates and constructs a T. Returns nullptr (and constructs nothing) when the allocation fails.
    template <typename T, typename... Args>
    [[nodiscard]] T* create(Args&&... args) {
        void* memory = allocate(sizeof(T), alignof(T));
        if (memory == nullptr) {
            return nullptr;
        }
        return std::construct_at(static_cast<T*>(memory), std::forward<Args>(args)...);
    }

    // Destroys and frees an object made by create<T>. T must be the exact type passed to create (sizes must match);
    // nullptr is ignored.
    template <typename T>
    void destroy(T* object) noexcept {
        if (object == nullptr) {
            return;
        }
        std::destroy_at(object);
        deallocate(const_cast<std::remove_cv_t<T>*>(object), sizeof(T), alignof(T));
    }
};

// General-purpose allocator over aligned global ::operator new / delete. Thread-safe. Tracks live bytes and live
// allocation counts (relaxed atomics: statistics only, never used for synchronization).
class HeapAllocator final : public IAllocator {
public:
    constexpr HeapAllocator() = default;
    HeapAllocator(const HeapAllocator&) = delete;
    HeapAllocator& operator=(const HeapAllocator&) = delete;

    [[nodiscard]] void* allocate(usize size, usize alignment = kDefaultAlignment) override;
    void deallocate(void* pointer, usize size, usize alignment) noexcept override;

    // Sum of the sizes of all live allocations.
    [[nodiscard]] usize bytesInUse() const { return bytesUsed.load(std::memory_order_relaxed); }
    // Number of live allocations.
    [[nodiscard]] usize allocationCount() const { return liveAllocations.load(std::memory_order_relaxed); }

private:
    std::atomic<usize> bytesUsed{0};
    std::atomic<usize> liveAllocations{0};
};

// Process-wide heap allocator. Never destroyed, so objects released during static destruction can still use it.
[[nodiscard]] IAllocator& defaultAllocator();

} // namespace dg
