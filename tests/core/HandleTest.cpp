#include <gtest/gtest.h>

#include "dg/core/Handle.h"
#include "dg/core/Hash.h"
#include "dg/core/Memory.h"
#include "dg/core/Types.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <ios>
#include <string>
#include <utility>
#include <vector>

namespace {

using dg::u32;
using dg::u64;
using dg::usize;

struct Tracked {
    static inline int liveCount = 0;

    explicit Tracked(int v) : value(v) { ++liveCount; }
    ~Tracked() { --liveCount; }
    // Neither default-constructible nor movable: the pool must construct in place and never relocate.
    Tracked(const Tracked&) = delete;
    Tracked& operator=(const Tracked&) = delete;

    int value;
};

// Exactly 64 bytes so MSVC's /W4 "padded due to alignment specifier" warning (C4324) does not fire.
struct alignas(64) OverAligned {
    std::array<u32, 16> payload{};
};

std::string longString(int i) {
    return "a string that is long enough to need a heap buffer #" + std::to_string(i);
}

// Scripted create/destroy workload driven by a fixed LCG, so the handle sequence is a pure function of the code.
u64 scriptedHandleHash() {
    dg::HandlePool<u32> pool;
    std::vector<dg::Handle<u32>> live;
    dg::Fnv1a64 hash;
    u64 lcg = 0x9E3779B97F4A7C15ull;
    for (u32 step = 0; step < 4000; ++step) {
        lcg = lcg * 6364136223846793005ull + 1442695040888963407ull;
        const auto roll = static_cast<u32>(lcg >> 33u);
        if (live.empty() || roll % 5u < 3u) {
            const dg::Handle<u32> handle = pool.create(step);
            live.push_back(handle);
            hash.updateValue(handle.index);
            hash.updateValue(handle.generation);
        } else {
            const usize victim = (roll / 5u) % live.size();
            pool.destroy(live[victim]);
            live[victim] = live.back();
            live.pop_back();
        }
    }
    pool.forEach([&](dg::Handle<u32> handle, const u32& value) {
        hash.updateValue(handle.index);
        hash.updateValue(handle.generation);
        hash.updateValue(value);
    });
    hash.updateValue(static_cast<u64>(pool.size()));
    hash.updateValue(static_cast<u64>(pool.slotCount()));
    return hash.digest();
}

} // namespace

TEST(Handle, DefaultZeroAndInvalidHandlesAreNotValid) {
    constexpr dg::Handle<int> defaulted{};
    static_assert(!defaulted.isValid());
    static_assert(!dg::Handle<int>::invalid().isValid());
    static_assert(defaulted == dg::Handle<int>::invalid());
    constexpr dg::Handle<int> zero{0, 0}; // what a zero-initialized sim component holds
    static_assert(!zero.isValid());
    static_assert(dg::Handle<int>{0, 1}.isValid());
    static_assert(dg::Handle<int>{1, 1} > dg::Handle<int>{0, 7}); // index first
    static_assert(dg::Handle<int>{1, 2} > dg::Handle<int>{1, 1});
    static_assert(dg::Handle<int>{3, 4} != dg::Handle<int>{3, 5});
    static_assert(sizeof(dg::Handle<int>) == 8);

    dg::HandlePool<int> pool;
    EXPECT_FALSE(pool.contains(zero));
    EXPECT_EQ(pool.get(dg::Handle<int>::invalid()), nullptr);
    EXPECT_FALSE(pool.destroy(dg::Handle<int>::invalid()));
}

