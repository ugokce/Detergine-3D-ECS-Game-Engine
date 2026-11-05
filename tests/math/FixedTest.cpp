#include <gtest/gtest.h>

#include "dg/core/Hash.h"
#include "dg/math/Fixed.h"

#include <array>
#include <compare>
#include <format>
#include <limits>
#include <ostream>
#include <string>
#include <type_traits>

using namespace dg;
using namespace dg::literals;

namespace {

// xorshift64*: a local generator so this suite does not depend on dg::sim and its golden hash cannot be changed by Rng
// edits.
struct TestRng {
    u64 state = 0x9E37'79B9'7F4A'7C15ull;

    constexpr u64 next() {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        return state * 0x2545'F491'4F6C'DD1Dull;
    }
};

// Independent reference arithmetic: a 192-bit unsigned integer in 32-bit limbs, operated on bit by bit. Slow and
// simple on purpose so it shares no code (and no bugs) with detail::mulWide / divWide.
struct Big {
    std::array<u64, 6> limb{}; // each holds 32 bits

    static Big fromU64(u64 v) {
        Big b;
        b.limb[0] = v & 0xFFFF'FFFFu;
        b.limb[1] = v >> 32;
        return b;
    }

    [[nodiscard]] bool bit(int i) const { return ((limb[static_cast<usize>(i / 32)] >> (i % 32)) & 1u) != 0; }
    void setBit(int i) { limb[static_cast<usize>(i / 32)] |= u64{1} << (i % 32); }
    [[nodiscard]] u64 low64() const { return limb[0] | (limb[1] << 32); }
};

int compare(const Big& a, const Big& b) {
    for (usize i = a.limb.size(); i-- > 0;) {
        if (a.limb[i] != b.limb[i]) {
            return a.limb[i] < b.limb[i] ? -1 : 1;
        }
    }
    return 0;
}

Big add(const Big& a, const Big& b) {
    Big r;
    u64 carry = 0;
    for (usize i = 0; i < a.limb.size(); ++i) {
        const u64 s = a.limb[i] + b.limb[i] + carry;
        r.limb[i] = s & 0xFFFF'FFFFu;
        carry = s >> 32;
    }
    return r;
}

Big subtract(const Big& a, const Big& b) { // requires a >= b
    Big r;
    u64 borrow = 0;
    for (usize i = 0; i < a.limb.size(); ++i) {
        const u64 s = a.limb[i] - b.limb[i] - borrow;
        r.limb[i] = s & 0xFFFF'FFFFu;
        borrow = (s >> 63) & 1u;
    }
    return r;
}

Big multiply(const Big& a, const Big& b) {
    Big r;
    for (int i = 0; i < 192; ++i) {
        if (!a.bit(i)) {
            continue;
        }
        Big shifted;
        for (int j = 0; j + i < 192; ++j) {
            if (b.bit(j)) {
                shifted.setBit(i + j);
            }
        }
        r = add(r, shifted);
    }
    return r;
}

Big shiftLeftBig(const Big& a, int s) {
    Big r;
    for (int i = 0; i + s < 192; ++i) {
        if (a.bit(i)) {
            r.setBit(i + s);
        }
    }
    return r;
}

Big shiftRightBig(const Big& a, int s) {
    Big r;
    for (int i = s; i < 192; ++i) {
        if (a.bit(i)) {
            r.setBit(i - s);
        }
    }
    return r;
}

// Restoring binary long division.
void divide(const Big& n, const Big& d, Big& quotient, Big& remainder) {
    quotient = Big{};
    remainder = Big{};
    for (int i = 191; i >= 0; --i) {
        remainder = shiftLeftBig(remainder, 1);
        if (n.bit(i)) {
            remainder.setBit(0);
        }
        if (compare(remainder, d) >= 0) {
            remainder = subtract(remainder, d);
            quotient.setBit(i);
        }
    }
}

template <typename T>
u64 magnitudeOf(T x) {
    const auto raw = static_cast<u64>(static_cast<i64>(x.raw()));
    return x.raw() < 0 ? u64{0} - raw : raw;
}

template <typename T>
T fromSignedMagnitude(u64 magnitude, bool negative) {
    using U = typename T::UnsignedType;
    const auto m = static_cast<U>(magnitude);
    return T::fromRaw(static_cast<typename T::StorageType>(negative ? U{0} - m : m));
}

// Reference a * b: exact product, + 1/2 ulp on the magnitude (ties away from zero), truncate, wrap.
template <typename T>
T referenceMul(T a, T b) {
    Big p = multiply(Big::fromU64(magnitudeOf(a)), Big::fromU64(magnitudeOf(b)));
    p = add(p, Big::fromU64(u64{1} << (T::kFracBits - 1)));
    return fromSignedMagnitude<T>(shiftRightBig(p, T::kFracBits).low64(), (a.raw() < 0) != (b.raw() < 0));
}

// Reference a / b (b != 0): exact quotient, round half away from zero, saturate.
template <typename T>
T referenceDiv(T a, T b) {
    const bool negative = (a.raw() < 0) != (b.raw() < 0);
    const Big n = shiftLeftBig(Big::fromU64(magnitudeOf(a)), T::kFracBits);
    const Big d = Big::fromU64(magnitudeOf(b));
    Big q;
    Big r;
    divide(n, d, q, r);
    if (compare(shiftLeftBig(r, 1), d) >= 0) {
        q = add(q, Big::fromU64(1));
    }
    const Big limit = Big::fromU64(u64{1} << (T::kBits - 1));
    if (compare(q, limit) >= 0) {
        return negative ? T::min() : T::max();
    }
    return fromSignedMagnitude<T>(q.low64(), negative);
}

// Reference for 128/64 division: plain shift-subtract over the 128 bits.
u64 referenceDivWide(detail::U128 n, u64 d, u64& remainder) {
    u64 q = 0;
    u64 r = 0;
    for (int i = 127; i >= 0; --i) {
        const bool top = (r >> 63) != 0;
        const u64 nextBit = i >= 64 ? (n.hi >> (i - 64)) & 1u : (n.lo >> i) & 1u;
        r = (r << 1) | nextBit;
        if (top || r >= d) {
            r -= d;
            q = (q << 1) | 1u;
        } else {
            q <<= 1;
        }
    }
    remainder = r;
    return q;
}

std::string text(auto x) { return std::format("{}", x); }

// Randomized sweeps run tens of thousands of cases; one EXPECT per case would flood the log on a regression. Count
// failures instead and keep the first one, which is enough to reproduce the bug.
class Failures {
public:
    template <typename Describe>
    void record(bool ok, Describe&& describe) {
        if (!ok) {
            if (count == 0) {
                first = describe();
            }
            ++count;
        }
    }

