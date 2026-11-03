#pragma once

#include "dg/core/Types.h"
#include "dg/sim/InputCommand.h"
#include "dg/sim/Rng.h"

#include <span>
#include <vector>

namespace dg::sim {

struct SimConfig {
    u32 tickRate = 30;
    u64 seed = 0x5eedu;
    u32 bodyCount = 64;
};

// Fixed-tick deterministic simulation. Until dg::ecs lands (roadmap phase 1) it simulates a small set of bodies so the
// build, the fixed-tick loop and cross-config/cross-platform determinism checks have something real to exercise.
class Simulation {
public:
    explicit Simulation(const SimConfig& config);

    // Advances exactly one tick. `inputs` holds one command per player slot.
    void step(std::span<const InputCommand> inputs);

    [[nodiscard]] u64 tick() const { return currentTick; }
    [[nodiscard]] f32 fixedDeltaTime() const { return dt; }

    // Hash of the complete sim state (tick, RNG, bodies) from exact bit patterns. Equal hashes on two machines mean
    // their simulations are bit-identical.
    [[nodiscard]] u64 stateHash() const;

private:
    struct Body {
        f32 px = 0, py = 0, pz = 0;
        f32 vx = 0, vy = 0, vz = 0;
    };

    SimConfig config;
    f32 dt;
    u64 currentTick = 0;
    Rng rng;
    std::vector<Body> bodies;
};

} // namespace dg::sim
