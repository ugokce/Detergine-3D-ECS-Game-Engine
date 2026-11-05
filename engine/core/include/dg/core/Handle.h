#pragma once

#include "dg/core/Memory.h"
#include "dg/core/Types.h"

#include <compare>
#include <cstddef>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace dg {

// Generational handle: a slot index plus the generation the slot had when the handle was issued. Tag only makes
// handles to different kinds of objects distinct types. Generation 0 is never issued, so a zero-initialized handle
// (e.g. inside a zero-initialized sim component) is invalid, as is a default-constructed one.
template <typename Tag>
struct Handle {
    static constexpr u32 kInvalidIndex = std::numeric_limits<u32>::max();

    u32 index = kInvalidIndex;
    u32 generation = 0;

    // Structural validity only; whether the object still exists is a question for the pool that issued it.
    [[nodiscard]] constexpr bool isValid() const { return index != kInvalidIndex && generation != 0; }
    [[nodiscard]] static constexpr Handle invalid() { return Handle{}; }

    // Orders by index, then generation: a total order independent of memory addresses.
    friend constexpr bool operator==(const Handle&, const Handle&) = default;
    friend constexpr auto operator<=>(const Handle&, const Handle&) = default;
};

// Generational slot map. Objects live in fixed-size pages that never move, so a T* from get() stays valid until that
// object is destroyed (creating other objects does not invalidate it), and T needs neither default construction nor
// move support.
//
// Determinism: this pool will back the sim entity allocator, so handle values are part of the sim state. Every
// decision depends only on the sequence of create/destroy calls, never on addresses or allocator behaviour:
//  * fresh slots are taken in ascending index order;
//  * freed slots are reused FIFO (the oldest freed slot first), which also maximizes the time before a slot is reused
//    and thus the chance that stale handles are caught;
//  * generations start at 1 and grow by one per destroy. A slot whose generation would pass MaxGeneration is retired
//    forever instead of wrapping, so a stale handle can never alias a newer object.
// MaxGeneration exists so tests (and pools that pack handles into fewer bits) can reach retirement quickly.
//
// Re-entrancy: T's constructor and destructor may create and destroy *other* objects in the same pool (e.g. an
// entity spawning or despawning children). create() reserves its slot before running the constructor, so nested
// creates take the following slots, and destroy() keeps the slot off the free list until the destructor has
// returned, so nested creates cannot reuse it. forEach callbacks must not create or destroy.
//
// Not thread-safe.
template <typename T, u32 MaxGeneration = std::numeric_limits<u32>::max()>
class HandlePool {
    static_assert(MaxGeneration >= 1, "generation 0 is reserved for invalid handles");
    static_assert(std::is_destructible_v<T>);

public:
    using HandleType = Handle<T>;

    // Slots per page; pages are allocated from the pool's allocator on demand.
    static constexpr u32 kSlotsPerPage = 64;
    static constexpr u32 kMaxGeneration = MaxGeneration;

    explicit HandlePool(IAllocator& allocator = defaultAllocator()) : pageAllocator(&allocator) {}
    ~HandlePool() { releaseAll(); }

    HandlePool(const HandlePool&) = delete;
    HandlePool& operator=(const HandlePool&) = delete;

    // The moved-from pool is empty but usable; handles issued by it now refer to the destination.
    HandlePool(HandlePool&& other) noexcept
        : pageAllocator(other.pageAllocator), pages(std::exchange(other.pages, {})),
          slots(std::exchange(other.slots, {})), freeHead(std::exchange(other.freeHead, kNoSlot)),
          freeTail(std::exchange(other.freeTail, kNoSlot)), liveCount(std::exchange(other.liveCount, 0)) {}

    HandlePool& operator=(HandlePool&& other) noexcept {
        if (this != &other) {
            releaseAll();
            pageAllocator = other.pageAllocator;
            pages = std::exchange(other.pages, {});
            slots = std::exchange(other.slots, {});
            freeHead = std::exchange(other.freeHead, kNoSlot);
            freeTail = std::exchange(other.freeTail, kNoSlot);
            liveCount = std::exchange(other.liveCount, 0);
        }
        return *this;
    }

    // Constructs a T in the next slot (see the reuse order above). Returns an invalid handle if the index space is
    // exhausted or a page cannot be allocated.
    template <typename... Args>
    [[nodiscard]] HandleType create(Args&&... args) {
        u32 index = kNoSlot;
        if (freeHead != kNoSlot) {
            index = freeHead;
            freeHead = slots[index].nextFree;
            if (freeHead == kNoSlot) {
                freeTail = kNoSlot;
            }
            slots[index].nextFree = kNoSlot;
        } else {
            index = static_cast<u32>(slots.size());
            if (index == HandleType::kInvalidIndex) {
                return HandleType::invalid();
            }
            if (index / kSlotsPerPage >= pages.size() && !addPage()) {
                return HandleType::invalid();
            }
            slots.push_back(Slot{});
        }

        // The slot is now reserved (neither free nor alive), so a constructor that re-enters create() gets a
        // different slot. If the constructor unwinds, the slot goes back to the front of the free queue unchanged.
        SlotReservation reservation{this, index};
        std::construct_at(slotStorage(index), std::forward<Args>(args)...);
        reservation.pool = nullptr;

        Slot& slot = slots[index]; // re-indexed: nested creates may have grown the vector
        slot.alive = true;
        ++liveCount;
        return HandleType{index, slot.generation};
    }

    // Destroys the object. Returns false (and does nothing) if the handle is invalid or stale.
    bool destroy(HandleType handle) {
        if (!contains(handle)) {
            return false;
        }
        destroySlot(handle.index);
        return true;
    }

