#include <gtest/gtest.h>

#include "dg/core/LinearAllocator.h"
#include "dg/core/Memory.h"
#include "dg/core/PoolAllocator.h"
#include "dg/core/Types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

using dg::usize;

constexpr std::array<usize, 9> kAlignments = {1, 2, 4, 8, 16, 32, 64, 128, 256};

// Returned as a remainder rather than a bool so a failing EXPECT_EQ shows how far off the pointer is.
usize misalignment(const void* pointer, usize alignment) {
    return reinterpret_cast<std::uintptr_t>(pointer) % alignment;
}

std::uintptr_t address(const void* pointer) {
    return reinterpret_cast<std::uintptr_t>(pointer);
}

struct Tracked {
    static inline int liveCount = 0;
    static inline int destructorCalls = 0;

    explicit Tracked(int v) : value(v) { ++liveCount; }
    ~Tracked() {
        --liveCount;
        ++destructorCalls;
    }
    Tracked(const Tracked&) = delete;
    Tracked& operator=(const Tracked&) = delete;

    int value;
};

struct alignas(64) OverAligned {
    std::array<std::byte, 64> payload{};
};

} // namespace

TEST(AlignUp, RoundsPointersUpByLessThanOneAlignment) {
    static_assert(!dg::isPowerOfTwo(0));
    static_assert(dg::isPowerOfTwo(1) && dg::isPowerOfTwo(2) && dg::isPowerOfTwo(4096));
    static_assert(!dg::isPowerOfTwo(3) && !dg::isPowerOfTwo(6) && !dg::isPowerOfTwo(4097));
    static_assert(dg::alignUp(0, 16) == 0);
    static_assert(dg::alignUp(1, 16) == 16);
    static_assert(dg::alignUp(16, 16) == 16);
    static_assert(dg::alignUp(17, 1) == 17);
    static_assert(dg::alignUp(255, 256) == 256);

    alignas(64) std::array<std::byte, 128> buffer{};
    for (usize alignment : kAlignments) {
        if (alignment > 64) {
            break;
        }
        for (usize offset = 0; offset < 8; ++offset) {
            SCOPED_TRACE(testing::Message() << "alignment " << alignment << ", offset " << offset);
            void* aligned = dg::alignUp(buffer.data() + offset, alignment);
            EXPECT_EQ(misalignment(aligned, alignment), 0u);
            const auto distance = static_cast<std::byte*>(aligned) - (buffer.data() + offset);
            EXPECT_GE(distance, 0);
            EXPECT_LT(static_cast<usize>(distance), alignment);
        }
    }
}

TEST(HeapAllocator, HonoursEveryAlignmentAndTracksStats) {
    dg::HeapAllocator heap;
    std::vector<std::pair<void*, std::pair<usize, usize>>> live;
    usize expectedBytes = 0;
    for (usize alignment : kAlignments) {
        for (usize size : {usize{1}, usize{3}, usize{64}, usize{1000}}) {
            SCOPED_TRACE(testing::Message() << "size " << size << ", alignment " << alignment);
            void* pointer = heap.allocate(size, alignment);
            ASSERT_NE(pointer, nullptr);
            EXPECT_EQ(misalignment(pointer, alignment), 0u);
            std::memset(pointer, 0xAB, size); // ASan flags it if the block were smaller than requested
            live.push_back({pointer, {size, alignment}});
            expectedBytes += size;
        }
    }
    EXPECT_EQ(heap.bytesInUse(), expectedBytes);
    EXPECT_EQ(heap.allocationCount(), live.size());
    for (const auto& [pointer, layout] : live) {
        heap.deallocate(pointer, layout.first, layout.second);
    }
    EXPECT_EQ(heap.bytesInUse(), 0u);
    EXPECT_EQ(heap.allocationCount(), 0u);
}

TEST(HeapAllocator, ZeroSizeIsNullptrAndFreeingNullptrIsNoOp) {
    dg::HeapAllocator heap;
    EXPECT_EQ(heap.allocate(0), nullptr);
    EXPECT_EQ(heap.allocate(0, 64), nullptr);
    heap.deallocate(nullptr, 0, dg::kDefaultAlignment);
    heap.deallocate(nullptr, 128, 16);
    EXPECT_EQ(heap.bytesInUse(), 0u);
    EXPECT_EQ(heap.allocationCount(), 0u);
}

