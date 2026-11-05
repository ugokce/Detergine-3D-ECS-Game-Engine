#pragma once

#include "dg/core/Memory.h"
#include "dg/core/Types.h"

namespace dg {

// Fixed-size block pool. Memory is obtained from a backing allocator in whole chunks of blocksPerChunk blocks and is
// only returned to it in the destructor. Free blocks form an intrusive singly linked list stored inside the blocks
// themselves, so there is no per-block overhead. Blocks are handed out lowest address first within a fresh chunk and
// recycled LIFO (most recently freed first, which is cache-friendly). Not thread-safe.
class PoolAllocator final : public IAllocator {
public:
    // blockAlignment must be a power of two; blockSize and blocksPerChunk must be non-zero. Blocks are padded to at
    // least pointer size/alignment to hold the free-list link.
    explicit PoolAllocator(usize blockSize, usize blockAlignment = kDefaultAlignment, usize blocksPerChunk = 64,
                           IAllocator& backingAllocator = defaultAllocator());
    // Releases every chunk. Objects still living in blocks are not destroyed.
    ~PoolAllocator() override;

    PoolAllocator(const PoolAllocator&) = delete;
    PoolAllocator& operator=(const PoolAllocator&) = delete;
    // The moved-from pool is left empty; its allocate returns nullptr until it is assigned a new pool.
    PoolAllocator(PoolAllocator&& other) noexcept;
    PoolAllocator& operator=(PoolAllocator&& other) noexcept;

    // Returns one block. Requests larger than blockSize() or more aligned than blockAlignment() are programmer errors
    // (DG_ASSERT) and return nullptr. Also returns nullptr for size 0 or when the backing allocator is exhausted.
    [[nodiscard]] void* allocate(usize size, usize alignment = kDefaultAlignment) override;
    // Returns the block to the free list. size and alignment are not needed (all blocks are equal) and are ignored.
    void deallocate(void* pointer, usize size, usize alignment) noexcept override;

    // True if pointer is the start of a block in one of this pool's chunks. O(chunk count); meant for checks.
    [[nodiscard]] bool owns(const void* pointer) const;

    [[nodiscard]] usize blockSize() const { return requestedBlockSize; }
    [[nodiscard]] usize blockAlignment() const { return alignment; }
    [[nodiscard]] usize blocksPerChunk() const { return chunkBlockCount; }
    [[nodiscard]] usize usedBlocks() const { return used; }
    [[nodiscard]] usize capacityBlocks() const { return capacity; }
    [[nodiscard]] usize chunkCount() const { return chunks; }

private:
    struct FreeBlock {
        FreeBlock* next;
    };
    struct ChunkHeader {
        ChunkHeader* next;
    };

    bool grow();
    void release() noexcept;
    [[nodiscard]] usize chunkBytes() const { return blocksOffset + stride * chunkBlockCount; }
    [[nodiscard]] usize chunkAlignment() const;

    IAllocator* backing = nullptr;
    ChunkHeader* chunkList = nullptr;
    FreeBlock* freeList = nullptr;
    usize requestedBlockSize = 0;
    usize alignment = 0;
    usize stride = 0;       // distance between blocks: padded size
    usize blocksOffset = 0; // first block's offset from the chunk start, after the header
    usize chunkBlockCount = 0;
    usize used = 0;
    usize capacity = 0;
    usize chunks = 0;
};

} // namespace dg