    // nullptr if the handle is invalid or stale.
    [[nodiscard]] T* get(HandleType handle) { return contains(handle) ? slotPointer(handle.index) : nullptr; }
    [[nodiscard]] const T* get(HandleType handle) const {
        return contains(handle) ? slotPointer(handle.index) : nullptr;
    }

    [[nodiscard]] bool contains(HandleType handle) const {
        if (handle.index >= slots.size()) {
            return false; // also rejects kInvalidIndex
        }
        const Slot& slot = slots[handle.index];
        return slot.alive && slot.generation == handle.generation;
    }

    // Number of live objects.
    [[nodiscard]] usize size() const { return liveCount; }
    [[nodiscard]] bool empty() const { return liveCount == 0; }
    // Number of slots ever used (live, free or retired); the exclusive upper bound of issued indices.
    [[nodiscard]] usize slotCount() const { return slots.size(); }

    // Destroys every live object in ascending slot index, exactly as if destroy() were called on each in that order:
    // generations advance (outstanding handles become stale) and the slots join the FIFO free list in index order.
    // Memory is kept for reuse.
    void clear() {
        for (u32 index = 0; index < slots.size(); ++index) {
            if (slots[index].alive) {
                destroySlot(index);
            }
        }
    }

    // Calls fn(T&) or fn(HandleType, T&) for each live object in ascending slot index. fn must not create or destroy
    // objects in this pool.
    template <typename Fn>
    void forEach(Fn&& fn) {
        forEachImpl(*this, fn);
    }
    template <typename Fn>
    void forEach(Fn&& fn) const {
        forEachImpl(*this, fn);
    }

private:
    static constexpr u32 kNoSlot = std::numeric_limits<u32>::max();

    struct Slot {
        u32 generation = 1;
        u32 nextFree = kNoSlot;
        bool alive = false;
    };

    // Returns an unfinished create()'s slot to the front of the free queue if the constructor unwinds.
    struct SlotReservation {
        HandlePool* pool;
        u32 index;

        SlotReservation(HandlePool* owner, u32 slotIndex) : pool(owner), index(slotIndex) {}
        SlotReservation(const SlotReservation&) = delete;
        SlotReservation& operator=(const SlotReservation&) = delete;
        ~SlotReservation() {
            if (pool != nullptr) {
                pool->slots[index].nextFree = pool->freeHead;
                pool->freeHead = index;
                if (pool->freeTail == kNoSlot) {
                    pool->freeTail = index;
                }
            }
        }
    };

    template <typename Self, typename Fn>
    static void forEachImpl(Self& self, Fn& fn) {
        for (u32 index = 0; index < self.slots.size(); ++index) {
            const Slot& slot = self.slots[index];
            if (!slot.alive) {
                continue;
            }
            using Object = std::conditional_t<std::is_const_v<Self>, const T, T>;
            Object& object = *self.slotPointer(index);
            if constexpr (std::is_invocable_v<Fn&, HandleType, Object&>) {
                fn(HandleType{index, slot.generation}, object);
            } else {
                fn(object);
            }
        }
    }

    // Raw storage for a slot that holds no object yet; only valid as the target of std::construct_at.
    [[nodiscard]] T* slotStorage(u32 index) const {
        std::byte* page = pages[index / kSlotsPerPage];
        return reinterpret_cast<T*>(page + static_cast<usize>(index % kSlotsPerPage) * sizeof(T));
    }

    // Pointer to the live object in a slot. std::launder requires a T within its lifetime at that address, so this
    // is only called for alive slots (or one whose destructor is about to run).
    [[nodiscard]] T* slotPointer(u32 index) const { return std::launder(slotStorage(index)); }

    bool addPage() {
        void* page = pageAllocator->allocate(sizeof(T) * kSlotsPerPage, alignof(T));
        if (page == nullptr) {
            return false;
        }
        pages.push_back(static_cast<std::byte*>(page));
        return true;
    }

    void destroySlot(u32 index) {
        // Mark dead before the destructor runs, but queue the slot only afterwards: a destructor that re-enters the
        // pool then sees the object as gone, yet cannot have the slot handed out while the object still exists.
        slots[index].alive = false;
        --liveCount;
        std::destroy_at(slotPointer(index));

        Slot& slot = slots[index]; // re-indexed: the destructor may have grown the vector
        if (slot.generation == MaxGeneration) {
            return; // retired: never enters the free list again
        }
        ++slot.generation;
        slot.nextFree = kNoSlot;
        if (freeTail == kNoSlot) {
            freeHead = index;
        } else {
            slots[freeTail].nextFree = index;
        }
        freeTail = index;
    }

    void releaseAll() noexcept {
        for (u32 index = 0; index < slots.size(); ++index) {
            if (slots[index].alive) {
                slots[index].alive = false; // a destructor that destroys a sibling must not destroy this one twice
                std::destroy_at(slotPointer(index));
            }
        }
        for (std::byte* page : pages) {
            pageAllocator->deallocate(page, sizeof(T) * kSlotsPerPage, alignof(T));
        }
        pages.clear();
        slots.clear();
        freeHead = kNoSlot;
        freeTail = kNoSlot;
        liveCount = 0;
    }

    IAllocator* pageAllocator;
    std::vector<std::byte*> pages;
    std::vector<Slot> slots;
    u32 freeHead = kNoSlot; // oldest freed slot
    u32 freeTail = kNoSlot; // most recently freed slot
    usize liveCount = 0;
};

} // namespace dg
