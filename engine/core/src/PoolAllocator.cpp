#include "dg/core/PoolAllocator.h"

#include "dg/core/Assert.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

namespace dg {

PoolAllocator::PoolAllocator(usize blockSize, usize blockAlignment, usize blocksPerChunk,
                             IAllocator& backingAllocator)
    : backing(&backingAllocator), requestedBlockSize(blockSize), chunkBlockCount(blocksPerChunk) {
    DG_ASSERT_MSG(isPowerOfTwo(blockAlignment), "pool block alignment must be a power of two");
    DG_ASSERT_MSG(blockSize > 0 && blocksPerChunk > 0, "pool blocks and chunks must be non-empty");
    // Even in unchecked builds keep the layout well-formed; an invalid configuration just never allocates.
    if (!isPowerOfTwo(blockAlignment) || blockSize == 0 || blocksPerChunk == 0) {
        backing = nullptr;
        return;
    }
    alignment = std::max(blockAlignment, alignof(FreeBlock));
    stride = alignUp(std::max(blockSize, sizeof(FreeBlock)), alignment);
    blocksOffset = alignUp(sizeof(ChunkHeader), alignment);
}

PoolAllocator::~PoolAllocator() {
    release();
}

PoolAllocator::PoolAllocator(PoolAllocator&& other) noexcept
    : backing(std::exchange(other.backing, nullptr)), chunkList(std::exchange(other.chunkList, nullptr)),
      freeList(std::exchange(other.freeList, nullptr)), requestedBlockSize(std::exchange(other.requestedBlockSize, 0)),
      alignment(std::exchange(other.alignment, 0)), stride(std::exchange(other.stride, 0)),
      blocksOffset(std::exchange(other.blocksOffset, 0)), chunkBlockCount(std::exchange(other.chunkBlockCount, 0)),
      used(std::exchange(other.used, 0)), capacity(std::exchange(other.capacity, 0)),
      chunks(std::exchange(other.chunks, 0)) {}

PoolAllocator& PoolAllocator::operator=(PoolAllocator&& other) noexcept {
    if (this != &other) {
        release();
        backing = std::exchange(other.backing, nullptr);
        chunkList = std::exchange(other.chunkList, nullptr);
        freeList = std::exchange(other.freeList, nullptr);
        requestedBlockSize = std::exchange(other.requestedBlockSize, 0);
        alignment = std::exchange(other.alignment, 0);
        stride = std::exchange(other.stride, 0);
        blocksOffset = std::exchange(other.blocksOffset, 0);
        chunkBlockCount = std::exchange(other.chunkBlockCount, 0);
        used = std::exchange(other.used, 0);
        capacity = std::exchange(other.capacity, 0);
        chunks = std::exchange(other.chunks, 0);
    }
    return *this;
}

void* PoolAllocator::allocate(usize size, usize requestedAlignment) {
    if (backing == nullptr) {
        return nullptr; // moved-from or invalidly configured: not a request-level error
    }
    DG_ASSERT_MSG(isPowerOfTwo(requestedAlignment), "allocation alignment must be a power of two");
    if (size == 0) {
        return nullptr; // the IAllocator contract for zero-size requests, whatever alignment they name
    }
    DG_ASSERT_MSG(size <= requestedBlockSize, "allocation is larger than the pool block size");
    DG_ASSERT_MSG(requestedAlignment <= alignment, "allocation is more aligned than the pool blocks");
    if (size > requestedBlockSize || !isPowerOfTwo(requestedAlignment) || requestedAlignment > alignment) {
        return nullptr;
    }
    if (freeList == nullptr && !grow()) {
        return nullptr;
    }
    FreeBlock* block = freeList;
    freeList = block->next;
    std::destroy_at(block);
    ++used;
    return block;
}

void PoolAllocator::deallocate(void* pointer, usize /*size*/, usize /*alignment*/) noexcept {
    if (pointer == nullptr) {
        return;
    }
    DG_ASSERT_MSG(owns(pointer), "pointer was not allocated from this pool");
    DG_ASSERT_MSG(used > 0, "pool deallocate without a live block");
    freeList = std::construct_at(static_cast<FreeBlock*>(pointer), FreeBlock{freeList});
    --used;
}

bool PoolAllocator::owns(const void* pointer) const {
    // Integer comparisons: relational operators on pointers into different allocations are unspecified.
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    for (const ChunkHeader* chunk = chunkList; chunk != nullptr; chunk = chunk->next) {
        const auto first = reinterpret_cast<std::uintptr_t>(chunk) + blocksOffset;
        const auto end = first + stride * chunkBlockCount;
        if (address >= first && address < end) {
            return (address - first) % stride == 0;
        }
    }
    return false;
}

usize PoolAllocator::chunkAlignment() const {
    return std::max(alignment, alignof(ChunkHeader));
}

bool PoolAllocator::grow() {
    if (backing == nullptr) {
        return false;
    }
    auto* memory = static_cast<std::byte*>(backing->allocate(chunkBytes(), chunkAlignment()));
    if (memory == nullptr) {
        return false;
    }
    chunkList = std::construct_at(reinterpret_cast<ChunkHeader*>(memory), ChunkHeader{chunkList});
    // Push in reverse so the lowest-addressed block is handed out first.
    std::byte* blocks = memory + blocksOffset;
    for (usize i = chunkBlockCount; i-- > 0;) {
        freeList = std::construct_at(reinterpret_cast<FreeBlock*>(blocks + i * stride), FreeBlock{freeList});
    }
    capacity += chunkBlockCount;
    ++chunks;
    return true;
}

void PoolAllocator::release() noexcept {
    const usize bytes = chunkBytes();
    const usize chunkAlign = chunkAlignment();
    ChunkHeader* chunk = chunkList;
    while (chunk != nullptr) {
        ChunkHeader* next = chunk->next;
        backing->deallocate(chunk, bytes, chunkAlign);
        chunk = next;
    }
    chunkList = nullptr;
    freeList = nullptr;
    used = 0;
    capacity = 0;
    chunks = 0;
}

} // namespace dg
