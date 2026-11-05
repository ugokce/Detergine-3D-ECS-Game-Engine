#include "dg/math/DetMath.h"

#include <array>
#include <bit>
#include <cmath>

// The build already disables FP contraction for every engine target (cmake/DgTargets.cmake). Repeating it here keeps
// this file correct even if it is ever compiled by a target that forgets dg_configure_target: a fused multiply-add
// would round differently from the separate multiply and add that ARM64 or x64 would otherwise execute.
#if defined(__clang__)
    #pragma clang fp contract(off)
#elif defined(_MSC_VER)
    #pragma fp_contract(off)
#endif

// Implementation notes.
//
// * Every f32 function computes in f64 and rounds once to f32 at the end. The f64 cores are accurate to roughly
//   1e-15 relative, so the f32 result is the correctly rounded value except in rare near-halfway cases (measured max
//   error is reported by tests/math/DetMathTest.cpp).
// * Only + - * / on f64, comparisons, exact conversions and the exactly specified library functions (sqrt, floor,
//   trunc, round, fmod, fabs, frexp, ldexp) are used. All of them are correctly rounded or exact by IEEE-754, so the
//   bits are identical on every conforming platform.
// * Every NaN result is the canonical quiet NaN 0x7FC00000. Input NaN payloads are never propagated, because x64 and
//   ARM64 propagate and generate NaNs differently (x64's default NaN has the sign bit set, ARM64's does not), which
//   would make state hashes diverge.
// * Assumed FP environment (the platform default; nothing in the engine changes it): round-to-nearest-even, no
//   flush-to-zero / denormals-are-zero. Denormal f32 inputs are converted to f64, where they are normal numbers.
// * Polynomials are truncated Taylor series with coefficients 1/n! or 1/n rounded to nearest f64 (written as hex
//   float literals so the source pins the exact bits). On the reduced intervals used here the truncation error is
//   below 1e-16 relative, so a minimax fit would only save a few terms.

