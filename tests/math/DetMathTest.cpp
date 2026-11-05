#include <gtest/gtest.h>

#include "dg/core/Hash.h"
#include "dg/math/DetMath.h"

#include <bit>
#include <cinttypes>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <ostream>
#include <string>
#include <vector>

// The C library is used here only as an accuracy reference (in f64, so its own error is negligible next to an f32
// ulp). It is never part of a determinism check: golden hashes cover detmath outputs alone.

namespace {

using dg::f32;
using dg::f64;
using dg::i32;
using dg::u32;
using dg::u64;
namespace dm = dg::detmath;

f32 fromBits(u32 bits) { return std::bit_cast<f32>(bits); }
u32 toBits(f32 x) { return std::bit_cast<u32>(x); }
bool isNaN(f32 x) { return (toBits(x) & 0x7FFFFFFFu) > 0x7F800000u; }

constexpr u32 kCanonicalNaN = 0x7FC00000u;
const f32 kInf = std::numeric_limits<f32>::infinity();

// The bit pattern of an f32. Comparing these with EXPECT_EQ is a bitwise check (signed zeros and NaN payloads count),
// and PrintTo shows the pattern in hex next to the value so a failure is readable.
struct Bits {
    u32 value;
    friend bool operator==(const Bits&, const Bits&) = default;
};
Bits bitsOf(f32 x) { return Bits{toBits(x)}; }
const Bits kCanonicalNaNBits{kCanonicalNaN};

void PrintTo(Bits bits, std::ostream* os) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "0x%08" PRIX32 " (%a)", bits.value, static_cast<f64>(fromBits(bits.value)));
    *os << buffer;
}

// Hex float formatting for failure messages: decimal hides the low mantissa bits that matter here.
std::string hexFloat(f32 x) {
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%a", static_cast<f64>(x));
    return buffer;
}

// SplitMix64: a tiny deterministic generator for test inputs (dg::sim::Rng lives in the sim module, which this suite
// does not link).
struct SplitMix {
    u64 state;
    u64 next() {
        u64 z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30u)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27u)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31u);
    }
    // Uniform in [lo, hi], computed in f64 and rounded to f32.
    f32 uniform(f64 lo, f64 hi) {
        const f64 unit = static_cast<f64>(next() >> 11u) * 0x1p-53;
        return static_cast<f32>(lo + (hi - lo) * unit);
    }
};

// Error of `value` against the f64 reference, in units of the f32 ulp at the reference.
f64 ulpError(f32 value, f64 reference) {
    if (std::isnan(reference)) {
        return isNaN(value) ? 0.0 : 1e30;
    }
    const f64 maxFinite = static_cast<f64>(std::numeric_limits<f32>::max());
    if (std::isinf(value) || std::fabs(reference) > maxFinite) {
        // Overflow region: correct when value equals the reference rounded to f32 (including to infinity).
        const f64 rounded = std::fabs(reference) >= 0x1.ffffffp127 ? std::copysign(HUGE_VAL, reference) : reference;
        return static_cast<f64>(value) == static_cast<f64>(static_cast<f32>(rounded)) ? 0.0 : 1e30;
    }
    int exponent = 0;
    std::frexp(reference, &exponent);
    const int ulpExponent = exponent - 24 < -149 ? -149 : exponent - 24;
    return std::fabs(static_cast<f64>(value) - reference) / std::ldexp(1.0, ulpExponent);
}

struct ErrorStats {
    const char* name;
    f64 maxUlp = 0.0;
    f32 worstX = 0.0f;
    f32 worstY = 0.0f;
    u64 samples = 0;

    void add(f64 error, f32 x, f32 y = 0.0f) {
        ++samples;
        if (error > maxUlp) {
            maxUlp = error;
            worstX = x;
            worstY = y;
        }
    }
    // One line with the measured maximum and where it occurred, used both as the progress log and the failure message.
    std::string describe() const {
        char buffer[160];
        std::snprintf(buffer, sizeof(buffer), "%s: max %.6f ulp over %" PRIu64 " samples (worst x=%a y=%a)", name,
                      maxUlp, samples, static_cast<f64>(worstX), static_cast<f64>(worstY));
        return buffer;
    }
    void print() const { std::printf("    %s\n", describe().c_str()); }
};

// Visits about `count` f32 values spread over the bit patterns of [lo, hi] (both positive), so every binade gets
// samples in proportion to its float count, plus the exact endpoints.
template <typename Fn>
void sweepBits(f32 lo, f32 hi, u32 count, Fn&& fn) {
    const u32 first = toBits(lo);
    const u32 last = toBits(hi);
    const u32 span = last - first;
    u32 stride = span / count;
    stride = stride == 0u ? 1u : stride | 1u; // odd, so successive samples hit different low mantissa bits
    for (u32 bits = first; bits < last; bits += stride) {
        fn(fromBits(bits));
        if (last - bits < stride) {
            break;
        }
    }
    fn(hi);
}