    int count = 0;
    std::string first = "none";
};

std::string hexRaw(auto x) {
    return std::format("{:#x}", static_cast<std::make_unsigned_t<decltype(x.raw())>>(x.raw()));
}

// Runs a check for both formats. Plain TESTs with a trace are used instead of TYPED_TESTs because gtest_discover_tests
// folds the "# TypeParam" comment into typed-test names, which makes them hard to select with ctest -R.
template <typename Check>
void forBothFormats(Check check) {
    {
        SCOPED_TRACE("fixed32");
        check(fixed32{});
    }
    {
        SCOPED_TRACE("fixed64");
        check(fixed64{});
    }
}

} // namespace

namespace dg {

// GoogleTest finds this by ADL, so failures print the decimal value and the raw bits instead of a byte dump.
template <std::signed_integral Storage, int FracBits>
void PrintTo(Fixed<Storage, FracBits> x, std::ostream* os) {
    *os << std::format("{} (raw {:#x})", x, static_cast<std::make_unsigned_t<Storage>>(x.raw()));
}

} // namespace dg

// ---------------------------------------------------------------------------------------------------------------------
// Compile-time tests: the arithmetic is constexpr, so its exact rules are checked by the compiler as well.

static_assert(fixed32::one().raw() == 65536 && fixed32::half().raw() == 32768 && fixed32::epsilon().raw() == 1);
static_assert(fixed64::one().raw() == (i64{1} << 32) && fixed64::half().raw() == (i64{1} << 31));
static_assert(fixed32::pi().raw() == 0x3243F && fixed32::twoPi().raw() == 0x6487F && fixed32::e().raw() == 0x2B7E1);
static_assert(fixed64::pi().raw() == 0x3'243F'6A89 && fixed64::twoPi().raw() == 0x6'487E'D511);
static_assert(fixed64::e().raw() == 0x2'B7E1'5163);
static_assert(fixed32::max().raw() == 0x7FFF'FFFF && fixed32::min().raw() == -0x7FFF'FFFF - 1);

static_assert(fixed32(3).raw() == 3 * 65536 && fixed32(-2).raw() == -2 * 65536);
static_assert(fixed32(40000) == fixed32(40000 - 65536)); // integer construction wraps
static_assert(fixed64(i64{-5}).raw() == i64{-5} << 32);
static_assert(fixed32(7u).toInt() == 7);

static_assert(1.5_fx32 * 2_fx32 == 3_fx32 && 1.5_fx32 + 0.25_fx32 == 1.75_fx32 && 1_fx32 - 1.25_fx32 == -0.25_fx32);
static_assert(1_fx32 / 4_fx32 == 0.25_fx32 && 7.5_fx64 / 2.5_fx64 == 3_fx64);
static_assert((0.1_fx32).raw() == 6554 && (0.1_fx64).raw() == 429496730);
static_assert(1'000.25_fx32 == fixed32(1000) + 0.25_fx32 && 1e2_fx32 == 100_fx32 && 25e-2_fx64 == 0.25_fx64);
static_assert((0.00000762939453125_fx32).raw() == 1); // exactly half an ulp: ties away from zero
static_assert((0.0000076293945312_fx32).raw() == 0);  // just below half an ulp
static_assert(40000_fx32 == fixed32::max());         // literals saturate
static_assert(fixed32::fromDouble(0.1) == 0.1_fx32 && fixed64::fromDouble(3.25) == 3.25_fx64);

static_assert(fixed32::max() + fixed32::epsilon() == fixed32::min());
static_assert(-fixed32::min() == fixed32::min());
static_assert(fixed32(256) * fixed32(128) == fixed32::min()); // 32768 wraps
static_assert(floor(-2.5_fx32) == -3_fx32 && ceil(-2.5_fx32) == -2_fx32 && round(-2.5_fx32) == -3_fx32);
static_assert(trunc(-2.5_fx32) == -2_fx32 && frac(-2.25_fx32) == 0.75_fx32);
static_assert(sqrt(4_fx32) == 2_fx32 && sqrt(2_fx32).raw() == 92682 && sqrt(2_fx64).raw() == 6074001000);
static_assert(lerp(2_fx32, 4_fx32, fixed32::half()) == 3_fx32);
static_assert(min(1_fx32, 2_fx32) == 1_fx32 && max(1_fx32, 2_fx32) == 2_fx32);
static_assert(clamp(5_fx32, 0_fx32, 1_fx32) == 1_fx32);
static_assert(fixed32(fixed64::fromRaw(0x8000)).raw() == 1 && fixed64(fixed32::fromRaw(-1)).raw() == -65536);

static_assert(std::is_convertible_v<fixed32, fixed64>, "widening is lossless and implicit");
static_assert(!std::is_convertible_v<fixed64, fixed32>, "narrowing must be explicit");
static_assert(!std::is_convertible_v<int, fixed32> && !std::is_convertible_v<double, fixed32>);
static_assert(sizeof(fixed32) == 4 && sizeof(fixed64) == 8 && std::is_trivially_copyable_v<fixed64>);

// Character types have platform-dependent signedness/width, so they must not convert (regression: fixed32(char(200))
// was -56 on x64 and 200 on Android ARM64). The fixed-width byte types stay usable.
static_assert(!std::is_constructible_v<fixed32, char> && !std::is_constructible_v<fixed64, wchar_t>);
static_assert(!std::is_constructible_v<fixed32, char8_t> && !std::is_constructible_v<fixed32, char16_t>);
static_assert(!std::is_constructible_v<fixed64, char32_t> && !std::is_constructible_v<fixed32, const char>);
static_assert(!std::is_constructible_v<fixed32, bool>);
static_assert(std::is_constructible_v<fixed32, i8> && std::is_constructible_v<fixed32, u8>);
static_assert(fixed32(static_cast<u8>(200)) == 200_fx32 && fixed32(static_cast<i8>(-56)) == -56_fx32);

// Integer literals with a leading 0 are octal, as in C++ and fromDouble (regression: 010_fx32 was 10).
static_assert(010_fx32 == 8_fx32 && 010_fx32 == fixed32::fromDouble(010) && 0'17_fx64 == 15_fx64);
static_assert(0_fx32 == fixed32::zero() && 00_fx64 == fixed64::zero() && 0777_fx32 == 511_fx32);
static_assert(010.5_fx32 == 10.5_fx32 && 010e1_fx32 == 100_fx32 && 0.5_fx32 == fixed32::half()); // decimal forms

// lerp computes b - a exactly, so spans wider than the range still interpolate (regression: these returned -32768,
// max() and -2^31).
static_assert(lerp(-20000_fx32, 20000_fx32, 0.5_fx32) == 0_fx32);
static_assert(lerp(fixed32::min(), fixed32::max(), fixed32::half()) == fixed32::zero()); // -32768 + 32767.99999 / 2
static_assert(lerp(fixed64(-2'000'000'000), fixed64(2'000'000'000), 0.5_fx64) == fixed64::zero());
static_assert(lerp(fixed64::min(), fixed64::max(), fixed64::half()) == fixed64::zero());

// ---------------------------------------------------------------------------------------------------------------------
// Death tests: the DG_ASSERT paths. Asserts are compiled out in Release, where the defined fallbacks are tested below.

#if DG_ASSERTS_ENABLED

TEST(FixedDeath, DivisionByZeroAsserts) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    // volatile keeps the zero opaque, so the division happens at run time and reaches the assert.
    volatile i32 zeroRaw = 0;
    const fixed32 zero32 = fixed32::fromRaw(zeroRaw);
    const fixed64 zero64 = fixed64::fromRaw(zeroRaw);
    EXPECT_DEATH(static_cast<void>(1_fx32 / zero32), "Assertion failed.*fixed-point division by zero");
    EXPECT_DEATH(static_cast<void>(zero32 / zero32), "Assertion failed.*fixed-point division by zero");
    EXPECT_DEATH(static_cast<void>(-1_fx64 / zero64), "Assertion failed.*fixed-point division by zero");
}

TEST(FixedDeath, SqrtOfNegativeValueAsserts) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    volatile i32 minusOneRaw = -1;
    EXPECT_DEATH(static_cast<void>(sqrt(fixed32::fromRaw(minusOneRaw))),
                 "Assertion failed.*fixed-point sqrt of a negative value");
    EXPECT_DEATH(static_cast<void>(sqrt(fixed64::fromRaw(minusOneRaw))),
                 "Assertion failed.*fixed-point sqrt of a negative value");
}