TEST(HeapAllocator, DefaultAllocatorIsOneProcessWideHeap) {
    dg::IAllocator& a = dg::defaultAllocator();
    dg::IAllocator& b = dg::defaultAllocator();
    EXPECT_EQ(&a, &b);
    void* pointer = a.allocate(32, 32);
    ASSERT_NE(pointer, nullptr);
    EXPECT_EQ(misalignment(pointer, 32), 0u);
    a.deallocate(pointer, 32, 32);
}

TEST(HeapAllocator, CreateAndDestroyRunConstructorsAndDestructors) {
    dg::HeapAllocator heap;
    Tracked::liveCount = 0;
    Tracked::destructorCalls = 0;
    Tracked* first = heap.create<Tracked>(7);
    Tracked* second = heap.create<Tracked>(9);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(first->value, 7);
    EXPECT_EQ(second->value, 9);
    EXPECT_EQ(Tracked::liveCount, 2);
    EXPECT_EQ(heap.bytesInUse(), 2 * sizeof(Tracked));
    heap.destroy(first);
    heap.destroy(second);
    heap.destroy<Tracked>(nullptr);
    EXPECT_EQ(Tracked::liveCount, 0);
    EXPECT_EQ(Tracked::destructorCalls, 2);
    EXPECT_EQ(heap.bytesInUse(), 0u);
    EXPECT_EQ(heap.allocationCount(), 0u);

    OverAligned* overAligned = heap.create<OverAligned>();
    ASSERT_NE(overAligned, nullptr);
    EXPECT_EQ(misalignment(overAligned, 64), 0u);
    heap.destroy(overAligned);

    auto* text = heap.create<std::string>("a string long enough to defeat the small-string optimization");
    ASSERT_NE(text, nullptr);
    EXPECT_GT(text->size(), 40u);
    heap.destroy(text); // leaks of the string's buffer would show up under ASan
    EXPECT_EQ(heap.bytesInUse(), 0u);
}

TEST(LinearAllocator, AlignsEveryAllocationInsideItsBuffer) {
    alignas(256) std::array<std::byte, 8192> buffer{};
    // Start one byte in so the arena base itself is misaligned: alignment must use absolute addresses.
    dg::LinearAllocator arena(std::span<std::byte>(buffer).subspan(1));
    EXPECT_EQ(arena.capacity(), buffer.size() - 1);
    for (int round = 0; round < 3; ++round) {
        for (usize alignment : kAlignments) {
            SCOPED_TRACE(testing::Message() << "round " << round << ", alignment " << alignment);
            auto* pointer = static_cast<std::byte*>(arena.allocate(3, alignment));
            ASSERT_NE(pointer, nullptr);
            EXPECT_EQ(misalignment(pointer, alignment), 0u);
            EXPECT_GE(pointer, buffer.data() + 1);
            EXPECT_LE(pointer + 3, buffer.data() + buffer.size());
        }
    }
    EXPECT_EQ(arena.used() + arena.remaining(), arena.capacity());
    EXPECT_EQ(arena.allocate(0), nullptr);
}

TEST(LinearAllocator, MarkersRewindResetAndExhaustion) {
    alignas(16) std::array<std::byte, 256> buffer{};
    dg::LinearAllocator arena{std::span<std::byte>(buffer)};

    void* a = arena.allocate(16, 16);
    EXPECT_EQ(a, buffer.data());
    const dg::LinearAllocator::Marker marker = arena.mark();
    EXPECT_EQ(marker.offset, 16u);
    void* b = arena.allocate(64, 16);
    EXPECT_EQ(b, buffer.data() + 16);
    EXPECT_EQ(arena.used(), 80u);

    arena.rewind(marker);
    EXPECT_EQ(arena.used(), 16u);
    void* c = arena.allocate(64, 16);
    EXPECT_EQ(c, b); // rewinding makes the same memory available again, deterministically

    // Exhaustion: nullptr, no state change, and a request that still fits succeeds.
    const usize before = arena.used();
    EXPECT_EQ(arena.allocate(arena.remaining() + 1, 1), nullptr);
    EXPECT_EQ(arena.used(), before);
    EXPECT_EQ(arena.allocate(static_cast<usize>(-1), 1), nullptr); // no overflow in the bounds check
    void* rest = arena.allocate(arena.remaining(), 1);
    EXPECT_NE(rest, nullptr);
    EXPECT_EQ(arena.remaining(), 0u);
    EXPECT_EQ(arena.allocate(1, 1), nullptr);

    arena.deallocate(rest, 1, 1); // no-op
    EXPECT_EQ(arena.remaining(), 0u);
    arena.reset();
    EXPECT_EQ(arena.used(), 0u);
    EXPECT_EQ(arena.remaining(), buffer.size());
    EXPECT_EQ(arena.allocate(8, 8), buffer.data());
}

