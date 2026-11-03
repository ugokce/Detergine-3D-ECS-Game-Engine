#include "dg/sim/Simulation.h"

#include <array>
#include <cstdio>

using namespace dg;

namespace {

int gFailures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        ++gFailures;
    }
}

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

} // namespace

int main() {
    const u64 a = runScripted(0x5eed, 1800, 2);
    const u64 b = runScripted(0x5eed, 1800, 2);
    check(a == b, "two runs with the same seed and inputs produce the same state hash");
    check(runScripted(0x5eee, 1800, 2) != a, "a different seed produces a different state hash");
    check(runScripted(0x5eed, 1800, 3) != a, "different inputs produce a different state hash");

    std::printf("state-hash after 1800 ticks: 0x%016llx\n", static_cast<unsigned long long>(a));
    check(a == kGoldenHash, "state hash matches the cross-platform golden value");

    if (gFailures == 0) {
        std::printf("all determinism checks passed\n");
    }
    return gFailures == 0 ? 0 : 1;
}