TEST(FixedDeath, DivWideRequiresQuotientToFit) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    volatile u64 divisor = 0;
    u64 remainder = 0;
    EXPECT_DEATH(static_cast<void>(detail::divWide(detail::U128{0, 1}, divisor, remainder)), "Assertion failed");
    divisor = 5;
    // n.hi >= d: the quotient would need more than 64 bits.
    EXPECT_DEATH(static_cast<void>(detail::divWide(detail::U128{5, 0}, divisor, remainder)), "Assertion failed");
}

TEST(FixedDeath, ClampWithInvertedBoundsAsserts) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    volatile i32 loRaw = 65536;
    EXPECT_DEATH(static_cast<void>(clamp(0_fx32, fixed32::fromRaw(loRaw), -1_fx32)), "Assertion failed");
}

#endif

// ---------------------------------------------------------------------------------------------------------------------
// Construction and conversion

TEST(FixedConstruction, IntegerConstructionWrapsModuloIntegerRange) {
    EXPECT_EQ(fixed32().raw(), 0);
    EXPECT_EQ(fixed32(-32768), fixed32::min());
    EXPECT_EQ(fixed32(32768), fixed32::min()); // wraps
    EXPECT_EQ(fixed64(i64{1} << 31), fixed64::min());
    EXPECT_EQ(fixed32(u64{0x1'0000'0005}), fixed32(5)); // wraps modulo 2^16 integer values
}

TEST(FixedConversion, ToIntTruncatesTowardZero) {
    EXPECT_EQ((-2.5_fx32).toInt(), -2);
    EXPECT_EQ((2.99_fx32).toInt(), 2);
    EXPECT_EQ(fixed32::min().toInt(), -32768);
    EXPECT_EQ(fixed32::max().toInt(), 32767);
    EXPECT_EQ(fixed64::min().toInt(), -(i64{1} << 31));
    EXPECT_EQ((-0.5_fx64).toInt(), 0);
}

TEST(FixedConversion, ToFloatingPointIsExact) {
    EXPECT_EQ((-2.5_fx32).toDouble(), -2.5);
    EXPECT_EQ((0.25_fx64).toFloat(), 0.25f);
    EXPECT_EQ(fixed32::epsilon().toDouble(), 1.0 / 65536.0);
}