template <typename Fn, typename Ref>
ErrorStats measureUnary(const char* name, Fn&& fn, Ref&& ref, f32 lo, f32 hi, u32 count, bool alsoNegative) {
    ErrorStats stats{name};
    sweepBits(lo, hi, count, [&](f32 x) {
        stats.add(ulpError(fn(x), ref(static_cast<f64>(x))), x);
        if (alsoNegative) {
            stats.add(ulpError(fn(-x), ref(-static_cast<f64>(x))), -x);
        }
    });
    stats.print();
    return stats;
}

} // namespace

// --- Accuracy sweeps ------------------------------------------------------------------------------------------------

TEST(DetMathAccuracy, SinCosWithinTwoUlpUpTo1e5) {
    const auto sinStats = measureUnary(
        "sin", [](f32 x) { return dm::sin(x); }, [](f64 x) { return std::sin(x); }, 0x1p-149f, 1e5f, 3000000, true);
    const auto cosStats = measureUnary(
        "cos", [](f32 x) { return dm::cos(x); }, [](f64 x) { return std::cos(x); }, 0x1p-149f, 1e5f, 3000000, true);
    EXPECT_LE(sinStats.maxUlp, 2.0) << sinStats.describe();
    EXPECT_LE(cosStats.maxUlp, 2.0) << cosStats.describe();

    // Linear sweep: the bit sweep above spends most samples on tiny inputs, this one covers [0, 1e5] uniformly.
    ErrorStats linear{"sin/cos linear"};
    SplitMix rng{1};
    for (int i = 0; i < 2000000; ++i) {
        const f32 x = rng.uniform(-1e5, 1e5);
        linear.add(ulpError(dm::sin(x), std::sin(static_cast<f64>(x))), x);
        linear.add(ulpError(dm::cos(x), std::cos(static_cast<f64>(x))), x);
    }
    linear.print();
    EXPECT_LE(linear.maxUlp, 2.0) << linear.describe();
}

TEST(DetMathAccuracy, SinCosTanWithinTwoUlpForHugeArguments) {
    // Payne-Hanek path. Not part of the stated accuracy target, but the exact reduction should keep these as accurate
    // as small inputs.
    const auto sinStats = measureUnary(
        "sin huge", [](f32 x) { return dm::sin(x); }, [](f64 x) { return std::sin(x); }, 1e5f,
        std::numeric_limits<f32>::max(), 1000000, true);
    const auto cosStats = measureUnary(
        "cos huge", [](f32 x) { return dm::cos(x); }, [](f64 x) { return std::cos(x); }, 1e5f,
        std::numeric_limits<f32>::max(), 1000000, true);
    const auto tanStats = measureUnary(
        "tan huge", [](f32 x) { return dm::tan(x); }, [](f64 x) { return std::tan(x); }, 1e5f,
        std::numeric_limits<f32>::max(), 1000000, true);
    EXPECT_LE(sinStats.maxUlp, 2.0) << sinStats.describe();
    EXPECT_LE(cosStats.maxUlp, 2.0) << cosStats.describe();
    EXPECT_LE(tanStats.maxUlp, 2.0) << tanStats.describe();
}

TEST(DetMathAccuracy, TanWithinTwoUlpIncludingNearPoles) {
    const auto stats = measureUnary(
        "tan", [](f32 x) { return dm::tan(x); }, [](f64 x) { return std::tan(x); }, 0x1p-149f, 1e5f, 3000000, true);
    EXPECT_LE(stats.maxUlp, 2.0) << stats.describe();

    // The f32 neighbours of odd multiples of pi/2 are the worst conditioned inputs.
    ErrorStats poles{"tan near poles"};
    for (int k = 0; k < 20000; ++k) {
        const f32 center = static_cast<f32>((static_cast<f64>(k) + 0.5) * 0x1.921fb54442d18p+1);
        for (int d = -4; d <= 4; ++d) {
            const f32 x = fromBits(static_cast<u32>(static_cast<i32>(toBits(center)) + d));
            poles.add(ulpError(dm::tan(x), std::tan(static_cast<f64>(x))), x);
        }
    }
    poles.print();
    EXPECT_LE(poles.maxUlp, 2.0) << poles.describe();
}

TEST(DetMathAccuracy, InverseTrigWithinTwoUlpOnFullDomains) {
    const auto asinStats = measureUnary(
        "asin", [](f32 x) { return dm::asin(x); }, [](f64 x) { return std::asin(x); }, 0x1p-149f, 1.0f, 3000000,
        true);
    const auto acosStats = measureUnary(
        "acos", [](f32 x) { return dm::acos(x); }, [](f64 x) { return std::acos(x); }, 0x1p-149f, 1.0f, 3000000,
        true);
    const auto atanStats = measureUnary(
        "atan", [](f32 x) { return dm::atan(x); }, [](f64 x) { return std::atan(x); }, 0x1p-149f,
        std::numeric_limits<f32>::max(), 3000000, true);
    EXPECT_LE(asinStats.maxUlp, 2.0) << asinStats.describe();
    EXPECT_LE(acosStats.maxUlp, 2.0) << acosStats.describe();
    EXPECT_LE(atanStats.maxUlp, 2.0) << atanStats.describe();

    // Every f32 in [0.99, 1] and [-1, -0.99]: where asin/acos are steepest.
    ErrorStats nearOne{"asin/acos near +-1"};
    for (u32 bits = toBits(0.99f); bits <= toBits(1.0f); ++bits) {
        const f32 x = fromBits(bits);
        nearOne.add(ulpError(dm::asin(x), std::asin(static_cast<f64>(x))), x);
        nearOne.add(ulpError(dm::acos(x), std::acos(static_cast<f64>(x))), x);
        nearOne.add(ulpError(dm::acos(-x), std::acos(-static_cast<f64>(x))), -x);
    }
    nearOne.print();
    EXPECT_LE(nearOne.maxUlp, 2.0) << nearOne.describe();
}