TEST(LinearAllocator, PaddingThatDoesNotFitFailsCleanly) {
    alignas(64) std::array<std::byte, 64> buffer{};
    dg::LinearAllocator arena{std::span<std::byte>(buffer)};
    EXPECT_NE(arena.allocate(1, 1), nullptr);
    // 63 bytes remain, but aligning to 64 needs 63 bytes of padding and leaves none for the object.
    EXPECT_EQ(arena.allocate(1, 64), nullptr);
    EXPECT_EQ(arena.used(), 1u);
}

TEST(LinearAllocator, OwnsItsBackingBlockAcrossMoves) {
    dg::HeapAllocator heap;
    {
        dg::LinearAllocator arena(1024, heap);
        EXPECT_EQ(arena.capacity(), 1024u);
        EXPECT_EQ(heap.bytesInUse(), 1024u);
        EXPECT_EQ(heap.allocationCount(), 1u);
        void* first = arena.allocate(100);
        EXPECT_EQ(misalignment(first, dg::kDefaultAlignment), 0u);

        dg::LinearAllocator moved(std::move(arena));
        EXPECT_EQ(moved.capacity(), 1024u);
        EXPECT_EQ(moved.used(), 100u);
        EXPECT_EQ(arena.capacity(), 0u); // NOLINT(bugprone-use-after-move): documented state
        EXPECT_EQ(arena.used(), 0u);     // NOLINT(bugprone-use-after-move)
        EXPECT_EQ(arena.allocate(1), nullptr);

        dg::LinearAllocator assigned(512, heap);
        EXPECT_EQ(heap.allocationCount(), 2u);
        assigned = std::move(moved); // releases the 512-byte block
        EXPECT_EQ(heap.allocationCount(), 1u);
        EXPECT_EQ(heap.bytesInUse(), 1024u);
        EXPECT_EQ(assigned.used(), 100u);

        auto* tracked = assigned.create<int>(42);
        ASSERT_NE(tracked, nullptr);
        EXPECT_EQ(*tracked, 42);
    }
    EXPECT_EQ(heap.bytesInUse(), 0u);
    EXPECT_EQ(heap.allocationCount(), 0u);

    dg::LinearAllocator empty;
    EXPECT_EQ(empty.capacity(), 0u);
    EXPECT_EQ(empty.allocate(1), nullptr);
    dg::LinearAllocator zero(0, heap);
    EXPECT_EQ(zero.capacity(), 0u);
    EXPECT_EQ(heap.allocationCount(), 0u);
}