TEST(HandlePool, HandlesGoStaleAfterDestroy) {
    dg::HandlePool<Tracked> pool;
    Tracked::liveCount = 0;
    const auto a = pool.create(10);
    const auto b = pool.create(20);
    ASSERT_TRUE(a.isValid());
    ASSERT_TRUE(b.isValid());
    EXPECT_EQ(a.index, 0u);
    EXPECT_EQ(a.generation, 1u);
    EXPECT_EQ(b.index, 1u);
    EXPECT_EQ(b.generation, 1u);
    EXPECT_EQ(pool.size(), 2u);
    EXPECT_EQ(Tracked::liveCount, 2);
    ASSERT_NE(pool.get(a), nullptr);
    EXPECT_EQ(pool.get(a)->value, 10);
    EXPECT_EQ(pool.get(b)->value, 20);

    EXPECT_TRUE(pool.destroy(a));
    EXPECT_EQ(Tracked::liveCount, 1);
    EXPECT_FALSE(pool.contains(a));
    EXPECT_EQ(pool.get(a), nullptr);
    EXPECT_FALSE(pool.destroy(a)); // double destroy is rejected
    EXPECT_EQ(pool.size(), 1u);

    // A handle with the slot's next generation must not resolve to the dead slot either.
    EXPECT_EQ(pool.get(dg::Handle<Tracked>{0, 2}), nullptr);

    const auto c = pool.create(30);
    EXPECT_EQ(c.index, 0u);
    EXPECT_EQ(c.generation, 2u);
    EXPECT_EQ(pool.get(a), nullptr); // the old handle stays stale after reuse
    EXPECT_EQ(pool.get(c)->value, 30);
    EXPECT_EQ(pool.get(dg::Handle<Tracked>{99, 1}), nullptr);

    const dg::HandlePool<Tracked>& constPool = pool;
    ASSERT_NE(constPool.get(b), nullptr);
    EXPECT_EQ(constPool.get(b)->value, 20);
}

TEST(HandlePool, ReusesFreedSlotsInFifoOrder) {
    dg::HandlePool<int> pool;
    std::vector<dg::Handle<int>> handles;
    for (int i = 0; i < 6; ++i) {
        handles.push_back(pool.create(i));
    }
    EXPECT_TRUE(pool.destroy(handles[3]));
    EXPECT_TRUE(pool.destroy(handles[1]));
    EXPECT_TRUE(pool.destroy(handles[4]));

    const auto r0 = pool.create(100);
    const auto r1 = pool.create(101);
    EXPECT_EQ(r0.index, 3u);
    EXPECT_EQ(r0.generation, 2u);
    EXPECT_EQ(r1.index, 1u);
    EXPECT_EQ(r1.generation, 2u);
    // Freeing during the drain appends behind the remaining queue.
    EXPECT_TRUE(pool.destroy(handles[0]));
    const auto r2 = pool.create(102);
    const auto r3 = pool.create(103);
    const auto r4 = pool.create(104);
    EXPECT_EQ(r2.index, 4u);
    EXPECT_EQ(r2.generation, 2u);
    EXPECT_EQ(r3.index, 0u);
    EXPECT_EQ(r3.generation, 2u);
    EXPECT_EQ(r4.index, 6u); // queue empty: fresh slot
    EXPECT_EQ(r4.generation, 1u);
    EXPECT_EQ(pool.slotCount(), 7u);
    EXPECT_EQ(pool.size(), 7u);
}

TEST(HandlePool, RetiresSlotWhoseGenerationWouldWrap) {
    dg::HandlePool<int, 3> pool; // generations 1..3 per slot
    static_assert(dg::HandlePool<int, 3>::kMaxGeneration == 3);
    std::vector<dg::Handle<int>> issued;
    for (u32 expected = 1; expected <= 3; ++expected) {
        const auto h = pool.create(0);
        EXPECT_EQ(h.index, 0u);
        EXPECT_EQ(h.generation, expected);
        issued.push_back(h);
        EXPECT_TRUE(pool.destroy(h));
    }
    // Slot 0 is spent: the next create takes a fresh slot, and every old handle stays stale forever.
    const auto fresh = pool.create(1);
    EXPECT_EQ(fresh.index, 1u);
    EXPECT_EQ(fresh.generation, 1u);
    for (const auto& old : issued) {
        EXPECT_FALSE(pool.contains(old)) << "generation " << old.generation;
    }
    EXPECT_EQ(pool.slotCount(), 2u);
    EXPECT_EQ(pool.size(), 1u);

    // A retired slot is skipped even when other slots are queued around it.
    const auto other = pool.create(2);
    EXPECT_EQ(other.index, 2u);
    EXPECT_TRUE(pool.destroy(fresh));
    EXPECT_TRUE(pool.destroy(other));
    EXPECT_EQ(pool.create(3).index, 1u);
    EXPECT_EQ(pool.create(4).index, 2u);
    EXPECT_EQ(pool.create(5).index, 3u);

    // clear() retires through the same path.
    dg::HandlePool<int, 1> single;
    const auto once = single.create(7);
    single.clear();
    EXPECT_FALSE(single.contains(once));
    EXPECT_EQ(single.create(8).index, 1u);
}