TEST(DetMathAccuracy, Atan2WithinTwoUlp) {
    ErrorStats stats{"atan2"};
    SplitMix rng{2};
    for (int i = 0; i < 2000000; ++i) {
        // Random finite non-zero f32 bit patterns cover every magnitude ratio, including denormals.
        f32 y = 0.0f;
        f32 x = 0.0f;
        do {
            const u64 r = rng.next();
            y = fromBits(static_cast<u32>(r));
            x = fromBits(static_cast<u32>(r >> 32u));
        } while (!std::isfinite(y) || !std::isfinite(x));
        stats.add(ulpError(dm::atan2(y, x), std::atan2(static_cast<f64>(y), static_cast<f64>(x))), y, x);
    }
    for (int i = 0; i < 2000000; ++i) {
        const f32 y = rng.uniform(-10.0, 10.0);
        const f32 x = rng.uniform(-10.0, 10.0);
        stats.add(ulpError(dm::atan2(y, x), std::atan2(static_cast<f64>(y), static_cast<f64>(x))), y, x);
    }
    stats.print();
    EXPECT_LE(stats.maxUlp, 2.0) << stats.describe();
}

TEST(DetMathAccuracy, ExpLogWithinTwoUlpOnFullDomains) {
    const auto expStats = measureUnary(
        "exp", [](f32 x) { return dm::exp(x); }, [](f64 x) { return std::exp(x); }, 0x1p-149f, 89.0f, 3000000, true);
    const auto exp2Stats = measureUnary(
        "exp2", [](f32 x) { return dm::exp2(x); }, [](f64 x) { return std::exp2(x); }, 0x1p-149f, 128.0f, 3000000,
        true);
    const auto logStats = measureUnary(
        "log", [](f32 x) { return dm::log(x); }, [](f64 x) { return std::log(x); }, 0x1p-149f,
        std::numeric_limits<f32>::max(), 3000000, false);
    const auto log2Stats = measureUnary(
        "log2", [](f32 x) { return dm::log2(x); }, [](f64 x) { return std::log2(x); }, 0x1p-149f,
        std::numeric_limits<f32>::max(), 3000000, false);
    EXPECT_LE(expStats.maxUlp, 2.0) << expStats.describe();
    EXPECT_LE(exp2Stats.maxUlp, 2.0) << exp2Stats.describe();
    EXPECT_LE(logStats.maxUlp, 2.0) << logStats.describe();
    EXPECT_LE(log2Stats.maxUlp, 2.0) << log2Stats.describe();

    // Every third f32 in [0.5, 2]: log near 1 is where cancellation would show.
    ErrorStats nearOne{"log/log2 in [0.5, 2]"};
    for (u32 bits = toBits(0.5f); bits <= toBits(2.0f); bits += 3u) {
        const f32 x = fromBits(bits);
        nearOne.add(ulpError(dm::log(x), std::log(static_cast<f64>(x))), x);
        nearOne.add(ulpError(dm::log2(x), std::log2(static_cast<f64>(x))), x);
    }
    nearOne.print();
    EXPECT_LE(nearOne.maxUlp, 2.0) << nearOne.describe();
}

TEST(DetMathAccuracy, PowWithinFourUlpForNormalResults) {
    ErrorStats stats{"pow"};
    SplitMix rng{3};
    const f64 minNormal = static_cast<f64>(std::numeric_limits<f32>::min());
    const f64 maxFinite = static_cast<f64>(std::numeric_limits<f32>::max());
    auto sample = [&](f32 base, f32 exponent) {
        const f64 reference = std::pow(static_cast<f64>(base), static_cast<f64>(exponent));
        if (std::fabs(reference) >= minNormal && std::fabs(reference) <= maxFinite) {
            stats.add(ulpError(dm::pow(base, exponent), reference), base, exponent);
        }
    };
    for (int i = 0; i < 1500000; ++i) {
        sample(rng.uniform(0.0, 4.0), rng.uniform(-60.0, 60.0));
    }
    for (int i = 0; i < 1500000; ++i) {
        // Log-uniform bases over the whole positive range with exponents that keep many results in range.
        const f32 base = dm::exp2(rng.uniform(-149.0, 128.0));
        sample(base, rng.uniform(-2.0, 2.0));
    }
    for (int i = 0; i < 500000; ++i) {
        // Negative bases with integer exponents.
        sample(rng.uniform(-8.0, 0.0), static_cast<f32>(static_cast<i32>(rng.next() % 81u) - 40));
    }
    stats.print();
    EXPECT_LE(stats.maxUlp, 4.0) << stats.describe();
}