TEST(PoolAllocator, GrowsByChunksAndRecyclesMostRecentlyFreedFirst) {
    dg::HeapAllocator heap;
    {
        dg::PoolAllocator pool(24, 8, 4, heap);
        EXPECT_EQ(pool.blockSize(), 24u);
        EXPECT_EQ(pool.capacityBlocks(), 0u);
        EXPECT_EQ(pool.usedBlocks(), 0u);
        EXPECT_EQ(pool.chunkCount(), 0u);

        std::vector<void*> blocks;
        std::set<std::uintptr_t> unique;
        for (int i = 0; i < 4; ++i) {
            SCOPED_TRACE(testing::Message() << "block " << i);
            void* block = pool.allocate(24, 8);
            ASSERT_NE(block, nullptr);
            EXPECT_EQ(misalignment(block, 8), 0u);
            EXPECT_TRUE(pool.owns(block));
            std::memset(block, i, 24);
            blocks.push_back(block);
            unique.insert(address(block));
        }
        EXPECT_EQ(pool.chunkCount(), 1u);
        EXPECT_EQ(pool.capacityBlocks(), 4u);
        EXPECT_EQ(pool.usedBlocks(), 4u);
        // Within a fresh chunk blocks come out in ascending address order.
        for (usize i = 1; i < blocks.size(); ++i) {
            EXPECT_GT(address(blocks[i]), address(blocks[i - 1])) << "block " << i;
        }

        void* fifth = pool.allocate(16, 8); // smaller requests are fine
        ASSERT_NE(fifth, nullptr);
        unique.insert(address(fifth));
        blocks.push_back(fifth);
        EXPECT_EQ(pool.chunkCount(), 2u);
        EXPECT_EQ(pool.capacityBlocks(), 8u);
        EXPECT_EQ(pool.usedBlocks(), 5u);
        EXPECT_EQ(unique.size(), 5u);
        EXPECT_EQ(heap.allocationCount(), 2u);

        pool.deallocate(blocks[1], 24, 8);
        pool.deallocate(blocks[3], 24, 8);
        EXPECT_EQ(pool.usedBlocks(), 3u);
        EXPECT_EQ(pool.allocate(24, 8), blocks[3]); // most recently freed first
        EXPECT_EQ(pool.allocate(24, 8), blocks[1]);
        EXPECT_EQ(pool.usedBlocks(), 5u);
        EXPECT_EQ(pool.capacityBlocks(), 8u);

        int stackValue = 0;
        EXPECT_FALSE(pool.owns(&stackValue));
        EXPECT_FALSE(pool.owns(static_cast<std::byte*>(blocks[0]) + 1)); // interior pointers are not blocks
        // Zero-size requests are nullptr even when their (default, 16) alignment exceeds the 8-aligned blocks.
        EXPECT_EQ(pool.allocate(0), nullptr);
        EXPECT_EQ(pool.allocate(0, 256), nullptr);
        pool.deallocate(nullptr, 24, 8);
    }
    // The destructor released every chunk.
    EXPECT_EQ(heap.bytesInUse(), 0u);
    EXPECT_EQ(heap.allocationCount(), 0u);
}

// Regression: kDefaultAlignment used to be alignof(std::max_align_t), which is 8 on Apple ARM64 but 16 on Linux,
// Android and x86_64. A default-alignment request then fit an 8-aligned pool only on some platforms, and arena
// layouts differed between targets.
TEST(DefaultAlignment, IsTheSameOnEveryPlatform) {
    static_assert(dg::kDefaultAlignment == 16);
    static_assert(dg::kDefaultAlignment >= alignof(std::max_align_t));

    dg::HeapAllocator heap;
    for (int i = 0; i < 16; ++i) {
        SCOPED_TRACE(testing::Message() << "size " << i + 1);
        void* pointer = heap.allocate(static_cast<usize>(i) + 1);
        ASSERT_NE(pointer, nullptr);
        EXPECT_EQ(misalignment(pointer, 16), 0u);
        heap.deallocate(pointer, static_cast<usize>(i) + 1, dg::kDefaultAlignment);
    }

    // Arena layout with default alignment is a fixed function of the request sequence.
    alignas(16) std::array<std::byte, 64> buffer{};
    dg::LinearAllocator arena{std::span<std::byte>(buffer)};
    EXPECT_EQ(arena.allocate(1), buffer.data());
    EXPECT_EQ(arena.allocate(1), buffer.data() + 16);
    EXPECT_EQ(arena.used(), 17u);
    EXPECT_EQ(arena.allocate(32), buffer.data() + 32);
    EXPECT_EQ(arena.allocate(1), nullptr); // exhausted at the same point everywhere

    // A pool with the default block alignment accepts default-alignment requests on every platform.
    dg::PoolAllocator pool(24, dg::kDefaultAlignment, 4, heap);
    EXPECT_EQ(pool.blockAlignment(), 16u);
    void* block = pool.allocate(24);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(misalignment(block, 16), 0u);
    pool.deallocate(block, 24, dg::kDefaultAlignment);
}

TEST(PoolAllocator, HonoursEveryBlockAlignment) {
    dg::HeapAllocator heap;
    for (usize alignment : kAlignments) {
        SCOPED_TRACE(testing::Message() << "alignment " << alignment);
        dg::PoolAllocator pool(alignment * 3 / 2 + 1, alignment, 3, heap);
        EXPECT_GE(pool.blockAlignment(), alignment);
        std::vector<void*> blocks;
        for (int i = 0; i < 10; ++i) {
            void* block = pool.allocate(pool.blockSize(), alignment);
            ASSERT_NE(block, nullptr) << "block " << i;
            EXPECT_EQ(misalignment(block, alignment), 0u) << "block " << i;
            std::memset(block, 0xCD, pool.blockSize());
            blocks.push_back(block);
        }
        EXPECT_EQ(pool.chunkCount(), 4u);
        EXPECT_EQ(pool.capacityBlocks(), 12u);
        for (void* block : blocks) {
            pool.deallocate(block, pool.blockSize(), alignment);
        }
        EXPECT_EQ(pool.usedBlocks(), 0u);
    }
    EXPECT_EQ(heap.bytesInUse(), 0u);
}