TEST(FixedConversion, WideningIsExactAndNarrowingRoundsAndSaturates) {
    fixed64 widened = -1.75_fx32; // implicit, exact
    EXPECT_EQ(widened, -1.75_fx64);
    EXPECT_EQ(fixed32(fixed64::fromRaw(-0x8000)).raw(), -1);
    EXPECT_EQ(fixed32(fixed64::fromRaw(0x7FFF)).raw(), 0);
    EXPECT_EQ(fixed32(fixed64::fromRaw(0x1'8000)).raw(), 2);
    EXPECT_EQ(fixed32(fixed64(40000)), fixed32::max());
    EXPECT_EQ(fixed32(fixed64(-40000)), fixed32::min());
    EXPECT_EQ(fixed32(fixed64(-32768)), fixed32::min());
    EXPECT_EQ(fixed32(fixed64::max()), fixed32::max());
    EXPECT_EQ(fixed32(fixed64::min()), fixed32::min());
    EXPECT_EQ(fixed32(fixed64(fixed32::max())), fixed32::max());
}

// ---------------------------------------------------------------------------------------------------------------------
// fromDouble / fromFloat

TEST(FixedFromDouble, RoundsHalfUlpAwayFromZero) {
    constexpr f64 kUlp = 1.0 / 65536.0;
    EXPECT_EQ(fixed32::fromDouble(0.5 * kUlp).raw(), 1);
    EXPECT_EQ(fixed32::fromDouble(-0.5 * kUlp).raw(), -1);
    EXPECT_EQ(fixed32::fromDouble(1.5 * kUlp).raw(), 2);
    EXPECT_EQ(fixed32::fromDouble(2.5 * kUlp).raw(), 3); // away from zero, not to even
    EXPECT_EQ(fixed32::fromDouble(-2.5 * kUlp).raw(), -3);
    EXPECT_EQ(fixed32::fromDouble(0.49999 * kUlp).raw(), 0);
    EXPECT_EQ(fixed32::fromDouble(-0.0).raw(), 0);
    EXPECT_EQ(fixed32::fromDouble(1e-300).raw(), 0);
}

TEST(FixedFromDouble, MapsNanToZeroAndSaturatesInfinities) {
    const f64 nan = std::numeric_limits<f64>::quiet_NaN();
    const f64 inf = std::numeric_limits<f64>::infinity();
    EXPECT_EQ(fixed32::fromDouble(nan), fixed32::zero());
    EXPECT_EQ(fixed64::fromDouble(-nan), fixed64::zero());
    EXPECT_EQ(fixed32::fromDouble(inf), fixed32::max());
    EXPECT_EQ(fixed32::fromDouble(-inf), fixed32::min());
    EXPECT_EQ(fixed64::fromDouble(1e300), fixed64::max());
    EXPECT_EQ(fixed64::fromDouble(-1e300), fixed64::min());
}

TEST(FixedFromDouble, RoundsThenSaturatesAtRangeEdges) {
    EXPECT_EQ(fixed32::fromDouble(32767.99999).raw(), 0x7FFF'FFFF); // rounds to max
    EXPECT_EQ(fixed32::fromDouble(32767.999995), fixed32::max());   // rounds past max: saturates
    EXPECT_EQ(fixed32::fromDouble(-32768.0), fixed32::min());       // exactly min
    EXPECT_EQ(fixed32::fromDouble(-32768.000007), fixed32::min());  // rounds to min
    EXPECT_EQ(fixed32::fromDouble(-32768.5), fixed32::min());       // saturates
    EXPECT_EQ(fixed64::fromDouble(-2147483648.0), fixed64::min());
    EXPECT_EQ(fixed64::fromDouble(2147483647.75), fixed64::fromRaw(0x7FFF'FFFF'C000'0000));
}

TEST(FixedFromFloat, UsesTheExactFloatValue) {
    EXPECT_EQ(fixed32::fromFloat(1.5f), 1.5_fx32);
    EXPECT_EQ(fixed64::fromFloat(-0.1f).raw(), -429496736); // the float's exact value, not 0.1
    EXPECT_EQ(fixed32::fromFloat(std::numeric_limits<f32>::quiet_NaN()), fixed32::zero());
}

TEST(FixedFromDouble, IsExactForEveryDyadicValueInASweep) {
    for (i32 raw = -70000; raw <= 70000; raw += 7) {
        EXPECT_EQ(fixed32::fromDouble(static_cast<f64>(raw) / 65536.0).raw(), raw);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Wrapping arithmetic

TEST(FixedWrapping, AddAndSubtractWrapAroundTheRange) {
    forBothFormats([](auto tag) {
        using T = decltype(tag);
        EXPECT_EQ(T::max() + T::epsilon(), T::min());
        EXPECT_EQ(T::min() - T::epsilon(), T::max());
    });
}

TEST(FixedWrapping, NegatingMinYieldsMin) {
    forBothFormats([](auto tag) {
        using T = decltype(tag);
        EXPECT_EQ(-T::min(), T::min());
    });
}

TEST(FixedArithmetic, AbsAndNegateFollowTwosComplement) {
    EXPECT_EQ(abs(fixed32::min()), fixed32::min());
    EXPECT_EQ(abs(-3.5_fx64), 3.5_fx64);
    EXPECT_EQ(-fixed32::max(), fixed32::min() + fixed32::epsilon());
}

TEST(FixedArithmetic, MultiplyWrapsOutOfRangeProducts) {
    EXPECT_EQ(fixed32(-256) * fixed32(128), fixed32::min()); // exactly -32768
    EXPECT_EQ(fixed32(300) * fixed32(300), fixed32(90000 - 65536));
    EXPECT_EQ(fixed64(65536) * fixed64(32768), fixed64::min());
    EXPECT_EQ(fixed64(-65536) * fixed64(-65536), fixed64::zero()); // 2^32 wraps to 0
    EXPECT_EQ(fixed64::min() * fixed64::min(), fixed64::zero());
    EXPECT_EQ(fixed32::min() * fixed32(-1), fixed32::min());
}

TEST(FixedArithmetic, CompoundAssignmentMatchesBinaryOperators) {
    fixed32 x = 1_fx32;
    x += 0.5_fx32;
    x -= 0.25_fx32;
    x *= 4_fx32;
    x /= 2_fx32;
    EXPECT_EQ(x, 2.5_fx32);
}

// ---------------------------------------------------------------------------------------------------------------------
// Multiplication and division rounding

TEST(FixedMultiplyTies, HalfUlpRoundsAwayFromZero) {
    forBothFormats([](auto tag) {
        using T = decltype(tag);
        const T eps = T::epsilon();
        EXPECT_EQ(eps * T::half(), eps);   // 0.5 ulp -> 1
        EXPECT_EQ(-eps * T::half(), -eps); // -0.5 ulp -> -1
    });
}

TEST(FixedMultiply, RoundsTiesAwayFromZero) {
    const fixed32 eps = fixed32::epsilon();
    const fixed32 h = fixed32::half();
    EXPECT_EQ(fixed32::fromRaw(3) * h, fixed32::fromRaw(2));   // 1.5 -> 2
    EXPECT_EQ(fixed32::fromRaw(-3) * h, fixed32::fromRaw(-2)); // -1.5 -> -2
    EXPECT_EQ(fixed32::fromRaw(5) * h, fixed32::fromRaw(3));   // 2.5 -> 3 (not to even)
    EXPECT_EQ(eps * 0.25_fx32, fixed32::zero());
    EXPECT_EQ(fixed32::fromRaw(5) * 0.375_fx32, fixed32::fromRaw(2)); // 1.875
    EXPECT_EQ(fixed32::fromRaw(-1) * 0.75_fx32, fixed32::fromRaw(-1));

    EXPECT_EQ(fixed64::fromRaw(-5) * fixed64::half(), fixed64::fromRaw(-3));
    EXPECT_EQ(fixed64::fromRaw(1) * fixed64::fromRaw(0x7FFF'FFFF), fixed64::zero()); // just under half an ulp
}

TEST(FixedDivision, RoundsToNearestTiesAwayFromZero) {
    EXPECT_EQ(fixed32::fromRaw(1) / 2_fx32, fixed32::fromRaw(1));
    EXPECT_EQ(fixed32::fromRaw(-1) / 2_fx32, fixed32::fromRaw(-1));
    EXPECT_EQ(fixed32::fromRaw(3) / 2_fx32, fixed32::fromRaw(2));
    EXPECT_EQ(fixed32::fromRaw(5) / -2_fx32, fixed32::fromRaw(-3));
    EXPECT_EQ(fixed32::fromRaw(1) / 3_fx32, fixed32::zero());
    EXPECT_EQ(1_fx32 / 3_fx32, fixed32::fromRaw(21845));
    EXPECT_EQ(2_fx32 / 3_fx32, fixed32::fromRaw(43691));
    EXPECT_EQ(-2_fx32 / 3_fx32, fixed32::fromRaw(-43691));
    EXPECT_EQ(fixed64::fromRaw(1) / 2_fx64, fixed64::fromRaw(1));
    EXPECT_EQ(fixed64::fromRaw(-3) / 2_fx64, fixed64::fromRaw(-2));
}

TEST(FixedDivision, SaturatesOnOverflow) {
    EXPECT_EQ(30000_fx32 / fixed32::epsilon(), fixed32::max());
    EXPECT_EQ(-30000_fx32 / fixed32::epsilon(), fixed32::min());
    EXPECT_EQ(fixed32::min() / -1_fx32, fixed32::max());
    EXPECT_EQ(fixed32::min() / 1_fx32, fixed32::min());
    EXPECT_EQ(fixed64::min() / -1_fx64, fixed64::max());
    EXPECT_EQ(fixed64::max() / fixed64::epsilon(), fixed64::max());
    EXPECT_EQ(fixed64::min() / fixed64::epsilon(), fixed64::min());
}

#if !DG_ASSERTS_ENABLED
// Defined release behavior (asserts are on in Debug, where these abort; see FixedDeath).
TEST(FixedDivision, ByZeroSaturatesWhenAssertsAreOff) {
    volatile i32 zeroRaw = 0;
    const fixed32 zero = fixed32::fromRaw(zeroRaw);
    EXPECT_EQ(1_fx32 / zero, fixed32::max());
    EXPECT_EQ(-1_fx32 / zero, fixed32::min());
    EXPECT_EQ(zero / zero, fixed32::zero());
    EXPECT_EQ(fixed64(5) / fixed64(zero), fixed64::max());
}

TEST(FixedSqrt, OfNegativeValueIsZeroWhenAssertsAreOff) { EXPECT_EQ(sqrt(-1_fx32), fixed32::zero()); }
#endif

TEST(FixedDivision, Fixed64MatchesExactRationalQuotients) {
    // Small rationals p / q: the expected raw value round(p * 2^32 / q) fits comfortably in 64 bits.
    const std::array<i64, 10> numerators = {1, 2, -7, 22, 355, -1000, 65535, 3, -1, 1'000'000};
    const std::array<i64, 8> denominators = {3, 7, -9, 113, 10, -6, 1024, 99991};
    for (i64 p : numerators) {
        for (i64 q : denominators) {
            const u64 n = static_cast<u64>(p < 0 ? -p : p) << 32;
            const u64 d = static_cast<u64>(q < 0 ? -q : q);
            const u64 m = n / d + ((n % d) * 2 >= d ? 1u : 0u);
            const i64 expected = ((p < 0) != (q < 0)) ? -static_cast<i64>(m) : static_cast<i64>(m);
            EXPECT_EQ((fixed64(p) / fixed64(q)).raw(), expected) << p << " / " << q;
        }
    }
}

TEST(FixedMultiply, Fixed64MatchesExactSmallProducts) {
    // Small raw values: the exact product fits in 64 bits.
    TestRng rng;
    for (int i = 0; i < 2000; ++i) {
        const auto a = static_cast<i64>(rng.next() >> 33) - (i64{1} << 30);
        const auto b = static_cast<i64>(rng.next() >> 33) - (i64{1} << 30);
        const u64 product = static_cast<u64>(a < 0 ? -a : a) * static_cast<u64>(b < 0 ? -b : b);
        const u64 m = (product + (u64{1} << 31)) >> 32;
        const i64 expected = ((a < 0) != (b < 0)) ? -static_cast<i64>(m) : static_cast<i64>(m);
        EXPECT_EQ((fixed64::fromRaw(a) * fixed64::fromRaw(b)).raw(), expected) << "raw " << a << " * raw " << b;
    }
}

TEST(FixedReference, MultiplyAndDivideMatchBigIntegerReferenceOverFullRange) {
    TestRng rng;
    Failures failures;
    auto checkPair = [&failures](auto a, auto b) {
        const auto describe = [&](const char* op) {
            return [=] { return std::format("{} {} {} (raw {} {} {})", a, op, b, hexRaw(a), op, hexRaw(b)); };
        };
        failures.record(a * b == referenceMul(a, b), describe("*"));
        failures.record(a / b == referenceDiv(a, b), describe("/"));
    };
    for (int i = 0; i < 3000; ++i) {
        // Mix full-range values with small ones so both saturation and ordinary results are exercised.
        const int shiftA = static_cast<int>(rng.next() % 64);
        const int shiftB = static_cast<int>(rng.next() % 64);
        const fixed64 a = fixed64::fromRaw(static_cast<i64>(rng.next()) >> shiftA);
        fixed64 b = fixed64::fromRaw(static_cast<i64>(rng.next()) >> shiftB);
        if (b.raw() == 0) {
            b = fixed64::epsilon();
        }
        checkPair(a, b);

        const fixed32 c = fixed32::fromRaw(static_cast<i32>(static_cast<u32>(rng.next())) >> (shiftA % 32));
        fixed32 d = fixed32::fromRaw(static_cast<i32>(static_cast<u32>(rng.next())) >> (shiftB % 32));
        if (d.raw() == 0) {
            d = fixed32::epsilon();
        }
        checkPair(c, d);
    }
    const std::array<fixed64, 6> edges = {fixed64::min(), fixed64::max(), fixed64::epsilon(), -fixed64::epsilon(),
                                          fixed64::one(), -fixed64::half()};
    for (fixed64 a : edges) {
        for (fixed64 b : edges) {
            checkPair(a, b);
        }
    }
    EXPECT_EQ(failures.count, 0) << "first failure: " << failures.first;
}

TEST(FixedDetail, DivWideMatchesShiftSubtractReference) {
    TestRng rng;
    Failures failures;
    for (int i = 0; i < 20000; ++i) {
        u64 d = rng.next() >> (rng.next() % 64);
        if (d == 0) {
            d = 1;
        }
        const detail::U128 n{rng.next() % d, rng.next()};
        u64 r = 0;
        u64 expectedR = 0;
        const u64 q = detail::divWide(n, d, r);
        const u64 expectedQ = referenceDivWide(n, d, expectedR);
        failures.record(q == expectedQ && r == expectedR, [&] {
            return std::format("{:#x}:{:#x} / {:#x}: got q={:#x} r={:#x}, expected q={:#x} r={:#x}", n.hi, n.lo, d, q,
                               r, expectedQ, expectedR);
        });
    }
    EXPECT_EQ(failures.count, 0) << "first failure: " << failures.first;
}

TEST(FixedDetail, MulWideOfMaxOperandsIsExact) {
    const detail::U128 p = detail::mulWide(~u64{0}, ~u64{0});
    EXPECT_EQ(p.hi, ~u64{0} - 1) << std::hex << p.hi;
    EXPECT_EQ(p.lo, 1u) << std::hex << p.lo;
}

// ---------------------------------------------------------------------------------------------------------------------
// Rounding functions

TEST(FixedRounding, FloorCeilRoundTruncFracOfNegativeValues) {
    EXPECT_EQ(floor(-2.5_fx32), -3_fx32);
    EXPECT_EQ(ceil(-2.5_fx32), -2_fx32);
    EXPECT_EQ(round(-2.5_fx32), -3_fx32);
    EXPECT_EQ(round(-2.25_fx32), -2_fx32);
    EXPECT_EQ(round(-2.75_fx32), -3_fx32);
    EXPECT_EQ(round(-0.5_fx32), -1_fx32);
    EXPECT_EQ(round(0.5_fx32), 1_fx32);
    EXPECT_EQ(round(2.5_fx64), 3_fx64);
    EXPECT_EQ(round(-fixed32::epsilon()), fixed32::zero());
    EXPECT_EQ(trunc(-2.5_fx32), -2_fx32);
    EXPECT_EQ(trunc(2.5_fx32), 2_fx32);
    EXPECT_EQ(frac(-2.25_fx32), 0.75_fx32);
    EXPECT_EQ(frac(2.25_fx64), 0.25_fx64);
}

TEST(FixedRounding, IntegersAndRangeEdges) {
    EXPECT_EQ(floor(-3_fx32), -3_fx32);
    EXPECT_EQ(ceil(-3_fx32), -3_fx32);
    EXPECT_EQ(round(-3_fx32), -3_fx32);
    EXPECT_EQ(floor(-fixed32::epsilon()), -1_fx32);
    EXPECT_EQ(ceil(-fixed32::epsilon()), fixed32::zero());
    EXPECT_EQ(floor(fixed32::min()), fixed32::min());
    EXPECT_EQ(ceil(fixed32::min()), fixed32::min());
    EXPECT_EQ(round(fixed32::min()), fixed32::min());
    EXPECT_EQ(ceil(fixed32::max()), fixed32::min()); // documented wrap: 32768 is not representable
    EXPECT_EQ(floor(fixed64::min() + fixed64::half()), fixed64::min());
}

// ---------------------------------------------------------------------------------------------------------------------
// sqrt

// One test, not two: the fixed64 sweep continues the fixed32 sweep's random sequence.
TEST(FixedSqrt, IsExactlyRoundedAcrossTheRange) {
    // Brute-force reference for fixed32: binary search for floor(sqrt(raw * 2^16)), then pick the nearer neighbor.
    auto reference = [](i32 raw) {
        const u64 n = static_cast<u64>(raw) << 16;
        u64 lo = 0;
        u64 hi = u64{1} << 24;
        while (lo < hi) {
            const u64 mid = (lo + hi + 1) / 2;
            if (mid * mid <= n) {
                lo = mid;
            } else {
                hi = mid - 1;
            }
        }
        return static_cast<i32>(((lo + 1) * (lo + 1) - n < n - lo * lo) ? lo + 1 : lo);
    };
    Failures failures;
    auto check32 = [&](i32 raw) {
        const i32 got = sqrt(fixed32::fromRaw(raw)).raw();
        const i32 expected = reference(raw);
        failures.record(got == expected,
                        [&] { return std::format("sqrt(raw {:#x}) = {:#x}, expected {:#x}", raw, got, expected); });
    };
    for (i32 raw = 0; raw <= 70000; ++raw) {
        check32(raw);
    }
    for (i32 raw = 70000; raw < 0x7FFF'0000; raw += 9973) {
        check32(raw);
    }
    TestRng rng;
    for (int i = 0; i < 20000; ++i) {
        check32(static_cast<i32>(rng.next() >> 33));
    }
    check32(0x7FFF'FFFF);
    EXPECT_EQ(failures.count, 0) << "first failure: " << failures.first;
    EXPECT_EQ(sqrt(fixed32::max()).raw(), 0xB504F3); // sqrt(2^47 - 2^16) * 2^-16 rounded

    // fixed64: s is correctly rounded iff (2s - 1)^2 <= 4n < (2s + 1)^2 with n = raw * 2^32.
    auto isRounded = [](i64 raw, i64 s) {
        const Big fourN = shiftLeftBig(Big::fromU64(static_cast<u64>(raw)), 34);
        const Big lower = Big::fromU64(static_cast<u64>(2 * s - 1));
        const Big upper = Big::fromU64(static_cast<u64>(2 * s + 1));
        return compare(multiply(lower, lower), fourN) <= 0 && compare(fourN, multiply(upper, upper)) < 0;
    };
    Failures failures64;
    auto check64 = [&](i64 raw) {
        const i64 got = sqrt(fixed64::fromRaw(raw)).raw();
        failures64.record(isRounded(raw, got), [&] { return std::format("sqrt(raw {:#x}) = {:#x}", raw, got); });
    };
    for (int i = 0; i < 2000; ++i) {
        const auto raw = static_cast<i64>(rng.next() >> (1 + rng.next() % 63));
        if (raw > 0) {
            check64(raw);
        }
    }
    for (i64 raw = 1; raw < 5000; ++raw) {
        check64(raw);
    }
    check64(fixed64::max().raw());
    EXPECT_EQ(failures64.count, 0) << "first failure: " << failures64.first;
    EXPECT_EQ(sqrt(fixed64::zero()), fixed64::zero());
    EXPECT_EQ(sqrt(9_fx64), 3_fx64);
    EXPECT_EQ(sqrt(0.25_fx64), 0.5_fx64);
}

// ---------------------------------------------------------------------------------------------------------------------
// lerp, min, max, clamp, comparison

// One test, not two: the interior sweep continues the endpoint sweep's random sequence.
TEST(FixedLerp, IsExactAtEndpointsAndRoundsOnceInside) {
    TestRng rng;
    Failures failures;
    for (int i = 0; i < 5000; ++i) {
        const fixed32 a = fixed32::fromRaw(static_cast<i32>(static_cast<u32>(rng.next())));
        const fixed32 b = fixed32::fromRaw(static_cast<i32>(static_cast<u32>(rng.next())));
        failures.record(lerp(a, b, fixed32::zero()) == a && lerp(a, b, fixed32::one()) == b,
                        [&] { return std::format("lerp({}, {}, 0 or 1)", a, b); });
        const fixed64 c = fixed64::fromRaw(static_cast<i64>(rng.next()));
        const fixed64 d = fixed64::fromRaw(static_cast<i64>(rng.next()));
        failures.record(lerp(c, d, fixed64::zero()) == c && lerp(c, d, fixed64::one()) == d,
                        [&] { return std::format("lerp({}, {}, 0 or 1)", c, d); });
    }
    EXPECT_EQ(failures.count, 0) << "first failure: " << failures.first;

    // Interior points over full-range spans: reference rounds the exact product (b - a) * t in i64, then adds it to a.
    // For t in [0, 1] the true result lies between a and b, so it is always representable.
    Failures interiorFailures;
    for (int i = 0; i < 20000; ++i) {
        const fixed32 a = fixed32::fromRaw(static_cast<i32>(static_cast<u32>(rng.next())));
        const fixed32 b = fixed32::fromRaw(static_cast<i32>(static_cast<u32>(rng.next())));
        const fixed32 t = fixed32::fromRaw(static_cast<i32>(rng.next() % 65537));
        const i64 product = (static_cast<i64>(b.raw()) - a.raw()) * t.raw();
        const i64 magnitude = ((product < 0 ? -product : product) + (i64{1} << 15)) >> 16;
        const i64 expected = a.raw() + (product < 0 ? -magnitude : magnitude);
        interiorFailures.record(lerp(a, b, t).raw() == expected,
                                [&] { return std::format("lerp({}, {}, {}) != raw {}", a, b, t, expected); });

        const fixed64 c = fixed64::fromRaw(static_cast<i64>(rng.next()));
        const fixed64 d = fixed64::fromRaw(static_cast<i64>(rng.next()));
        const fixed64 u = fixed64::fromRaw(static_cast<i64>(rng.next() % ((u64{1} << 32) + 1)));
        const fixed64 r = lerp(c, d, u);
        interiorFailures.record(!(r < min(c, d) || max(c, d) < r),
                                [&] { return std::format("lerp({}, {}, {}) = {} is outside the span", c, d, u, r); });
        // Halfway between c and d, from both sides: the two results differ by at most one ulp of rounding.
        const fixed64 mid = lerp(c, d, fixed64::half());
        const fixed64 midBack = lerp(d, c, fixed64::half());
        interiorFailures.record(!(abs(mid - midBack).raw() > 1),
                                [&] { return std::format("midpoints of {} and {}: {} vs {}", c, d, mid, midBack); });
    }
    EXPECT_EQ(interiorFailures.count, 0) << "first failure: " << interiorFailures.first;
}

TEST(FixedLerp, InterpolatesSpansWiderThanTheRange) {
    EXPECT_EQ(lerp(fixed64(-2'000'000'000), fixed64(2'000'000'000), 0.25_fx64), fixed64(-1'000'000'000));
    EXPECT_EQ(lerp(fixed32(30000), fixed32(-30000), 0.75_fx32), fixed32(-15000));
    EXPECT_EQ(lerp(fixed32::max(), fixed32::min(), fixed32::half()), -fixed32::epsilon()); // -0.5 ulp rounds away
    EXPECT_EQ(lerp(10_fx32, 20_fx32, 2_fx32), 30_fx32);                                     // extrapolation in range
    EXPECT_EQ(lerp(fixed32::min(), fixed32::max(), fixed32::one()), fixed32::max());
    EXPECT_EQ(lerp(fixed32::max(), fixed32::min(), fixed32::zero()), fixed32::max());
    EXPECT_EQ(lerp(-10_fx64, 10_fx64, 0.25_fx64), -5_fx64);
}

TEST(FixedMinMax, MinMaxClampAndComparisons) {
    EXPECT_EQ(min(-1_fx32, 1_fx32), -1_fx32);
    EXPECT_EQ(max(-1_fx64, 1_fx64), 1_fx64);
    EXPECT_EQ(clamp(-5_fx32, -1_fx32, 1_fx32), -1_fx32);
    EXPECT_EQ(clamp(0.5_fx32, -1_fx32, 1_fx32), 0.5_fx32);
    EXPECT_LT(1_fx32, 1.5_fx32);
    EXPECT_LE(-2_fx64, -2_fx64);
    EXPECT_GT(fixed32::max(), fixed32::min());
    EXPECT_TRUE((0.5_fx32 <=> 0.25_fx32) == std::strong_ordering::greater) << "0.5 <=> 0.25 must be greater";
}

// ---------------------------------------------------------------------------------------------------------------------
// Formatting

TEST(FixedFormat, DefaultFormatIsExactDecimal) {
    EXPECT_EQ(text(1.5_fx32), "1.5");
    EXPECT_EQ(text(-0.25_fx32), "-0.25");
    EXPECT_EQ(text(3_fx64), "3");
    EXPECT_EQ(text(fixed32::zero()), "0");
    EXPECT_EQ(text(fixed32::epsilon()), "0.0000152587890625");
    EXPECT_EQ(text(fixed32::min()), "-32768");
    EXPECT_EQ(text(fixed32::max()), "32767.9999847412109375");
    EXPECT_EQ(text(fixed64::epsilon()), "0.00000000023283064365386962890625");
    EXPECT_EQ(text(fixed64::min()), "-2147483648");
}

TEST(FixedFormat, PrecisionRoundsHalfAwayFromZero) {
    EXPECT_EQ(std::format("{:.2}", -2.125_fx32), "-2.13"); // tie away from zero
    EXPECT_EQ(std::format("{:.2}", 2.124_fx32), "2.12");
    EXPECT_EQ(std::format("{:.0}", 2.5_fx32), "3");
    EXPECT_EQ(std::format("{:.3}", 9.9996_fx32), "10.000"); // carry into the integer part
    EXPECT_EQ(std::format("{:.4}", 0.5_fx32), "0.5000");
    EXPECT_EQ(std::format("{:.10}", 1_fx64 / 3_fx64), "0.3333333333");
    EXPECT_EQ(std::format("{:.5}", fixed32::pi()), "3.14159");
    EXPECT_EQ(std::format("[{}|{:.1}]", 1.25_fx32, -1.25_fx64), "[1.25|-1.3]");
}

// ---------------------------------------------------------------------------------------------------------------------
// GOLDEN: ~100k mixed operations on fixed32 and fixed64 driven by a fixed xorshift sequence. Every result's raw bits
// are hashed. Every platform (x64 and ARM64, MSVC/clang-cl, AppleClang, NDK clang; Debug and Release) must reproduce
// this exact value; if it changes, the fixed-point semantics changed and every replay/state hash built on them did too.
TEST(FixedGolden, MixedOperationsHashMatchesRecordedValue) {
    constexpr u64 kGolden = 0x5088'80CD'2D9E'3341ull; // recorded on macOS arm64 (AppleClang), Debug and Release

    TestRng rng;
    std::array<fixed32, 8> a{};
    std::array<fixed64, 8> b{};
    for (usize i = 0; i < a.size(); ++i) {
        a[i] = fixed32::fromRaw(static_cast<i32>(static_cast<u32>(rng.next())));
        b[i] = fixed64::fromRaw(static_cast<i64>(rng.next()));
    }

    Fnv1a64 hash;
    for (int iteration = 0; iteration < 100'000; ++iteration) {
        const u64 r = rng.next();
        const auto x = static_cast<usize>((r >> 8) & 7u);
        const auto y = static_cast<usize>((r >> 11) & 7u);
        const auto z = static_cast<usize>((r >> 14) & 7u);
        const u64 noise = rng.next();
        switch (r % 14) {
        case 0:
            a[z] = a[x] + a[y];
            b[z] = b[x] + b[y];
            break;
        case 1:
            a[z] = a[x] - a[y];
            b[z] = b[x] - b[y];
            break;
        case 2:
            a[z] = a[x] * a[y];
            b[z] = b[x] * b[y];
            break;
        case 3:
            a[z] = a[x] / (a[y].raw() == 0 ? fixed32::one() : a[y]);
            b[z] = b[x] / (b[y].raw() == 0 ? fixed64::one() : b[y]);
            break;
        case 4:
            a[z] = sqrt(fixed32::fromRaw(a[x].raw() & 0x7FFF'FFFF));
            b[z] = sqrt(fixed64::fromRaw(b[x].raw() & 0x7FFF'FFFF'FFFF'FFFF));
            break;
        case 5:
            a[z] = lerp(a[x], a[y], fixed32::fromRaw(static_cast<i32>(noise & 0x1'FFFF)));
            b[z] = lerp(b[x], b[y], fixed64::fromRaw(static_cast<i64>(noise & 0x1'FFFF'FFFF)));
            break;
        case 6:
            a[z] = round(a[x]) + frac(a[y]);
            b[z] = floor(b[x]) - ceil(b[y]);
            break;
        case 7:
            a[z] = fixed32(b[x]);
            b[z] = b[y] + fixed64(a[x]);
            break;
        case 8:
            a[z] = -abs(a[x]) * fixed32::pi();
            b[z] = clamp(b[x], min(b[y], b[z]), max(b[y], b[z])) * fixed64::e();
            break;
        case 9:
            a[z] = fixed32(static_cast<i32>(noise));
            b[z] = fixed64(static_cast<i64>(noise >> 7)) / fixed64::twoPi();
            break;
        case 10:
            a[z] = fixed32::fromRaw(static_cast<i32>(static_cast<u32>(noise)));
            b[z] = fixed64::fromRaw(static_cast<i64>(noise));
            break;
        case 11:
            a[z] = fixed32::fromRaw(static_cast<i32>(static_cast<u32>(noise)) >> (noise % 31));
            b[z] = fixed64::fromRaw(static_cast<i64>(noise) >> (noise % 63));
            break;
        case 12:
            a[z] *= a[x] + fixed32::fromRaw(static_cast<i32>(noise & 0x3'FFFF));
            b[z] /= b[x].raw() == 0 ? fixed64::half() : b[x];
            break;
        default:
            a[z] = trunc(a[x]) - fixed32(a[y].toInt());
            b[z] = fixed64(b[x].toInt()) + fixed64(fixed32(b[y]));
            break;
        }
        hash.updateValue(a[z].raw());
        hash.updateValue(b[z].raw());
    }

    const u64 digest = hash.digest();
    EXPECT_EQ(digest, kGolden) << std::hex << std::showbase << "measured " << digest << ", golden " << kGolden;
}