// --- Special values -------------------------------------------------------------------------------------------------

TEST(DetMathSpecialValues, NaNInputsYieldCanonicalNaN) {
    // A payload and sign that must not leak through, plus a signalling NaN.
    for (f32 input : {std::numeric_limits<f32>::quiet_NaN(), fromBits(0xFFC00001u), fromBits(0x7F800001u)}) {
        SCOPED_TRACE(testing::PrintToString(bitsOf(input)));
        EXPECT_EQ(bitsOf(dm::sin(input)), kCanonicalNaNBits);
        EXPECT_EQ(bitsOf(dm::cos(input)), kCanonicalNaNBits);
        EXPECT_EQ(bitsOf(dm::tan(input)), kCanonicalNaNBits);
        EXPECT_EQ(bitsOf(dm::asin(input)), kCanonicalNaNBits);
        EXPECT_EQ(bitsOf(dm::acos(input)), kCanonicalNaNBits);
        EXPECT_EQ(bitsOf(dm::atan(input)), kCanonicalNaNBits);
        EXPECT_EQ(bitsOf(dm::atan2(input, 1.0f)), kCanonicalNaNBits);
        EXPECT_EQ(bitsOf(dm::atan2(1.0f, input)), kCanonicalNaNBits);
        EXPECT_EQ(bitsOf(dm::exp(input)), kCanonicalNaNBits);
        EXPECT_EQ(bitsOf(dm::exp2(input)), kCanonicalNaNBits);
        EXPECT_EQ(bitsOf(dm::log(input)), kCanonicalNaNBits);
        EXPECT_EQ(bitsOf(dm::log2(input)), kCanonicalNaNBits);
        EXPECT_EQ(bitsOf(dm::pow(input, 2.0f)), kCanonicalNaNBits);
        EXPECT_EQ(bitsOf(dm::pow(2.0f, input)), kCanonicalNaNBits);
        EXPECT_EQ(bitsOf(dm::sqrt(input)), kCanonicalNaNBits);
        EXPECT_EQ(bitsOf(dm::floor(input)), kCanonicalNaNBits);
        EXPECT_EQ(bitsOf(dm::ceil(input)), kCanonicalNaNBits);
        EXPECT_EQ(bitsOf(dm::round(input)), kCanonicalNaNBits);
        EXPECT_EQ(bitsOf(dm::trunc(input)), kCanonicalNaNBits);
        EXPECT_EQ(bitsOf(dm::abs(input)), kCanonicalNaNBits);
        EXPECT_EQ(bitsOf(dm::fmod(input, 1.0f)), kCanonicalNaNBits);
        EXPECT_EQ(bitsOf(dm::fmod(1.0f, input)), kCanonicalNaNBits);
        f32 s = 0.0f;
        f32 c = 0.0f;
        dm::sincos(input, s, c);
        EXPECT_EQ(bitsOf(s), kCanonicalNaNBits);
        EXPECT_EQ(bitsOf(c), kCanonicalNaNBits);
    }
}

TEST(DetMathSpecialValues, DomainErrorsYieldCanonicalNaN) {
    const f32 denormMin = std::numeric_limits<f32>::denorm_min();
    EXPECT_EQ(bitsOf(dm::sin(kInf)), kCanonicalNaNBits);
    EXPECT_EQ(bitsOf(dm::cos(-kInf)), kCanonicalNaNBits);
    EXPECT_EQ(bitsOf(dm::tan(kInf)), kCanonicalNaNBits);
    EXPECT_EQ(bitsOf(dm::asin(1.0000001f)), kCanonicalNaNBits);
    EXPECT_EQ(bitsOf(dm::acos(-1.0000001f)), kCanonicalNaNBits);
    EXPECT_EQ(bitsOf(dm::log(-1.0f)), kCanonicalNaNBits);
    EXPECT_EQ(bitsOf(dm::log2(-denormMin)), kCanonicalNaNBits);
    EXPECT_EQ(bitsOf(dm::log(-kInf)), kCanonicalNaNBits);
    EXPECT_EQ(bitsOf(dm::pow(-2.0f, 0.5f)), kCanonicalNaNBits);
    EXPECT_EQ(bitsOf(dm::sqrt(-1.0f)), kCanonicalNaNBits);
    EXPECT_EQ(bitsOf(dm::fmod(1.0f, 0.0f)), kCanonicalNaNBits);
    EXPECT_EQ(bitsOf(dm::fmod(kInf, 1.0f)), kCanonicalNaNBits);
}