TEST(PoolAllocator, ObjectHelpersAndMovesKeepObjectsAlive) {
    dg::HeapAllocator heap;
    Tracked::liveCount = 0;
    {
        dg::PoolAllocator pool(sizeof(std::string), alignof(std::string), 2, heap);
        auto* a = pool.create<std::string>("first string that is long enough to live on the heap");
        auto* b = pool.create<std::string>("second string that is long enough to live on the heap");
        auto* c = pool.create<std::string>("third string that is long enough to live on the heap");
        ASSERT_NE(a, nullptr);
        ASSERT_NE(b, nullptr);
        ASSERT_NE(c, nullptr);
        EXPECT_EQ(*b, "second string that is long enough to live on the heap");
        pool.destroy(a);
        pool.destroy(b);
        pool.destroy(c);
        EXPECT_EQ(pool.usedBlocks(), 0u);

        dg::PoolAllocator moved(std::move(pool));
        EXPECT_EQ(moved.capacityBlocks(), 4u);
        EXPECT_EQ(moved.chunkCount(), 2u);
        EXPECT_EQ(pool.capacityBlocks(), 0u); // NOLINT(bugprone-use-after-move)
        EXPECT_EQ(pool.allocate(8), nullptr); // NOLINT(bugprone-use-after-move)
        Tracked* tracked = moved.create<Tracked>(3);
        ASSERT_NE(tracked, nullptr);
        EXPECT_EQ(Tracked::liveCount, 1);
        moved.destroy(tracked);
        EXPECT_EQ(Tracked::liveCount, 0);
    }
    EXPECT_EQ(heap.bytesInUse(), 0u);
    EXPECT_EQ(heap.allocationCount(), 0u);
}

#if !DG_ASSERTS_ENABLED
// With asserts compiled out the contract still holds: misuse returns nullptr instead of corrupting memory.
TEST(UncheckedAllocators, InvalidRequestsReturnNullptr) {
    dg::HeapAllocator heap;
    EXPECT_EQ(heap.allocate(16, 3), nullptr);
    dg::PoolAllocator pool(16, 16, 4, heap);
    EXPECT_EQ(pool.allocate(17, 16), nullptr);
    EXPECT_EQ(pool.allocate(16, 32), nullptr);
    EXPECT_EQ(pool.usedBlocks(), 0u);
    alignas(16) std::array<std::byte, 64> buffer{};
    dg::LinearAllocator arena{std::span<std::byte>(buffer)};
    EXPECT_EQ(arena.allocate(8, 0), nullptr);
}
#endif

#if DG_ASSERTS_ENABLED
// Checked builds turn the misuse above into a hard stop. Each statement runs in a re-executed child process
// ("threadsafe" style), so the allocations it makes before dying never leak into the parent.
// The misuse lives in lambdas because EXPECT_DEATH is a macro and would split statements at their commas.
// Each matcher names the specific DG_ASSERT_MSG text, so a test only passes if the intended contract fired.

TEST(HeapAllocatorDeath, RejectsNonPowerOfTwoAlignment) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    dg::HeapAllocator heap;
    EXPECT_DEATH((void)heap.allocate(16, 3), "Assertion failed.*allocation alignment must be a power of two");
}

TEST(HeapAllocatorDeath, RejectsNonPowerOfTwoDeallocationAlignment) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    const auto misuse = [] {
        dg::HeapAllocator heap;
        void* pointer = heap.allocate(16, 16);
        heap.deallocate(pointer, 16, 3);
    };
    EXPECT_DEATH(misuse(), "Assertion failed.*deallocation alignment must be a power of two");
}

TEST(HeapAllocatorDeath, RejectsDeallocateOfBlockItDidNotAllocate) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    const auto misuse = [] {
        dg::HeapAllocator owner;
        dg::HeapAllocator stranger;
        void* pointer = owner.allocate(32, 16);
        stranger.deallocate(pointer, 32, 16);
    };
    EXPECT_DEATH(misuse(), "Assertion failed.*deallocate does not match a live allocation");
}