TEST(HandlePool, ForEachVisitsLiveObjectsInAscendingSlotIndex) {
    dg::HandlePool<int> pool;
    std::vector<dg::Handle<int>> handles;
    for (int i = 0; i < 5; ++i) {
        handles.push_back(pool.create(i * 10));
    }
    pool.destroy(handles[1]);
    pool.destroy(handles[4]);
    pool.destroy(handles[0]);
    (void)pool.create(99); // reuses slot 1 (oldest freed), created last but visited second

    // Fixed-size capture instead of std::vector<int>::push_back: growing a vector<int> in this TU emits libc++'s
    // out-of-line relocation helper under Xcode's Debug hardening mode, and the linker then shares that copy with
    // the non-hardened GoogleTest library, whose own vector<int> growth it corrupts (ASan: heap-buffer-overflow).
    std::array<int, 3> values{};
    usize visitCount = 0;
    pool.forEach([&](int& value) {
        if (visitCount < values.size()) {
            values[visitCount] = value;
        }
        ++visitCount;
    });
    EXPECT_EQ(visitCount, 3u);
    EXPECT_EQ(values, (std::array<int, 3>{99, 20, 30}));

    std::vector<u32> indices;
    pool.forEach([&](dg::Handle<int> handle, int& value) {
        indices.push_back(handle.index);
        EXPECT_EQ(pool.get(handle), &value);
        value += 1;
    });
    EXPECT_EQ(indices, (std::vector<u32>{1, 2, 3}));

    const dg::HandlePool<int>& constPool = pool;
    int sum = 0;
    constPool.forEach([&](const int& value) { sum += value; });
    EXPECT_EQ(sum, 100 + 21 + 31);
}

TEST(HandlePool, ObjectsNeverMoveAndNonMovableTypesWork) {
    dg::HandlePool<Tracked> pool;
    Tracked::liveCount = 0;
    const auto first = pool.create(1);
    Tracked* address = pool.get(first);
    std::vector<dg::Handle<Tracked>> more;
    for (int i = 0; i < 500; ++i) { // spans many pages
        more.push_back(pool.create(i));
    }
    EXPECT_EQ(pool.get(first), address);
    EXPECT_EQ(address->value, 1);
    for (int i = 0; i < 500; ++i) {
        EXPECT_EQ(pool.get(more[static_cast<usize>(i)])->value, i);
    }
    EXPECT_EQ(Tracked::liveCount, 501);

    dg::HandlePool<OverAligned> aligned;
    for (u32 i = 0; i < 130; ++i) {
        const auto h = aligned.create();
        EXPECT_EQ(reinterpret_cast<std::uintptr_t>(aligned.get(h)) % 64, 0u) << "object " << i;
    }
}

TEST(HandlePool, DestroyClearAndDestructorReleaseNonTrivialObjects) {
    dg::HeapAllocator heap;
    Tracked::liveCount = 0;
    {
        dg::HandlePool<std::string> strings(heap);
        std::vector<dg::Handle<std::string>> handles;
        for (int i = 0; i < 200; ++i) {
            handles.push_back(strings.create(longString(i)));
        }
        EXPECT_EQ(*strings.get(handles[123]), longString(123));
        for (usize i = 0; i < handles.size(); i += 2) {
            EXPECT_TRUE(strings.destroy(handles[i])) << "handle " << i;
        }
        EXPECT_EQ(strings.size(), 100u);
        // Reused slots get freshly constructed strings.
        for (int i = 0; i < 50; ++i) {
            const auto h = strings.create(longString(1000 + i));
            EXPECT_EQ(h.index, static_cast<u32>(i) * 2);
            EXPECT_EQ(*strings.get(h), longString(1000 + i));
        }
        EXPECT_GT(heap.allocationCount(), 0u);
        // The remaining 150 strings are released by the pool destructor (ASan reports any leak).

        dg::HandlePool<Tracked> tracked(heap);
        std::vector<dg::Handle<Tracked>> trackedHandles;
        for (int i = 0; i < 70; ++i) {
            trackedHandles.push_back(tracked.create(i));
        }
        EXPECT_EQ(Tracked::liveCount, 70);
        tracked.clear();
        EXPECT_EQ(Tracked::liveCount, 0);
        EXPECT_EQ(tracked.size(), 0u);
        EXPECT_TRUE(tracked.empty());
        for (const auto& h : trackedHandles) {
            EXPECT_FALSE(tracked.contains(h)) << "index " << h.index;
        }
        // clear() queues slots in index order, so refills are deterministic.
        EXPECT_EQ(tracked.create(0).index, 0u);
        EXPECT_EQ(tracked.create(0).index, 1u);
        (void)tracked.create(0);
        EXPECT_EQ(Tracked::liveCount, 3);
    }
    EXPECT_EQ(Tracked::liveCount, 0);
    // Every page was returned.
    EXPECT_EQ(heap.bytesInUse(), 0u);
    EXPECT_EQ(heap.allocationCount(), 0u);
}