TEST(DetMathSpecialValues, OddFunctionsPreserveSignedZeroAndDenormals) {
    for (f32 z : {0.0f, -0.0f}) {
        SCOPED_TRACE(testing::PrintToString(bitsOf(z)));
        EXPECT_EQ(bitsOf(dm::sin(z)), bitsOf(z));
        EXPECT_EQ(bitsOf(dm::tan(z)), bitsOf(z));
        EXPECT_EQ(bitsOf(dm::asin(z)), bitsOf(z));
        EXPECT_EQ(bitsOf(dm::atan(z)), bitsOf(z));
        EXPECT_EQ(bitsOf(dm::sqrt(z)), bitsOf(z));
        EXPECT_EQ(dm::cos(z), 1.0f);
        EXPECT_EQ(dm::exp(z), 1.0f);
        EXPECT_EQ(dm::exp2(z), 1.0f);
        EXPECT_EQ(bitsOf(dm::log(z)), bitsOf(-kInf));
        EXPECT_EQ(bitsOf(dm::log2(z)), bitsOf(-kInf));
    }
    const f32 denormMin = std::numeric_limits<f32>::denorm_min();
    EXPECT_EQ(bitsOf(dm::sin(denormMin)), bitsOf(denormMin));
    EXPECT_EQ(bitsOf(dm::sin(-denormMin)), bitsOf(-denormMin));
    EXPECT_EQ(bitsOf(dm::atan(denormMin)), bitsOf(denormMin));
    EXPECT_EQ(bitsOf(dm::asin(-denormMin)), bitsOf(-denormMin));
}

TEST(DetMathSpecialValues, ExactValuesAndOverflowBoundaries) {
    const f32 pi = dm::kPi;
    const f32 halfPi = dm::kHalfPi;
    EXPECT_EQ(bitsOf(dm::log(1.0f)), bitsOf(0.0f));
    EXPECT_EQ(bitsOf(dm::acos(1.0f)), bitsOf(0.0f));
    EXPECT_EQ(dm::acos(-1.0f), pi);
    EXPECT_EQ(dm::asin(1.0f), halfPi);
    EXPECT_EQ(dm::asin(-1.0f), -halfPi);
    EXPECT_EQ(dm::atan(kInf), halfPi);
    EXPECT_EQ(dm::atan(-kInf), -halfPi);
    for (int k = -149; k <= 127; ++k) {
        const f32 p = std::ldexp(1.0f, k);
        EXPECT_EQ(dm::exp2(static_cast<f32>(k)), p) << "k=" << k;
        EXPECT_EQ(dm::log2(p), static_cast<f32>(k)) << "k=" << k;
    }
    EXPECT_EQ(dm::exp(kInf), kInf);
    EXPECT_EQ(bitsOf(dm::exp(-kInf)), bitsOf(0.0f));
    EXPECT_EQ(bitsOf(dm::exp2(-kInf)), bitsOf(0.0f));
    EXPECT_LT(dm::exp(88.72283f), kInf);
    EXPECT_EQ(dm::exp(88.72284f), kInf);
    EXPECT_EQ(dm::exp2(128.0f), kInf);
    EXPECT_EQ(bitsOf(dm::exp(-104.0f)), bitsOf(0.0f));
    EXPECT_GT(dm::exp(-103.0f), 0.0f);
    EXPECT_EQ(dm::log(kInf), kInf);
    EXPECT_EQ(dm::log2(kInf), kInf);
}

// C99 Annex F.9.1.4: quadrants, zeros and infinities.
TEST(DetMathSpecialValues, Atan2FollowsAnnexF) {
    const f32 pi = dm::kPi;
    const f32 halfPi = dm::kHalfPi;
    EXPECT_EQ(bitsOf(dm::atan2(0.0f, 0.0f)), bitsOf(0.0f));
    EXPECT_EQ(bitsOf(dm::atan2(-0.0f, 0.0f)), bitsOf(-0.0f));
    EXPECT_EQ(dm::atan2(0.0f, -0.0f), pi);
    EXPECT_EQ(dm::atan2(-0.0f, -0.0f), -pi);
    EXPECT_EQ(dm::atan2(0.0f, -1.0f), pi);
    EXPECT_EQ(dm::atan2(-0.0f, -1.0f), -pi);
    EXPECT_EQ(bitsOf(dm::atan2(0.0f, 1.0f)), bitsOf(0.0f));
    EXPECT_EQ(bitsOf(dm::atan2(-0.0f, 1.0f)), bitsOf(-0.0f));
    EXPECT_EQ(dm::atan2(1.0f, 0.0f), halfPi);
    EXPECT_EQ(dm::atan2(1.0f, -0.0f), halfPi);
    EXPECT_EQ(dm::atan2(-1.0f, 0.0f), -halfPi);
    EXPECT_EQ(dm::atan2(kInf, 5.0f), halfPi);
    EXPECT_EQ(dm::atan2(-kInf, -5.0f), -halfPi);
    EXPECT_EQ(dm::atan2(kInf, kInf), pi / 4.0f);
    EXPECT_EQ(dm::atan2(-kInf, kInf), -pi / 4.0f);
    EXPECT_EQ(dm::atan2(kInf, -kInf), static_cast<f32>(3.0 * 0x1.921fb54442d18p-1));
    EXPECT_EQ(bitsOf(dm::atan2(1.0f, kInf)), bitsOf(0.0f));
    EXPECT_EQ(bitsOf(dm::atan2(-1.0f, kInf)), bitsOf(-0.0f));
    EXPECT_EQ(dm::atan2(1.0f, -kInf), pi);
    EXPECT_EQ(dm::atan2(-1.0f, -kInf), -pi);
    // Open intervals: each original two-sided check is split into its two bounds.
    EXPECT_GT(dm::atan2(1.0f, 1.0f), 0.0f);
    EXPECT_LT(dm::atan2(1.0f, 1.0f), halfPi);
    EXPECT_GT(dm::atan2(1.0f, -1.0f), halfPi);
    EXPECT_LT(dm::atan2(1.0f, -1.0f), pi);
    EXPECT_LT(dm::atan2(-1.0f, -1.0f), -halfPi);
    EXPECT_GT(dm::atan2(-1.0f, -1.0f), -pi);
    EXPECT_LT(dm::atan2(-1.0f, 1.0f), 0.0f);
    EXPECT_GT(dm::atan2(-1.0f, 1.0f), -halfPi);
}

