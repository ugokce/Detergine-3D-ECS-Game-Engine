#include "dg/sim/Simulation.h"

#include "dg/core/Assert.h"
#include "dg/core/Hash.h"

namespace dg::sim {

namespace {

constexpr f32 kGravity = -9.81f;
constexpr f32 kArenaHalfExtent = 10.0f;
constexpr f32 kRestitution = 0.6f;
constexpr f32 kDamping = 0.995f;
constexpr f32 kMoveImpulse = 0.75f;
constexpr f32 kJumpSpeed = 6.0f;
constexpr u64 kRandomKickInterval = 30;

// Mirrors a coordinate back inside [-limit, limit] and reverses its velocity. Uses only + - * and comparisons, which are
// exact IEEE operations, so the result is identical on every platform.
void bounce(f32& position, f32& velocity, f32 limit) {
    if (position > limit) {
        position = limit - (position - limit);
        velocity = -velocity * kRestitution;
    } else if (position < -limit) {
        position = -limit - (position + limit);
        velocity = -velocity * kRestitution;
    }
}

// Asserts before the value is used: the fixed delta time is computed in the member initializer list.
u32 checkedTickRate(u32 tickRate) {
    DG_ASSERT(tickRate > 0);
    return tickRate;
}

} // namespace

Simulation::Simulation(const SimConfig& simConfig)
    : config(simConfig), dt(1.0f / static_cast<f32>(checkedTickRate(simConfig.tickRate))), rng(simConfig.seed) {
    DG_ASSERT(config.bodyCount > 0);

    bodies.resize(config.bodyCount);
    for (Body& body : bodies) {
        body.px = (rng.nextUnitFloat() * 2.0f - 1.0f) * kArenaHalfExtent;
        body.py = rng.nextUnitFloat() * kArenaHalfExtent;
        body.pz = (rng.nextUnitFloat() * 2.0f - 1.0f) * kArenaHalfExtent;
    }
}

void Simulation::step(std::span<const InputCommand> inputs) {
    // Player slot i drives body i (wrapping), in slot order.
    for (usize slot = 0; slot < inputs.size(); ++slot) {
        const InputCommand& input = inputs[slot];
        Body& body = bodies[slot % bodies.size()];
        body.vx += static_cast<f32>(input.moveX) * kMoveImpulse;
        body.vz += static_cast<f32>(input.moveY) * kMoveImpulse;
        if ((input.buttons & buttons::kJump) != 0 && body.py <= 0.05f) {
            body.vy = kJumpSpeed;
        }
    }

    if (currentTick % kRandomKickInterval == 0) {
        Body& body = bodies[rng.nextBounded(static_cast<u32>(bodies.size()))];
        body.vx += rng.nextUnitFloat() * 2.0f - 1.0f;
        body.vz += rng.nextUnitFloat() * 2.0f - 1.0f;
    }

    for (Body& body : bodies) {
        body.vy += kGravity * dt;
        body.vx *= kDamping;
        body.vz *= kDamping;

        body.px += body.vx * dt;
        body.py += body.vy * dt;
        body.pz += body.vz * dt;

        if (body.py < 0.0f) {
            body.py = -body.py;
            body.vy = -body.vy * kRestitution;
        }
        bounce(body.px, body.vx, kArenaHalfExtent);
        bounce(body.pz, body.vz, kArenaHalfExtent);
    }

    ++currentTick;
}

u64 Simulation::stateHash() const {
    Fnv1a64 hash;
    hash.updateValue(currentTick);
    hash.updateValue(rng.rawState());
    hash.updateValue(rng.rawIncrement());
    for (const Body& body : bodies) {
        for (f32 value : {body.px, body.py, body.pz, body.vx, body.vy, body.vz}) {
            hash.updateValue(value);
        }
    }
    return hash.digest();
}

} // namespace dg::sim