namespace {

// Objects that re-enter their own pool from the constructor and destructor, like entities spawning children.
struct Spawner {
    static inline dg::HandlePool<Spawner>* pool = nullptr;
    static inline int liveCount = 0;
    static inline int createsInDestructor = 0;

    Spawner(int v, int childCount) : value(v) {
        ++liveCount;
        for (int i = 0; i < childCount; ++i) {
            children.push_back(pool->create(v * 10 + i, 0));
        }
    }
    ~Spawner() {
        --liveCount;
        for (const auto& child : children) {
            pool->destroy(child);
        }
        // Enough creates to force the slot vector to reallocate while the destructor runs.
        for (int i = 0; i < createsInDestructor; ++i) {
            (void)pool->create(-1, 0);
        }
    }
    Spawner(const Spawner&) = delete;
    Spawner& operator=(const Spawner&) = delete;

    int value;
    std::vector<dg::Handle<Spawner>> children;
};

} // namespace

// Regression: create() used to pick its slot index before running the constructor and destroy() held a Slot&
// across the destructor, so nested create/destroy calls aliased slots or wrote to freed memory.
TEST(HandlePool, ConstructorsAndDestructorsMayReenterThePool) {
    dg::HeapAllocator heap;
    Spawner::liveCount = 0;
    Spawner::createsInDestructor = 0;
    {
        dg::HandlePool<Spawner> pool(heap);
        Spawner::pool = &pool;

        const auto parent = pool.create(1, 3);
        ASSERT_TRUE(parent.isValid());
        EXPECT_EQ(parent.index, 0u); // reserved before the constructor ran
        EXPECT_EQ(pool.size(), 4u);
        EXPECT_EQ(Spawner::liveCount, 4);
        const Spawner* parentObject = pool.get(parent);
        ASSERT_NE(parentObject, nullptr);
        EXPECT_EQ(parentObject->value, 1);
        for (usize i = 0; i < parentObject->children.size(); ++i) {
            SCOPED_TRACE(testing::Message() << "child " << i);
            const auto child = parentObject->children[i];
            EXPECT_EQ(child.index, static_cast<u32>(i) + 1);
            ASSERT_NE(pool.get(child), nullptr);
            EXPECT_EQ(pool.get(child)->value, 10 + static_cast<int>(i));
        }

        const auto children = parentObject->children;
        EXPECT_TRUE(pool.destroy(parent)); // destructor destroys the children
        EXPECT_EQ(pool.size(), 0u);
        EXPECT_EQ(Spawner::liveCount, 0);
        for (const auto& child : children) {
            EXPECT_FALSE(pool.contains(child)) << "index " << child.index;
        }
        // FIFO: children were queued during the parent's destructor, the parent's slot only after it returned.
        EXPECT_EQ(pool.create(2, 0).index, 1u);
        EXPECT_EQ(pool.create(3, 0).index, 2u);
        EXPECT_EQ(pool.create(4, 0).index, 3u);
        EXPECT_EQ(pool.create(5, 0).index, 0u);
        pool.clear();

        // A destructor that creates must not be handed its own, still-occupied slot, and must survive the slot
        // vector reallocating underneath destroy().
        Spawner::createsInDestructor = 200;
        const auto noisy = pool.create(6, 0);
        const usize slotsBefore = pool.slotCount();
        EXPECT_TRUE(pool.destroy(noisy));
        Spawner::createsInDestructor = 0;
        EXPECT_EQ(pool.size(), 200u);
        EXPECT_EQ(Spawner::liveCount, 200);
        EXPECT_GT(pool.slotCount(), slotsBefore);
        EXPECT_FALSE(pool.contains(noisy));
        usize visited = 0;
        pool.forEach([&](dg::Handle<Spawner> handle, const Spawner& object) {
            EXPECT_NE(handle, noisy);
            EXPECT_EQ(object.value, -1);
            ++visited;
        });
        EXPECT_EQ(visited, 200u);

        // Destructors that destroy siblings while the pool itself is being torn down.
        const auto nestedParent = pool.create(7, 2);
        EXPECT_NE(pool.get(nestedParent), nullptr);
    }
    EXPECT_EQ(Spawner::liveCount, 0);
    EXPECT_EQ(heap.bytesInUse(), 0u);
    EXPECT_EQ(heap.allocationCount(), 0u);
    Spawner::pool = nullptr;
}