// C99 Annex F.9.4.4.
TEST(DetMathSpecialValues, PowFollowsAnnexF) {
    const f32 nan = std::numeric_limits<f32>::quiet_NaN();
    const f32 denormMin = std::numeric_limits<f32>::denorm_min();
    EXPECT_EQ(dm::pow(nan, 0.0f), 1.0f);
    EXPECT_EQ(dm::pow(nan, -0.0f), 1.0f);
    EXPECT_EQ(dm::pow(1.0f, nan), 1.0f);
    EXPECT_EQ(dm::pow(1.0f, kInf), 1.0f);
    EXPECT_EQ(dm::pow(-1.0f, kInf), 1.0f);
    EXPECT_EQ(dm::pow(-1.0f, -kInf), 1.0f);
    EXPECT_EQ(bitsOf(dm::pow(0.0f, -3.0f)), bitsOf(kInf));
    EXPECT_EQ(bitsOf(dm::pow(-0.0f, -3.0f)), bitsOf(-kInf));
    EXPECT_EQ(bitsOf(dm::pow(-0.0f, -2.0f)), bitsOf(kInf));
    EXPECT_EQ(bitsOf(dm::pow(-0.0f, -0.5f)), bitsOf(kInf));
    EXPECT_EQ(bitsOf(dm::pow(-0.0f, -kInf)), bitsOf(kInf));
    EXPECT_EQ(bitsOf(dm::pow(-0.0f, 3.0f)), bitsOf(-0.0f));
    EXPECT_EQ(bitsOf(dm::pow(0.0f, 3.0f)), bitsOf(0.0f));
    EXPECT_EQ(bitsOf(dm::pow(-0.0f, 2.0f)), bitsOf(0.0f));
    EXPECT_EQ(bitsOf(dm::pow(-0.0f, kInf)), bitsOf(0.0f));
    EXPECT_EQ(bitsOf(dm::pow(0.5f, -kInf)), bitsOf(kInf));
    EXPECT_EQ(bitsOf(dm::pow(2.0f, -kInf)), bitsOf(0.0f));
    EXPECT_EQ(bitsOf(dm::pow(-0.5f, kInf)), bitsOf(0.0f));
    EXPECT_EQ(bitsOf(dm::pow(-2.0f, kInf)), bitsOf(kInf));
    EXPECT_EQ(bitsOf(dm::pow(-kInf, -3.0f)), bitsOf(-0.0f));
    EXPECT_EQ(bitsOf(dm::pow(-kInf, -2.0f)), bitsOf(0.0f));
    EXPECT_EQ(bitsOf(dm::pow(-kInf, 3.0f)), bitsOf(-kInf));
    EXPECT_EQ(bitsOf(dm::pow(-kInf, 2.5f)), bitsOf(kInf));
    EXPECT_EQ(bitsOf(dm::pow(kInf, -1.0f)), bitsOf(0.0f));
    EXPECT_EQ(bitsOf(dm::pow(kInf, 0.5f)), bitsOf(kInf));
    EXPECT_EQ(dm::pow(-2.0f, 3.0f), -8.0f);
    EXPECT_EQ(dm::pow(-2.0f, 4.0f), 16.0f);
    EXPECT_EQ(dm::pow(-2.0f, 0x1p25f), kInf); // huge even exponent
    EXPECT_EQ(dm::pow(2.0f, 10.0f), 1024.0f);
    EXPECT_EQ(dm::pow(10.0f, 3.0f), 1000.0f);
    EXPECT_EQ(dm::pow(4.0f, 0.5f), 2.0f);
    EXPECT_EQ(dm::pow(2.0f, -1.0f), 0.5f);
    EXPECT_EQ(dm::pow(2.0f, 128.0f), kInf);
    EXPECT_EQ(bitsOf(dm::pow(2.0f, -151.0f)), bitsOf(0.0f));
    EXPECT_EQ(bitsOf(dm::pow(-2.0f, -151.0f)), bitsOf(-0.0f));
    EXPECT_EQ(dm::pow(2.0f, -149.0f), denormMin);
    for (int k = 1; k <= 20; ++k) {
        EXPECT_EQ(dm::pow(3.0f, static_cast<f32>(k)), static_cast<f32>(std::pow(3.0, k))) << "k=" << k;
    }
}

