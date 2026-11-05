#include "dg/sim/Simulation.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdio>

using namespace dg;

namespace {

u64 runScripted(u64 seed, u64 ticks, u32 players) {
    sim::Simulation simulation({.tickRate = 30, .seed = seed});
    std::array<sim::InputCommand, 8> inputs{};
    for (u64 t = 0; t < ticks; ++t) {
        for (u32 slot = 0; slot < players; ++slot) {
            inputs[slot] = sim::scriptedInput(t, slot);
        }
        simulation.step(std::span(inputs.data(), players));
    }
    return simulation.stateHash();
}

// Recorded on macos-arm64 (AppleClang). Every platform, architecture and build configuration must reproduce it
// exactly; a mismatch means the simulation is not cross-platform deterministic. Re-record only when the sim itself
// changes on purpose, and say so in the commit message.
constexpr u64 kGoldenHash = 0x9f504fe2ce4e5ee2ull;

constexpr u64 kSeed = 0x5eed;
constexpr u64 kTicks = 1800;
constexpr u32 kPlayers = 2;

} // namespace

TEST(SimDeterminism, SameSeedAndInputsProduceSameStateHash) {
    const u64 a = runScripted(kSeed, kTicks, kPlayers);
    const u64 b = runScripted(kSeed, kTicks, kPlayers);
    EXPECT_EQ(a, b) << "two runs with the same seed and inputs produce the same state hash: 0x" << std::hex << a
                    << " vs 0x" << b;
}

TEST(SimDeterminism, DifferentSeedProducesDifferentStateHash) {
    const u64 a = runScripted(kSeed, kTicks, kPlayers);
    const u64 other = runScripted(0x5eee, kTicks, kPlayers);
    EXPECT_NE(other, a) << "a different seed produces a different state hash: both 0x" << std::hex << a;
}

TEST(SimDeterminism, DifferentInputsProduceDifferentStateHash) {
    const u64 a = runScripted(kSeed, kTicks, kPlayers);
    const u64 other = runScripted(kSeed, kTicks, kPlayers + 1);
    EXPECT_NE(other, a) << "different inputs produce a different state hash: both 0x" << std::hex << a;
}

TEST(SimDeterminism, StateHashMatchesCrossPlatformGolden) {
    const u64 a = runScripted(kSeed, kTicks, kPlayers);
    // Logged on every run (not only on failure) so hashes from different machines can be compared side by side.
    std::printf("state-hash after %llu ticks: 0x%016llx\n", static_cast<unsigned long long>(kTicks),
                static_cast<unsigned long long>(a));
    // The actual value is printed so a deliberate sim change can be re-recorded from the failure output.
    EXPECT_EQ(a, kGoldenHash) << "state hash after " << std::dec << kTicks << " ticks is 0x" << std::hex << a
                              << ", expected the cross-platform golden value 0x" << kGoldenHash;
}

#if DG_ASSERTS_ENABLED

// A zero tick rate would make the fixed dt infinite; the constructor must reject it as a programmer error.
TEST(SimulationDeath, ZeroTickRateAsserts) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    EXPECT_DEATH(sim::Simulation({.tickRate = 0}), "Assertion failed: tickRate > 0");
}

// Player slots map onto bodies modulo the body count, so an empty body set cannot be stepped.
TEST(SimulationDeath, ZeroBodyCountAsserts) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    EXPECT_DEATH(sim::Simulation({.bodyCount = 0}), "Assertion failed: config\\.bodyCount > 0");
}

#endif