namespace dg::detmath {
namespace {

constexpr u32 kCanonicalNaNBits = 0x7FC00000u;

constexpr f64 kPiD = 0x1.921fb54442d18p+1;
constexpr f64 kHalfPiD = 0x1.921fb54442d18p+0;
constexpr f64 kQuarterPiD = 0x1.921fb54442d18p-1;
constexpr f64 kThreeQuarterPiD = 0x1.2d97c7f3321d2p+1;
constexpr f64 kSixthPiD = 0x1.0c152382d7366p-1;
constexpr f64 kTwoOverPiD = 0x1.45f306dc9c883p-1;
constexpr f64 kSqrt3 = 0x1.bb67ae8584caap+0;
constexpr f64 kTanTwelfthPi = 0x1.126145e9ecd56p-2; // 2 - sqrt(3)
constexpr f64 kSqrtHalf = 0x1.6a09e667f3bcdp-1;
constexpr f64 kLn2 = 0x1.62e42fefa39efp-1;
constexpr f64 kLog2e = 0x1.71547652b82fep+0;

// Cody-Waite split of ln2 (fdlibm's ln2_hi/ln2_lo). kLn2Hi has 32 significant bits, so k * kLn2Hi is exact for
// |k| < 2^21, and x - k * kLn2Hi is exact by Sterbenz' lemma.
constexpr f64 kLn2Hi = 0x1.62e42feep-1;
constexpr f64 kLn2Lo = 0x1.a39ef35793c76p-33;

// Cody-Waite split of pi/2 (fdlibm's pio2_1, pio2_2, pio2_2t). The first two parts have 33 significant bits, so
// k * part is exact for k < 2^20; together the three parts carry about 119 bits of pi/2.
constexpr f64 kHalfPiPart1 = 0x1.921fb544p+0;
constexpr f64 kHalfPiPart2 = 0x1.0b4611a6p-34;
constexpr f64 kHalfPiPart3 = 0x1.3198a2e037073p-69;

// Below this, Cody-Waite reduction is exact enough (k < 2^19 keeps k * part exact with margin); above it the
// Payne-Hanek reduction handles the full f32 range.
constexpr f32 kCodyWaiteLimit = 0x1p19f;

// Bits of 2/pi after the binary point, most significant first, preceded by two zero words. The zero words let the
// Payne-Hanek window start up to 64 bits before the binary point without a special case: 2/pi has no integer bits.
// 320 bits after the point cover the largest f32 exponent (the last window ends at bit 230).
constexpr std::array<u32, 12> kTwoOverPiWords = {
    0x00000000u, 0x00000000u, 0xA2F9836Eu, 0x4E441529u, 0xFC2757D1u, 0xF534DDC0u,
    0xDB629599u, 0x3C439041u, 0xFE5163ABu, 0xDEBBC561u, 0xB7246E3Au, 0x424DD2E0u,
};

f32 canonicalNaN() { return std::bit_cast<f32>(kCanonicalNaNBits); }

bool isNaN(f32 x) { return (std::bit_cast<u32>(x) & 0x7FFFFFFFu) > 0x7F800000u; }
bool isInf(f32 x) { return (std::bit_cast<u32>(x) & 0x7FFFFFFFu) == 0x7F800000u; }
bool isFinite(f32 x) { return (std::bit_cast<u32>(x) & 0x7FFFFFFFu) < 0x7F800000u; }
bool signBit(f32 x) { return (std::bit_cast<u32>(x) >> 31u) != 0u; }
f32 infinity() { return std::bit_cast<f32>(0x7F800000u); }
f32 signedValue(bool negative, f32 magnitude) { return negative ? -magnitude : magnitude; }

// Rounds an f64 to f32. Converting an out-of-range double is undefined behavior in C++, so overflow is handled
// explicitly: everything at or above FLT_MAX + half an ulp rounds to infinity under round-to-nearest-even.
f32 toF32(f64 value) {
    constexpr f64 kOverflowThreshold = 0x1.ffffffp127;
    if (value >= kOverflowThreshold) {
        return infinity();
    }
    if (value <= -kOverflowThreshold) {
        return -infinity();
    }
    return static_cast<f32>(value);
}

// sin(r) for |r| <= ~pi/4. Truncation error |r|^19/19! < 1e-19.
f64 sinPoly(f64 r) {
    const f64 r2 = r * r;
    f64 p = 0x1.952c77030ad4ap-49; // 1/17!
    p = p * r2 - 0x1.ae7f3e733b81fp-41;
    p = p * r2 + 0x1.6124613a86d09p-33;
    p = p * r2 - 0x1.ae64567f544e4p-26;
    p = p * r2 + 0x1.71de3a556c734p-19;
    p = p * r2 - 0x1.a01a01a01a01ap-13;
    p = p * r2 + 0x1.1111111111111p-7;
    p = p * r2 - 0x1.5555555555555p-3; // 1/3!
    return r + (r * r2) * p;
}

// cos(r) for |r| <= ~pi/4. Truncation error |r|^18/18! < 1e-17.
f64 cosPoly(f64 r) {
    const f64 r2 = r * r;
    f64 p = 0x1.ae7f3e733b81fp-45; // 1/16!
    p = p * r2 - 0x1.93974a8c07c9dp-37;
    p = p * r2 + 0x1.1eed8eff8d898p-29;
    p = p * r2 - 0x1.27e4fb7789f5cp-22;
    p = p * r2 + 0x1.a01a01a01a01ap-16;
    p = p * r2 - 0x1.6c16c16c16c17p-10;
    p = p * r2 + 0x1.5555555555555p-5; // 1/4!
    p = p * r2 - 0x1.0p-1;             // 1/2!
    return 1.0 + r2 * p;
}

// exp(r) for |r| <= ~ln2/2. Truncation error |r|^14/14! < 1e-17.
f64 expPoly(f64 r) {
    f64 p = 0x1.6124613a86d09p-33; // 1/13!
    p = p * r + 0x1.1eed8eff8d898p-29;
    p = p * r + 0x1.ae64567f544e4p-26;
    p = p * r + 0x1.27e4fb7789f5cp-22;
    p = p * r + 0x1.71de3a556c734p-19;
    p = p * r + 0x1.a01a01a01a01ap-16;
    p = p * r + 0x1.a01a01a01a01ap-13;
    p = p * r + 0x1.6c16c16c16c17p-10;
    p = p * r + 0x1.1111111111111p-7;
    p = p * r + 0x1.5555555555555p-5;
    p = p * r + 0x1.5555555555555p-3;
    p = p * r + 0x1.0p-1;
    p = p * r + 1.0;
    return p * r + 1.0;
}

// exp(t) for finite t in about [-150, 130] (callers clamp first). The result is a normal f64.
f64 expCore(f64 t) {
    const f64 k = std::floor(t * kLog2e + 0.5);
    const f64 r = (t - k * kLn2Hi) - k * kLn2Lo;
    return std::ldexp(expPoly(r), static_cast<int>(k));
}

// atan(t) for t >= 0 (including +inf). Reduces to |u| <= tan(pi/12) with atan(t) = pi/2 - atan(1/t) and
// atan(t) = pi/6 + atan((t*sqrt3 - 1) / (t + sqrt3)); neither step cancels, because the added constant is at least as
// large as the reduced part. Taylor truncation error |u|^25/25 < 1e-15 relative.
f64 atanCore(f64 t) {
    const bool inverted = t > 1.0;
    if (inverted) {
        t = 1.0 / t;
    }
    f64 base = 0.0;
    if (t > kTanTwelfthPi) {
        t = (t * kSqrt3 - 1.0) / (t + kSqrt3);
        base = kSixthPiD;
    }
    const f64 t2 = t * t;
    f64 p = 0x1.642c8590b2164p-5; // 1/23
    p = -0x1.8618618618618p-5 + t2 * p;
    p = 0x1.af286bca1af28p-5 + t2 * p;
    p = -0x1.e1e1e1e1e1e1ep-5 + t2 * p;
    p = 0x1.1111111111111p-4 + t2 * p;
    p = -0x1.3b13b13b13b14p-4 + t2 * p;
    p = 0x1.745d1745d1746p-4 + t2 * p;
    p = -0x1.c71c71c71c71cp-4 + t2 * p;
    p = 0x1.2492492492492p-3 + t2 * p;
    p = -0x1.999999999999ap-3 + t2 * p;
    p = 0x1.5555555555555p-2 + t2 * p; // 1/3
    const f64 a = base + (t - (t * t2) * p);
    return inverted ? kHalfPiD - a : a;
}

// Splits log(v) for finite v > 0 into exponent * ln2 + logMantissa, with the mantissa in [sqrt(1/2), sqrt(2)) so that
// logMantissa is small and accurate relative to itself (exact zero for powers of two).
void logParts(f64 v, f64& exponent, f64& logMantissa) {
    int e = 0;
    f64 m = std::frexp(v, &e); // m in [0.5, 1)
    if (m < kSqrtHalf) {
        m = m * 2.0;
        e = e - 1;
    }
    const f64 f = m - 1.0; // exact (Sterbenz)
    const f64 s = f / (2.0 + f);
    const f64 s2 = s * s;
    // log(m) = 2 atanh(s) = 2 (s + s^3/3 + s^5/5 + ...); |s| <= 0.1716, truncation error s^23/23 < 1e-18.
    f64 p = 0x1.8618618618618p-5; // 1/21
    p = 0x1.af286bca1af28p-5 + s2 * p;
    p = 0x1.e1e1e1e1e1e1ep-5 + s2 * p;
    p = 0x1.1111111111111p-4 + s2 * p;
    p = 0x1.3b13b13b13b14p-4 + s2 * p;
    p = 0x1.745d1745d1746p-4 + s2 * p;
    p = 0x1.c71c71c71c71cp-4 + s2 * p;
    p = 0x1.2492492492492p-3 + s2 * p;
    p = 0x1.999999999999ap-3 + s2 * p;
    p = 0x1.5555555555555p-2 + s2 * p; // 1/3
    const f64 twoS = s * 2.0;
    exponent = static_cast<f64>(e);
    logMantissa = twoS + (twoS * s2) * p;
}

// Natural log in f64 for finite v > 0, accurate to a few f64 ulps (pow needs more than f32 precision).
f64 logCore(f64 v) {
    f64 e = 0.0;
    f64 lm = 0.0;
    logParts(v, e, lm);
    return e * kLn2Hi + (e * kLn2Lo + lm);
}

// 32 bits of 2/pi starting at bit index `first` (1-based after the binary point; indices <= 0 are zero bits).
u32 twoOverPiBits(i32 first) {
    const auto offset = static_cast<u32>(first - 1 + 64);
    const u32 word = offset >> 5u;
    const u32 shift = offset & 31u;
    if (shift == 0u) {
        return kTwoOverPiWords[word];
    }
    return (kTwoOverPiWords[word] << shift) | (kTwoOverPiWords[word + 1u] >> (32u - shift));
}

struct Reduced {
    f64 r;         // x - quadrant * pi/2, in about [-pi/4, pi/4]
    u32 quadrant;  // modulo 4
};

// Payne-Hanek reduction for finite ax >= 2^19, exact for the whole f32 range. ax = m * 2^s with a 24-bit integer m;
// x * 2/pi mod 4 only needs the 2/pi bits from index s - 1 on (earlier bits contribute multiples of 4). A 128-bit
// window of them, multiplied by m, gives 2 quadrant bits and 126 fraction bits; the bits beyond the window contribute
// less than 2^-102 of a quadrant. Integer-only, so it is deterministic everywhere and needs no 128-bit type.
Reduced reducePayneHanek(f32 ax) {
    constexpr u64 kLow32 = 0xFFFFFFFFull;
    const u32 bits = std::bit_cast<u32>(ax);
    const i32 s = static_cast<i32>(bits >> 23u) - 127 - 23;
    const u64 m = static_cast<u64>((bits & 0x7FFFFFu) | 0x800000u);
    const i32 first = s - 1;

    // P = m * B, with B the 128-bit window as four 32-bit limbs; each partial product is below 2^56.
    const u64 p3 = m * twoOverPiBits(first + 96);
    const u64 p2 = m * twoOverPiBits(first + 64) + (p3 >> 32u);
    const u64 p1 = m * twoOverPiBits(first + 32) + (p2 >> 32u);
    const u64 p0 = m * twoOverPiBits(first) + (p1 >> 32u);

    // x * 2/pi = P * 2^-126: the low 128 bits of P hold the quadrant (top two bits) and 126 fraction bits.
    u64 hi = ((p0 & kLow32) << 32u) | (p1 & kLow32);
    u64 lo = ((p2 & kLow32) << 32u) | (p3 & kLow32);
    u32 quadrant = static_cast<u32>(hi >> 62u);
    hi = (hi << 2u) | (lo >> 62u);
    lo = lo << 2u;

    // Round to the nearest quadrant so that the remainder is in [-1/2, 1/2): negate the 128-bit fraction.
    const bool negative = (hi >> 63u) != 0u;
    if (negative) {
        quadrant = (quadrant + 1u) & 3u;
        hi = ~hi;
        lo = ~lo + 1u;
        if (lo == 0u) {
            hi = hi + 1u;
        }
    }

    // Normalize so the top bit is set, then convert the top 64 bits with a single rounding (two exact 32-bit halves).
    int scale = -64;
    if (hi == 0u) {
        hi = lo;
        lo = 0u;
        scale = scale - 64;
    }
    f64 fraction = 0.0;
    if (hi != 0u) {
        const int lz = std::countl_zero(hi);
        if (lz != 0) {
            hi = (hi << static_cast<u32>(lz)) | (lo >> static_cast<u32>(64 - lz));
        }
        scale = scale - lz;
        const f64 top = static_cast<f64>(static_cast<u32>(hi >> 32u)) * 0x1p32 + static_cast<f64>(hi & kLow32);
        fraction = std::ldexp(top, scale);
    }
    const f64 r = fraction * kHalfPiD;
    return {negative ? -r : r, quadrant};
}

// Reduces finite ax >= 0 modulo pi/2.
Reduced reduceHalfPi(f32 ax) {
    if (ax <= static_cast<f32>(kQuarterPiD)) {
        return {static_cast<f64>(ax), 0u};
    }
    if (ax < kCodyWaiteLimit) {
        const auto x = static_cast<f64>(ax);
        const f64 k = std::floor(x * kTwoOverPiD + 0.5);
        f64 r = x - k * kHalfPiPart1; // exact: k * part1 is exact and the difference is a multiple of 2^-32 below 1
        r = r - k * kHalfPiPart2;
        r = r - k * kHalfPiPart3;
        return {r, static_cast<u32>(static_cast<i32>(k)) & 3u};
    }
    return reducePayneHanek(ax);
}

// sin and cos of |x| for finite x; sincos and the single functions share it so they agree bit for bit.
void sinCosAbs(f32 ax, f64& outSin, f64& outCos) {
    const Reduced red = reduceHalfPi(ax);
    const f64 s = sinPoly(red.r);
    const f64 c = cosPoly(red.r);
    switch (red.quadrant) {
    case 0u: outSin = s; outCos = c; break;
    case 1u: outSin = c; outCos = -s; break;
    case 2u: outSin = -s; outCos = -c; break;
    default: outSin = -c; outCos = s; break;
    }
}

} // namespace

f32 sin(f32 x) {
    if (!isFinite(x)) {
        return canonicalNaN();
    }
    f64 s = 0.0;
    f64 c = 0.0;
    sinCosAbs(abs(x), s, c);
    return toF32(signBit(x) ? -s : s);
}

f32 cos(f32 x) {
    if (!isFinite(x)) {
        return canonicalNaN();
    }
    f64 s = 0.0;
    f64 c = 0.0;
    sinCosAbs(abs(x), s, c);
    return toF32(c);
}

void sincos(f32 x, f32& outSin, f32& outCos) {
    if (!isFinite(x)) {
        outSin = canonicalNaN();
        outCos = canonicalNaN();
        return;
    }
    f64 s = 0.0;
    f64 c = 0.0;
    sinCosAbs(abs(x), s, c);
    outSin = toF32(signBit(x) ? -s : s);
    outCos = toF32(c);
}

f32 tan(f32 x) {
    if (!isFinite(x)) {
        return canonicalNaN();
    }
    const Reduced red = reduceHalfPi(abs(x));
    const f64 s = sinPoly(red.r);
    const f64 c = cosPoly(red.r);
    // tan(r + k*pi/2) = tan(r) for even k and -cot(r) for odd k. Near a pole r is tiny but carries full relative
    // precision, so the result stays accurate there too.
    const f64 t = (red.quadrant & 1u) == 0u ? s / c : -c / s;
    return toF32(signBit(x) ? -t : t);
}

f32 asin(f32 x) {
    if (isNaN(x)) {
        return canonicalNaN();
    }
    const auto ax = static_cast<f64>(abs(x));
    if (ax > 1.0) {
        return canonicalNaN();
    }
    if (ax == 1.0) {
        return toF32(signBit(x) ? -kHalfPiD : kHalfPiD);
    }
    // asin(x) = atan(x / sqrt(1 - x^2)); (1 - x)(1 + x) avoids the cancellation of 1 - x*x near |x| = 1.
    const f64 a = atanCore(ax / std::sqrt((1.0 - ax) * (1.0 + ax)));
    return toF32(signBit(x) ? -a : a);
}

f32 acos(f32 x) {
    if (isNaN(x)) {
        return canonicalNaN();
    }
    const auto xd = static_cast<f64>(x);
    if (xd > 1.0 || xd < -1.0) {
        return canonicalNaN();
    }
    if (xd == -1.0) {
        return toF32(kPiD);
    }
    // acos(x) = 2 atan(sqrt((1 - x) / (1 + x))): relative accuracy near x = 1, where acos(x) is small.
    return toF32(2.0 * atanCore(std::sqrt((1.0 - xd) / (1.0 + xd))));
}

f32 atan(f32 x) {
    if (isNaN(x)) {
        return canonicalNaN();
    }
    if (isInf(x)) {
        return toF32(signBit(x) ? -kHalfPiD : kHalfPiD);
    }
    const f64 a = atanCore(static_cast<f64>(abs(x)));
    return toF32(signBit(x) ? -a : a);
}

f32 atan2(f32 y, f32 x) {
    if (isNaN(x) || isNaN(y)) {
        return canonicalNaN();
    }
    const bool yNegative = signBit(y);
    const bool xNegative = signBit(x);
    f64 a = 0.0;
    if (y == 0.0f) {
        // atan2(+-0, +0) = +-0 and atan2(+-0, -0) = +-pi; the sign of a zero x matters, so test the sign bit.
        a = xNegative ? kPiD : 0.0;
    } else if (isInf(y)) {
        a = isInf(x) ? (xNegative ? kThreeQuarterPiD : kQuarterPiD) : kHalfPiD;
    } else if (x == 0.0f) {
        a = kHalfPiD;
    } else if (isInf(x)) {
        a = xNegative ? kPiD : 0.0;
    } else {
        // Both finite and non-zero: the f64 quotient of two f32 values never overflows or underflows.
        a = atanCore(static_cast<f64>(abs(y)) / static_cast<f64>(abs(x)));
        if (xNegative) {
            a = kPiD - a; // a <= pi/2, so no cancellation
        }
    }
    return toF32(yNegative ? -a : a);
}

f32 exp(f32 x) {
    if (isNaN(x)) {
        return canonicalNaN();
    }
    // exp(x) overflows f32 above ~88.72 and rounds to zero below ~-103.97; the f64 core is exact enough to make that
    // decision itself inside the clamps.
    if (x > 89.0f) {
        return infinity();
    }
    if (x < -110.0f) {
        return 0.0f;
    }
    return toF32(expCore(static_cast<f64>(x)));
}

f32 exp2(f32 x) {
    if (isNaN(x)) {
        return canonicalNaN();
    }
    if (x >= 128.0f) {
        return infinity();
    }
    if (x < -160.0f) {
        return 0.0f;
    }
    // Reducing by the integer part keeps exp2 of an integer exact.
    const auto xd = static_cast<f64>(x);
    const f64 k = std::floor(xd + 0.5);
    const f64 r = xd - k; // exact
    return toF32(std::ldexp(expPoly(r * kLn2), static_cast<int>(k)));
}

f32 log(f32 x) {
    if (isNaN(x) || x < 0.0f) {
        return canonicalNaN();
    }
    if (x == 0.0f) {
        return -infinity();
    }
    if (isInf(x)) {
        return infinity();
    }
    return toF32(logCore(static_cast<f64>(x)));
}

f32 log2(f32 x) {
    if (isNaN(x) || x < 0.0f) {
        return canonicalNaN();
    }
    if (x == 0.0f) {
        return -infinity();
    }
    if (isInf(x)) {
        return infinity();
    }
    // exponent + log(m) * log2(e): exact for powers of two because log(m) is then exactly zero.
    f64 e = 0.0;
    f64 lm = 0.0;
    logParts(static_cast<f64>(x), e, lm);
    return toF32(e + lm * kLog2e);
}

f32 pow(f32 base, f32 exponent) {
    // Special cases follow C99 Annex F (F.9.4.4).
    if (exponent == 0.0f) {
        return 1.0f;
    }
    if (base == 1.0f) {
        return 1.0f;
    }
    if (isNaN(base) || isNaN(exponent)) {
        return canonicalNaN();
    }
    const bool exponentIsInf = isInf(exponent);
    const bool exponentIsInteger = exponentIsInf || std::trunc(exponent) == exponent;
    // Every f32 of magnitude >= 2^24 is an even integer.
    const bool exponentIsOdd = !exponentIsInf && exponentIsInteger && abs(exponent) < 0x1p24f &&
                               (static_cast<i32>(exponent) & 1) != 0;
    const bool baseNegative = signBit(base);

    if (base == 0.0f) {
        if (exponent < 0.0f) {
            return exponentIsOdd ? signedValue(baseNegative, infinity()) : infinity();
        }
        return exponentIsOdd ? base : 0.0f;
    }
    if (exponentIsInf) {
        const f32 ab = abs(base);
        if (ab == 1.0f) {
            return 1.0f; // pow(-1, +-inf)
        }
        return (ab < 1.0f) == (exponent < 0.0f) ? infinity() : 0.0f;
    }
    if (isInf(base)) {
        const bool negativeResult = baseNegative && exponentIsOdd;
        return signedValue(negativeResult, exponent < 0.0f ? 0.0f : infinity());
    }
    if (baseNegative && !exponentIsInteger) {
        return canonicalNaN();
    }

    // pow = exp(y log|x|) in f64. log is accurate to a few f64 ulps and |y log|x|| <= ~110 inside the clamps, so the
    // exponent's absolute error (which becomes the result's relative error) stays near 1e-14, far below an f32 ulp.
    const bool negativeResult = baseNegative && exponentIsOdd;
    const f64 t = static_cast<f64>(exponent) * logCore(static_cast<f64>(abs(base)));
    if (t > 90.0) {
        return signedValue(negativeResult, infinity());
    }
    if (t < -110.0) {
        return signedValue(negativeResult, 0.0f);
    }
    const f64 result = expCore(t);
    return toF32(negativeResult ? -result : result);
}

f32 sqrt(f32 x) {
    if (isNaN(x) || x < 0.0f) {
        return canonicalNaN();
    }
    return std::sqrt(x);
}

f32 floor(f32 x) { return isNaN(x) ? canonicalNaN() : std::floor(x); }

f32 ceil(f32 x) { return isNaN(x) ? canonicalNaN() : std::ceil(x); }

f32 round(f32 x) { return isNaN(x) ? canonicalNaN() : std::round(x); }

f32 trunc(f32 x) { return isNaN(x) ? canonicalNaN() : std::trunc(x); }

f32 abs(f32 x) { return isNaN(x) ? canonicalNaN() : std::fabs(x); }

f32 fmod(f32 x, f32 y) {
    if (isNaN(x) || isNaN(y) || isInf(x) || y == 0.0f) {
        return canonicalNaN();
    }
    return std::fmod(x, y); // exact by definition: the result is representable
}

} // namespace dg::detmath