TEST(DetMathSpecialValues, RoundingAndHelperFunctions) {
    EXPECT_EQ(dm::sqrt(2.0f), std::sqrt(2.0f));
    EXPECT_EQ(dm::floor(-1.5f), -2.0f);
    EXPECT_EQ(dm::ceil(-1.5f), -1.0f);
    EXPECT_EQ(dm::round(2.5f), 3.0f);
    EXPECT_EQ(dm::round(-2.5f), -3.0f);
    EXPECT_EQ(dm::trunc(-1.7f), -1.0f);
    EXPECT_EQ(bitsOf(dm::abs(-0.0f)), bitsOf(0.0f));
    EXPECT_EQ(dm::abs(-kInf), kInf);
    EXPECT_EQ(dm::fmod(7.5f, 2.0f), 1.5f);
    EXPECT_EQ(bitsOf(dm::fmod(-6.0f, 3.0f)), bitsOf(-0.0f));
}

// --- Identities -----------------------------------------------------------------------------------------------------

TEST(DetMathIdentities, SymmetryAndIdentitiesHold) {
    constexpr u32 kSign = 0x80000000u;
    SplitMix rng{4};
    f64 maxPythagorasError = 0.0;
    int expLogMismatches = 0;
    for (int i = 0; i < 500000; ++i) {
        const f32 x = rng.uniform(-1e5, 1e5);
        EXPECT_EQ(bitsOf(dm::sin(-x)), Bits{toBits(dm::sin(x)) ^ kSign}) << "x=" << hexFloat(x);
        EXPECT_EQ(bitsOf(dm::cos(-x)), bitsOf(dm::cos(x))) << "x=" << hexFloat(x);
        EXPECT_EQ(bitsOf(dm::tan(-x)), Bits{toBits(dm::tan(x)) ^ kSign}) << "x=" << hexFloat(x);
        const f64 s = dm::sin(x);
        const f64 c = dm::cos(x);
        const f64 e = std::fabs(s * s + c * c - 1.0);
        maxPythagorasError = e > maxPythagorasError ? e : maxPythagorasError;

        const f32 a = rng.uniform(-1.0, 1.0);
        EXPECT_EQ(bitsOf(dm::asin(-a)), Bits{toBits(dm::asin(a)) ^ kSign}) << "a=" << hexFloat(a);
        EXPECT_EQ(bitsOf(dm::atan(-x)), Bits{toBits(dm::atan(x)) ^ kSign}) << "x=" << hexFloat(x);
        // acos(-a) = pi - acos(a), up to the rounding of both sides.
        EXPECT_LT(std::fabs(static_cast<f64>(dm::acos(-a)) - (0x1.921fb54442d18p+1 - dm::acos(a))), 1e-6)
            << "a=" << hexFloat(a);
        // asin(a) + acos(a) = pi/2.
        EXPECT_LT(std::fabs(static_cast<f64>(dm::asin(a)) + dm::acos(a) - 0x1.921fb54442d18p+0), 1e-6)
            << "a=" << hexFloat(a);

        const f32 p = dm::exp2(rng.uniform(-120.0, 120.0));
        const f32 roundTrip = dm::exp(dm::log(p));
        // log rounds to f32, which can move the result by |log p| * 2^-24 relative: allow that many ulps.
        if (std::fabs(static_cast<f64>(roundTrip) - p) > std::fabs(static_cast<f64>(p)) * 0x1p-17) {
            ++expLogMismatches;
        }
    }
    EXPECT_LT(maxPythagorasError, 4e-7);
    EXPECT_EQ(expLogMismatches, 0);

    // atan2 agrees with atan in the right half-plane and has the expected quadrant sign pattern.
    for (int i = 0; i < 200000; ++i) {
        const f32 y = rng.uniform(-100.0, 100.0);
        const f32 x = rng.uniform(1e-3, 100.0);
        const f32 r = dm::atan2(y, x);
        EXPECT_LT(std::fabs(static_cast<f64>(r) - dm::atan(y / x)), 1e-6)
            << "y=" << hexFloat(y) << " x=" << hexFloat(x);
        EXPECT_EQ(bitsOf(dm::atan2(-y, x)), Bits{toBits(r) ^ kSign}) << "y=" << hexFloat(y) << " x=" << hexFloat(x);
        EXPECT_EQ(bitsOf(dm::atan2(-y, -x)), Bits{toBits(dm::atan2(y, -x)) ^ kSign})
            << "y=" << hexFloat(y) << " x=" << hexFloat(x);
        EXPECT_EQ(dm::atan2(y, -x) >= 0.0f, y >= 0.0f) << "y=" << hexFloat(y) << " x=" << hexFloat(x);
    }
}

TEST(DetMathIdentities, SincosMatchesSinAndCosBitwise) {
    SplitMix rng{5};
    auto checkOne = [](f32 x) {
        f32 s = 0.0f;
        f32 c = 0.0f;
        dm::sincos(x, s, c);
        EXPECT_EQ(bitsOf(s), bitsOf(dm::sin(x))) << "x=" << testing::PrintToString(bitsOf(x));
        EXPECT_EQ(bitsOf(c), bitsOf(dm::cos(x))) << "x=" << testing::PrintToString(bitsOf(x));
    };
    for (int i = 0; i < 1000000; ++i) {
        checkOne(fromBits(static_cast<u32>(rng.next())));
    }
    checkOne(0.0f);
    checkOne(-0.0f);
    checkOne(kInf);
}

