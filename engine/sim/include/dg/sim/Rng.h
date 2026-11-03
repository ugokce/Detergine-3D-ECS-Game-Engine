#pragma once

#include "dg/core/Types.h"

namespace dg::sim {

// PCG32 (XSH-RR). The only random source allowed in sim code (CLAUDE.md determinism rule 5). Its state is part of the
// sim state: it is snapshotted and hashed with everything else.
class Rng {
public:
    constexpr explicit Rng(u64 seed = 0x853c49e6748fea9bull, u64 stream = 0xda3e39cb94b95bdbull)
        : increment((stream << 1u) | 1u) {
        nextU32();
        state += seed;
        nextU32();
    }

    constexpr u32 nextU32() {
        const u64 old = state;
        state = old * kMultiplier + increment;
        const auto xorShifted = static_cast<u32>(((old >> 18u) ^ old) >> 27u);
        const auto rotation = static_cast<u32>(old >> 59u);
        return (xorShifted >> rotation) | (xorShifted << ((~rotation + 1u) & 31u));
    }

    // Uniform in [0, bound) without modulo bias.
    constexpr u32 nextBounded(u32 bound) {
        const u32 threshold = (~bound + 1u) % bound;
        for (;;) {
            const u32 r = nextU32();
            if (r >= threshold) {
                return r % bound;
            }
        }
    }

    // Uniform in [0, 1) built from 24 random bits: exact in f32, so identical on every platform.
    constexpr f32 nextUnitFloat() { return static_cast<f32>(nextU32() >> 8u) * (1.0f / 16777216.0f); }

    [[nodiscard]] constexpr u64 rawState() const { return state; }
    [[nodiscard]] constexpr u64 rawIncrement() const { return increment; }

private:
    static constexpr u64 kMultiplier = 6364136223846793005ull;

    u64 state = 0;
    u64 increment;
};

} // namespace dg::sim
