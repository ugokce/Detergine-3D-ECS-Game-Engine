#include "dg/core/LinearAllocator.h"

#include "dg/core/Assert.h"

#include <cstdint>
#include <utility>

namespace dg {

LinearAllocator::LinearAllocator(std::span<std::byte> buffer) : base(buffer.data()), capacityBytes(buffer.size()) {}

LinearAllocator::LinearAllocator(usize capacity, IAllocator& backingAllocator) {
    if (capacity == 0) {
        return;
    }
    base = static_cast<std::byte*>(backingAllocator.allocate(capacity, kDefaultAlignment));
    if (base != nullptr) {
        capacityBytes = capacity;
        backing = &backingAllocator;
    }
}

LinearAllocator::~LinearAllocator() {
    release();
}

LinearAllocator::LinearAllocator(LinearAllocator&& other) noexcept
    : base(std::exchange(other.base, nullptr)), capacityBytes(std::exchange(other.capacityBytes, 0)),
      offset(std::exchange(other.offset, 0)), backing(std::exchange(other.backing, nullptr)) {}

LinearAllocator& LinearAllocator::operator=(LinearAllocator&& other) noexcept {
    if (this != &other) {
        release();
        base = std::exchange(other.base, nullptr);
        capacityBytes = std::exchange(other.capacityBytes, 0);
        offset = std::exchange(other.offset, 0);
        backing = std::exchange(other.backing, nullptr);
    }
    return *this;
}

void* LinearAllocator::allocate(usize size, usize alignment) {
    DG_ASSERT_MSG(isPowerOfTwo(alignment), "allocation alignment must be a power of two");
    if (size == 0 || !isPowerOfTwo(alignment) || base == nullptr) {
        return nullptr;
    }
    // Align the absolute address, not the offset: a borrowed buffer may itself be only byte-aligned.
    const auto current = reinterpret_cast<std::uintptr_t>(base) + offset;
    const usize padding = alignUp(current, alignment) - current;
    const usize available = capacityBytes - offset;
    // Written as two comparisons so that huge sizes cannot overflow padding + size.
    if (padding > available || size > available - padding) {
        return nullptr;
    }
    std::byte* result = base + offset + padding;
    offset += padding + size;
    return result;
}

void LinearAllocator::deallocate(void* /*pointer*/, usize /*size*/, usize /*alignment*/) noexcept {}

void LinearAllocator::rewind(Marker marker) {
    DG_ASSERT_MSG(marker.offset <= offset, "rewind to a marker newer than the arena position");
    if (marker.offset <= offset) {
        offset = marker.offset;
    }
}

void LinearAllocator::release() noexcept {
    if (backing != nullptr) {
        backing->deallocate(base, capacityBytes, kDefaultAlignment);
    }
    base = nullptr;
    capacityBytes = 0;
    offset = 0;
    backing = nullptr;
}

} // namespace dg