// --- Determinism ----------------------------------------------------------------------------------------------------

namespace {

// Inputs for the golden hash: awkward values plus a fixed pseudo-random spread of bit patterns.
std::vector<f32> goldenInputs() {
    std::vector<f32> values = {
        0.0f, -0.0f, kInf, -kInf, std::numeric_limits<f32>::quiet_NaN(), fromBits(0xFFC00001u),
        std::numeric_limits<f32>::denorm_min(), -std::numeric_limits<f32>::denorm_min(), fromBits(0x007FFFFFu),
        std::numeric_limits<f32>::min(), std::numeric_limits<f32>::max(), -std::numeric_limits<f32>::max(), 1.0f,
        -1.0f, fromBits(0x3F7FFFFFu), fromBits(0x3F800001u), 0.5f, 2.0f, 0.70710677f, 1e-30f, 1e-10f, 1e-5f, 0.1f,
        3.0f, 10.0f, 88.72283f, 88.72284f, 89.0f, -87.33654f, -103.97207f, -104.0f, 127.0f, 128.0f, -149.0f,
        -150.0f, 1e5f, 1e6f, 0x1p19f, fromBits(0x48FFFFFFu), 1e10f, 1e20f, 1e30f, 1e38f, fromBits(0x5922AA80u),
        fromBits(0x6A1F5D80u), fromBits(0x47E5D7A0u),
    };
    // Neighbours of multiples of pi/2, where range reduction cancels.
    for (int k = 1; k <= 64; ++k) {
        const f32 center = static_cast<f32>(static_cast<f64>(k) * 0x1.921fb54442d18p+0);
        for (int d = -2; d <= 2; ++d) {
            values.push_back(fromBits(static_cast<u32>(static_cast<i32>(toBits(center)) + d)));
        }
    }
    SplitMix rng{0x00D37E26u};
    for (int i = 0; i < 4096; ++i) {
        values.push_back(fromBits(static_cast<u32>(rng.next()))); // every class, NaN payloads included
    }
    for (int i = 0; i < 2048; ++i) {
        values.push_back(rng.uniform(-10.0, 10.0));
    }
    return values;
}

} // namespace

TEST(DetMathDeterminism, GoldenBitHashMatchesAcrossPlatforms) {
    const std::vector<f32> inputs = goldenInputs();
    dg::Fnv1a64 hash;
    for (f32 x : inputs) {
        f32 s = 0.0f;
        f32 c = 0.0f;
        dm::sincos(x, s, c);
        for (f32 value : {dm::sin(x), dm::cos(x), s, c, dm::tan(x), dm::asin(x), dm::acos(x), dm::atan(x),
                          dm::exp(x), dm::exp2(x), dm::log(x), dm::log2(x), dm::sqrt(x), dm::floor(x), dm::ceil(x),
                          dm::round(x), dm::trunc(x), dm::abs(x)}) {
            hash.updateValue(toBits(value));
        }
    }
    // Binary functions over a grid of the first inputs (all the awkward values and some random bit patterns).
    const std::size_t gridSize = inputs.size() < 160 ? inputs.size() : 160;
    for (std::size_t i = 0; i < gridSize; ++i) {
        for (std::size_t j = 0; j < gridSize; ++j) {
            hash.updateValue(toBits(dm::atan2(inputs[i], inputs[j])));
            hash.updateValue(toBits(dm::pow(inputs[i], inputs[j])));
            hash.updateValue(toBits(dm::fmod(inputs[i], inputs[j])));
        }
    }
    // pow over bases and exponents that keep results finite, the common case in gameplay code.
    SplitMix rng{0x90u};
    for (int i = 0; i < 4096; ++i) {
        const f32 base = rng.uniform(-4.0, 4.0);
        const f32 exponent = (i & 1) != 0 ? rng.uniform(-8.0, 8.0) : static_cast<f32>(i % 17 - 8);
        hash.updateValue(toBits(dm::pow(base, exponent)));
    }

    // Recorded on macOS ARM64 (AppleClang), identical in Debug+ASan/UBSan and Release. x64 (MSVC, clang-cl, clang)
    // and Android ARM64 must reproduce it exactly; a mismatch means detmath is not deterministic on that platform.
    // Any intentional change to detmath's results must update this constant (it changes sim state hashes too).
    constexpr u64 kGolden = 0x1BFF52C5DF7021AEull;
    const u64 digest = hash.digest();
    // Always printed so x64 and Android runs can record and compare the hash even when it passes.
    std::printf("    detmath golden hash: 0x%016" PRIX64 " over %zu inputs\n", digest, inputs.size());
    EXPECT_EQ(digest, kGolden) << std::hex << std::showbase << "detmath golden hash " << digest << " (expected "
                               << kGolden << ") over " << std::dec << inputs.size() << " inputs";
}
