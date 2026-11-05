#include "dg/core/Memory.h"

#include "dg/core/Assert.h"

#include <new>

namespace dg {

void* HeapAllocator::allocate(usize size, usize alignment) {
    DG_ASSERT_MSG(isPowerOfTwo(alignment), "allocation alignment must be a power of two");
    if (size == 0 || !isPowerOfTwo(alignment)) {
        return nullptr;
    }
    // nothrow: exceptions are disabled in Shipping, and the IAllocator contract reports exhaustion as nullptr.
    void* pointer = ::operator new(size, std::align_val_t{alignment}, std::nothrow);
    if (pointer == nullptr) {
        return nullptr;
    }
    bytesUsed.fetch_add(size, std::memory_order_relaxed);
    liveAllocations.fetch_add(1, std::memory_order_relaxed);
    return pointer;
}

void HeapAllocator::deallocate(void* pointer, usize size, usize alignment) noexcept {
    if (pointer == nullptr) {
        return;
    }
    DG_ASSERT_MSG(isPowerOfTwo(alignment), "deallocation alignment must be a power of two");
    DG_ASSERT_MSG(bytesInUse() >= size && allocationCount() > 0, "deallocate does not match a live allocation");
    bytesUsed.fetch_sub(size, std::memory_order_relaxed);
    liveAllocations.fetch_sub(1, std::memory_order_relaxed);
    // Unsized aligned delete: sized deallocation is not enabled by default on every toolchain we target.
    ::operator delete(pointer, std::align_val_t{alignment});
}

namespace {

// Constant-initialized (no guard, no init-order issues) and deliberately never destroyed.
union DefaultAllocatorStorage {
    HeapAllocator allocator;

    constexpr DefaultAllocatorStorage() : allocator() {}
    constexpr ~DefaultAllocatorStorage() {}
};

constinit DefaultAllocatorStorage gDefaultAllocator;

} // namespace

IAllocator& defaultAllocator() {
    return gDefaultAllocator.allocator;
}

} // namespace dg