TEST(LinearAllocatorDeath, RejectsZeroAlignment) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    alignas(16) std::array<std::byte, 64> buffer{};
    dg::LinearAllocator arena{std::span<std::byte>(buffer)};
    EXPECT_DEATH((void)arena.allocate(8, 0), "Assertion failed.*allocation alignment must be a power of two");
}

TEST(LinearAllocatorDeath, RejectsNonPowerOfTwoAlignment) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    alignas(16) std::array<std::byte, 64> buffer{};
    dg::LinearAllocator arena{std::span<std::byte>(buffer)};
    EXPECT_DEATH((void)arena.allocate(8, 12), "Assertion failed.*allocation alignment must be a power of two");
}

TEST(LinearAllocatorDeath, RejectsRewindToNewerMarker) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    alignas(16) std::array<std::byte, 64> buffer{};
    dg::LinearAllocator arena{std::span<std::byte>(buffer)};
    (void)arena.allocate(32, 16);
    const dg::LinearAllocator::Marker newer = arena.mark();
    arena.reset();
    EXPECT_DEATH(arena.rewind(newer), "Assertion failed.*rewind to a marker newer than the arena position");
}

TEST(PoolAllocatorDeath, RejectsNonPowerOfTwoBlockAlignment) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    const auto misuse = [] {
        dg::HeapAllocator heap;
        const dg::PoolAllocator pool(16, 3, 4, heap);
    };
    EXPECT_DEATH(misuse(), "Assertion failed.*pool block alignment must be a power of two");
}

TEST(PoolAllocatorDeath, RejectsEmptyBlocks) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    const auto misuse = [] {
        dg::HeapAllocator heap;
        const dg::PoolAllocator pool(0, 16, 4, heap);
    };
    EXPECT_DEATH(misuse(), "Assertion failed.*pool blocks and chunks must be non-empty");
}

TEST(PoolAllocatorDeath, RejectsEmptyChunks) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    const auto misuse = [] {
        dg::HeapAllocator heap;
        const dg::PoolAllocator pool(16, 16, 0, heap);
    };
    EXPECT_DEATH(misuse(), "Assertion failed.*pool blocks and chunks must be non-empty");
}

TEST(PoolAllocatorDeath, RejectsRequestLargerThanBlock) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    dg::HeapAllocator heap;
    dg::PoolAllocator pool(16, 16, 4, heap);
    EXPECT_DEATH((void)pool.allocate(17, 16), "Assertion failed.*allocation is larger than the pool block size");
}

TEST(PoolAllocatorDeath, RejectsRequestMoreAlignedThanBlocks) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    dg::HeapAllocator heap;
    dg::PoolAllocator pool(16, 16, 4, heap);
    EXPECT_DEATH((void)pool.allocate(16, 32), "Assertion failed.*allocation is more aligned than the pool blocks");
}

TEST(PoolAllocatorDeath, RejectsNonPowerOfTwoRequestAlignment) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    dg::HeapAllocator heap;
    dg::PoolAllocator pool(16, 16, 4, heap);
    EXPECT_DEATH((void)pool.allocate(8, 3), "Assertion failed.*allocation alignment must be a power of two");
}

TEST(PoolAllocatorDeath, RejectsForeignPointer) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    const auto misuse = [] {
        dg::HeapAllocator heap;
        dg::PoolAllocator pool(16, 16, 4, heap);
        (void)pool.allocate(16, 16); // gives the pool a chunk, so ownership is actually checked against one
        alignas(16) std::array<std::byte, 16> foreign{};
        pool.deallocate(foreign.data(), 16, 16);
    };
    EXPECT_DEATH(misuse(), "Assertion failed.*pointer was not allocated from this pool");
}

TEST(PoolAllocatorDeath, RejectsDoubleFree) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    const auto misuse = [] {
        dg::HeapAllocator heap;
        dg::PoolAllocator pool(16, 16, 4, heap);
        void* block = pool.allocate(16, 16);
        pool.deallocate(block, 16, 16);
        pool.deallocate(block, 16, 16); // still owned, but no block is live any more
    };
    EXPECT_DEATH(misuse(), "Assertion failed.*pool deallocate without a live block");
}
#endif