#if defined(__cpp_exceptions)
namespace {
struct ThrowOnZero {
    explicit ThrowOnZero(int v) : value(v) {
        if (v == 0) {
            throw 1;
        }
    }
    int value;
};
} // namespace

TEST(HandlePool, ThrowingConstructorLeavesTheSlotReusable) {
    dg::HandlePool<ThrowOnZero> pool;
    const auto a = pool.create(1);
    pool.destroy(a);
    EXPECT_THROW((void)pool.create(0), int);
    EXPECT_EQ(pool.size(), 0u);
    const auto b = pool.create(2); // the reserved slot went back to the front of the queue unchanged
    EXPECT_EQ(b.index, 0u);
    EXPECT_EQ(b.generation, 2u);
    try {
        (void)pool.create(0); // fresh slot this time
    } catch (int) {
    }
    const auto c = pool.create(3);
    EXPECT_EQ(c.index, 1u);
    EXPECT_EQ(c.generation, 1u);
    EXPECT_EQ(pool.size(), 2u);
}
#endif

TEST(HandlePool, MovingAPoolTransfersItsObjectsAndHandles) {
    dg::HeapAllocator heap;
    {
        dg::HandlePool<std::string> source(heap);
        const auto a = source.create(longString(1));
        const auto b = source.create(longString(2));
        source.destroy(a);

        dg::HandlePool<std::string> moved(std::move(source));
        EXPECT_EQ(moved.size(), 1u);
        EXPECT_EQ(*moved.get(b), longString(2));
        EXPECT_EQ(source.size(), 0u);                     // NOLINT(bugprone-use-after-move)
        EXPECT_EQ(source.get(b), nullptr);                // NOLINT(bugprone-use-after-move)
        EXPECT_EQ(moved.create(longString(3)).index, 0u); // FIFO state moved too

        dg::HandlePool<std::string> assigned(heap);
        (void)assigned.create(longString(4));
        assigned = std::move(moved);
        EXPECT_EQ(assigned.size(), 2u);
        EXPECT_EQ(*assigned.get(b), longString(2));

        const auto reused = source.create(longString(5)); // moved-from pool is still usable
        EXPECT_EQ(reused.index, 0u);
        EXPECT_EQ(reused.generation, 1u);
    }
    EXPECT_EQ(heap.bytesInUse(), 0u);
    EXPECT_EQ(heap.allocationCount(), 0u);
}

TEST(HandlePool, ScriptedCreateDestroySequenceMatchesGoldenHash) {
    // Recorded on macOS ARM64; it depends only on the FIFO/generation rules, so every platform must reproduce it.
    constexpr u64 kGolden = 0x3a7ea63a09700779ull;
    const u64 first = scriptedHandleHash();
    const u64 second = scriptedHandleHash();
    EXPECT_EQ(first, second) << std::hex << "first 0x" << first << ", second 0x" << second;
    std::printf("handle golden hash: 0x%016llx\n", static_cast<unsigned long long>(first));
    EXPECT_EQ(first, kGolden) << "handle allocation order changed: this changes sim entity ids (got 0x" << std::hex
                              << first << ", golden 0x" << kGolden << ")";
}
