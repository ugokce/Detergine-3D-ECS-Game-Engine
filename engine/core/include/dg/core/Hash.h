#pragma once

#include "dg/core/Types.h"

#include <array>
#include <span>
#include <type_traits>

namespace dg {

// Incremental FNV-1a (64-bit). Stand-in for XXH3 until vcpkg dependencies land (DESIGN.md §2.1); state hashes will
// switch over then, which changes every recorded golden hash.
class Fnv1a64 {
public:
    constexpr void update(std::span<const std::byte> bytes) {
        for (std::byte b : bytes) {
            state ^= static_cast<u64>(b);
            state *= kPrime;
        }
    }

    // Hashes a scalar's exact bit pattern. Restricted to scalars so struct padding can never leak into a hash.
    template <typename T>
        requires std::is_scalar_v<T>
    constexpr void updateValue(T value) {
        const auto bytes = std::bit_cast<std::array<std::byte, sizeof(T)>>(value);
        update(bytes);
    }

    [[nodiscard]] constexpr u64 digest() const { return state; }

private:
    static constexpr u64 kOffsetBasis = 14695981039346656037ull;
    static constexpr u64 kPrime = 1099511628211ull;

    u64 state = kOffsetBasis;
};

} // namespace dg
