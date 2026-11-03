#pragma once

#include "dg/core/Types.h"

namespace dg::sim {

// Per-player, per-tick input: the only thing that flows into the sim (CLAUDE.md determinism rule 9).
// Placeholder layout until reflected InputCommand schemas land (DESIGN.md §14.3).
struct InputCommand {
    i8 moveX = 0; // -1, 0, 1
    i8 moveY = 0;
    u16 buttons = 0;
};

namespace buttons {
inline constexpr u16 kJump = 1u << 0;
} // namespace buttons

// Deterministic synthetic input for headless runs, smoke tests and determinism checks.
constexpr InputCommand scriptedInput(u64 tick, u32 playerSlot) {
    const u64 phase = tick + static_cast<u64>(playerSlot) * 17u;
    InputCommand input;
    input.moveX = static_cast<i8>(static_cast<i64>((phase / 45u) % 3u) - 1);
    input.moveY = static_cast<i8>(static_cast<i64>((phase / 70u) % 3u) - 1);
    input.buttons = (phase % 90u == 0u) ? buttons::kJump : u16{0};
    return input;
}

} // namespace dg::sim
