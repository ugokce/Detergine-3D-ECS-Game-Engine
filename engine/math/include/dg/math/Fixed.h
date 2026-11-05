#pragma once

#include "dg/core/Assert.h"
#include "dg/core/Types.h"

#include <algorithm>
#include <bit>
#include <compare>
#include <concepts>
#include <format>
#include <limits>
#include <string>
#include <type_traits>

// Deterministic fixed-point numbers for gameplay-critical sim values (HP, timers, currency) whose results must be
// bit-identical on every platform (DESIGN.md §5.2, CLAUDE.md determinism rules). Every operation is specified down to
// the last bit: rounding is always to nearest with ties away from zero, + - * wrap like two's complement integers, and
// / and narrowing conversions saturate. Nothing here relies on __int128 or compiler builtins, so MSVC/clang-cl, clang
// and the NDK all produce the same bits.

namespace dg {

namespace detail {

// Portable unsigned 128-bit integer: only the operations fixed64 needs. Member order (hi, lo) makes the defaulted
// comparison lexicographic, which is exactly unsigned 128-bit ordering.
struct U128 {
    u64 hi = 0;
    u64 lo = 0;

    friend constexpr auto operator<=>(const U128&, const U128&) = default;
};

constexpr U128 add(U128 a, U128 b) {
    const u64 lo = a.lo + b.lo;
    return {a.hi + b.hi + (lo < a.lo ? 1u : 0u), lo};
}

constexpr U128 sub(U128 a, U128 b) {
    return {a.hi - b.hi - (a.lo < b.lo ? 1u : 0u), a.lo - b.lo};
}

constexpr U128 shiftLeft(U128 a, int shift) {
    if (shift == 0) {
        return a;
    }
    if (shift >= 64) {
        return {a.lo << (shift - 64), 0};
    }
    return {(a.hi << shift) | (a.lo >> (64 - shift)), a.lo << shift};
}

constexpr U128 shiftRight(U128 a, int shift) {
    if (shift == 0) {
        return a;
    }
    if (shift >= 64) {
        return {0, a.hi >> (shift - 64)};
    }
    return {a.hi >> shift, (a.lo >> shift) | (a.hi << (64 - shift))};
}

// Number of significant bits (0 for zero).
constexpr int bitWidth(U128 a) {
    return a.hi != 0 ? 128 - std::countl_zero(a.hi) : 64 - std::countl_zero(a.lo);
}

// Full 64x64 -> 128 product from four 32x32 -> 64 partial products.
constexpr U128 mulWide(u64 a, u64 b) {
    constexpr u64 kMask = 0xFFFF'FFFFu;
    const u64 aLo = a & kMask;
    const u64 aHi = a >> 32;
    const u64 bLo = b & kMask;
    const u64 bHi = b >> 32;

    const u64 ll = aLo * bLo;
    const u64 lh = aLo * bHi;
    const u64 hl = aHi * bLo;
    const u64 hh = aHi * bHi;

    // Cannot overflow: each term is below 2^32, so the sum is below 3 * 2^32.
    const u64 middle = (ll >> 32) + (lh & kMask) + (hl & kMask);
    return {hh + (lh >> 32) + (hl >> 32) + (middle >> 32), (middle << 32) | (ll & kMask)};
}

// 128 / 64 -> 64 division (Hacker's Delight divlu, normalized two-digit long division in base 2^32). Requires
// n.hi < d so the quotient fits in 64 bits. All arithmetic is unsigned and wraps where the algorithm expects it to.
constexpr u64 divWide(U128 n, u64 d, u64& remainder) {
    DG_ASSERT(d != 0 && n.hi < d);
    constexpr u64 kBase = u64{1} << 32;
    constexpr u64 kMask = kBase - 1;

    const int shift = std::countl_zero(d);
    d <<= shift;
    const u64 dHi = d >> 32;
    const u64 dLo = d & kMask;

    const u64 n32 = (n.hi << shift) | (shift != 0 ? n.lo >> (64 - shift) : 0);
    const u64 n10 = n.lo << shift;
    const u64 n1 = n10 >> 32;
    const u64 n0 = n10 & kMask;

    u64 q1 = n32 / dHi;
    u64 rhat = n32 - q1 * dHi;
    while (q1 >= kBase || q1 * dLo > ((rhat << 32) | n1)) {
        --q1;
        rhat += dHi;
        if (rhat >= kBase) {
            break;
        }
    }

    const u64 n21 = (n32 << 32) + n1 - q1 * d;
    u64 q0 = n21 / dHi;
    rhat = n21 - q0 * dHi;
    while (q0 >= kBase || q0 * dLo > ((rhat << 32) | n0)) {
        --q0;
        rhat += dHi;
        if (rhat >= kBase) {
            break;
        }
    }

    remainder = ((n21 << 32) + n0 - q0 * d) >> shift;
    return (q1 << 32) + q0;
}

// Digit-by-digit integer square root: floor(sqrt(n)), with n - root^2 left in `remainder`.
constexpr u64 isqrt(u64 n, u64& remainder) {
    u64 root = 0;
    u64 bit = n == 0 ? 0 : u64{1} << ((std::bit_width(n) - 1) & ~1);
    while (bit != 0) {
        if (n >= root + bit) {
            n -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    remainder = n;
    return root;
}

constexpr U128 isqrt(U128 n, U128& remainder) {
    U128 root{};
    U128 bit = bitWidth(n) == 0 ? U128{} : shiftLeft(U128{0, 1}, (bitWidth(n) - 1) & ~1);
    while (bit != U128{}) {
        const U128 candidate = add(root, bit);
        if (n >= candidate) {
            n = sub(n, candidate);
            root = add(shiftRight(root, 1), bit);
        } else {
            root = shiftRight(root, 1);
        }
        bit = shiftRight(bit, 2);
    }
    remainder = n;
    return root;
}

// Integral types with the same meaning on every platform: excludes bool and the character types (plain char is signed
// on x64 and Apple arm64 but unsigned on Android/Linux ARM64; wchar_t is 16 bits on Windows and 32 bits elsewhere).
template <typename I>
concept FixedIntegral =
    std::integral<I> && !std::same_as<std::remove_cv_t<I>, bool> && !std::same_as<std::remove_cv_t<I>, char> &&
    !std::same_as<std::remove_cv_t<I>, wchar_t> && !std::same_as<std::remove_cv_t<I>, char8_t> &&
    !std::same_as<std::remove_cv_t<I>, char16_t> && !std::same_as<std::remove_cv_t<I>, char32_t>;

// Whether every value of one fixed-point layout is exactly representable in another. Lives outside the class because
// the class's own static members cannot be used in its constructor's explicit-specifier (the class is incomplete).
template <typename FromStorage, int FromFracBits, typename ToStorage, int ToFracBits>
inline constexpr bool kLosslessConversion =
    FromFracBits <= ToFracBits &&
    static_cast<int>(sizeof(FromStorage) * 8) - FromFracBits <= static_cast<int>(sizeof(ToStorage) * 8) - ToFracBits;

// Out of line (Fixed.cpp). Exact decimal text: integer arithmetic only, so logs and desync dumps print the same digits
// on every platform. precision < 0 prints every significant digit.
std::string formatFixed(u64 magnitude, bool negative, int fracBits, int precision);

// Never defined as constexpr: calling it during constant evaluation turns an invalid literal or format spec into a
// compile error without needing exceptions.
void fixedCompileError(const char* reason);

} // namespace detail

/// Two's complement fixed-point number with FracBits fractional bits stored in Storage (i32 or i64).
/// Rounding is to nearest with ties away from zero everywhere; see each operation for overflow behavior.
template <std::signed_integral Storage, int FracBits>
class Fixed {
    static_assert(sizeof(Storage) == 4 || sizeof(Storage) == 8, "Fixed supports 32- and 64-bit storage");
    static_assert(FracBits > 0 && FracBits < static_cast<int>(sizeof(Storage) * 8) - 1);

public:
    using StorageType = Storage;
    using UnsignedType = std::make_unsigned_t<Storage>;
    static constexpr int kFracBits = FracBits;
    static constexpr int kBits = static_cast<int>(sizeof(Storage) * 8);

    constexpr Fixed() = default;

    /// Exact for integers in range; out-of-range values wrap modulo 2^(kBits - kFracBits), like integer casts.
    /// Character types are rejected: char's signedness and wchar_t's width differ between platforms, so the same
    /// byte would give different values on x64 and Android ARM64. Cast to i8/u8 (or another fixed-width type) first.
    template <detail::FixedIntegral I>
    constexpr explicit Fixed(I value)
        : value(static_cast<Storage>(static_cast<UnsignedType>(static_cast<UnsignedType>(value) << FracBits))) {}

    /// Lossless widening (fixed32 -> fixed64) is implicit and exact. Narrowing is explicit, rounds to nearest (ties
    /// away from zero) and saturates to [min(), max()].
    template <typename OtherStorage, int OtherFracBits>
    constexpr explicit(!detail::kLosslessConversion<OtherStorage, OtherFracBits, Storage, FracBits>)
        Fixed(Fixed<OtherStorage, OtherFracBits> other)
        : value(convertRaw<OtherStorage, OtherFracBits>(other.raw())) {}

    [[nodiscard]] static constexpr Fixed fromRaw(Storage raw) {
        Fixed result;
        result.value = raw;
        return result;
    }

    [[nodiscard]] constexpr Storage raw() const { return value; }

    /// Authoring/load time only, never per-tick sim math. Rounds the exact value to nearest (ties away from zero),
    /// saturates out-of-range values and infinities, and maps NaN to zero. Uses only exact double operations (scaling
    /// by a power of two, truncation, subtracting the integer part), so it is deterministic too.
    [[nodiscard]] static constexpr Fixed fromDouble(f64 x) {
        if (x != x) {
            return zero();
        }
        constexpr f64 kScale = static_cast<f64>(u64{1} << FracBits);
        constexpr f64 kLimit = static_cast<f64>(u64{1} << (kBits - 1)); // 2^(kBits-1): first magnitude out of range
        const bool negative = x < 0.0;
        const f64 scaled = (negative ? -x : x) * kScale;
        if (scaled >= kLimit) {
            return negative ? min() : max();
        }
        u64 magnitude = static_cast<u64>(scaled);
        if (scaled - static_cast<f64>(magnitude) >= 0.5) {
            ++magnitude;
        }
        return fromMagnitudeSaturated(magnitude, negative);
    }

    /// See fromDouble; every f32 is exactly representable as f64, so this rounds the float's exact value.
    [[nodiscard]] static constexpr Fixed fromFloat(f32 x) { return fromDouble(static_cast<f64>(x)); }

    /// Display and tooling only: fixed64 values with more than 53 significant bits are rounded (IEEE nearest-even).
    [[nodiscard]] constexpr f64 toDouble() const {
        return static_cast<f64>(value) / static_cast<f64>(u64{1} << FracBits);
    }
    [[nodiscard]] constexpr f32 toFloat() const {
        return static_cast<f32>(value) / static_cast<f32>(u64{1} << FracBits);
    }

    /// Integer part, rounded toward zero.
    [[nodiscard]] constexpr Storage toInt() const {
        return static_cast<Storage>((value < 0 ? value + kFracMask : value) >> FracBits);
    }

    // Constants. Transcendental constants are rounded to nearest from 64 fractional bits of their hex expansion.
    [[nodiscard]] static constexpr Fixed zero() { return fromRaw(0); }
    [[nodiscard]] static constexpr Fixed one() { return fromRaw(static_cast<Storage>(Storage{1} << FracBits)); }
    [[nodiscard]] static constexpr Fixed half() { return fromRaw(static_cast<Storage>(Storage{1} << (FracBits - 1))); }
    [[nodiscard]] static constexpr Fixed epsilon() { return fromRaw(1); }
    [[nodiscard]] static constexpr Fixed max() { return fromRaw(std::numeric_limits<Storage>::max()); }
    [[nodiscard]] static constexpr Fixed min() { return fromRaw(std::numeric_limits<Storage>::min()); }
    [[nodiscard]] static constexpr Fixed pi() { return fromBinaryExpansion(3, 0x243F'6A88'85A3'08D3ull); }
    [[nodiscard]] static constexpr Fixed twoPi() { return fromBinaryExpansion(6, 0x487E'D511'0B46'11A6ull); }
    [[nodiscard]] static constexpr Fixed e() { return fromBinaryExpansion(2, 0xB7E1'5162'8AED'2A6Aull); }

    // Arithmetic is done on the unsigned representation: wrapping is well defined there, signed overflow is UB.
    friend constexpr Fixed operator+(Fixed a, Fixed b) {
        return fromRaw(static_cast<Storage>(static_cast<UnsignedType>(a.value) + static_cast<UnsignedType>(b.value)));
    }
    friend constexpr Fixed operator-(Fixed a, Fixed b) {
        return fromRaw(static_cast<Storage>(static_cast<UnsignedType>(a.value) - static_cast<UnsignedType>(b.value)));
    }
    /// Wrapping: -min() == min().
    friend constexpr Fixed operator-(Fixed a) {
        return fromRaw(static_cast<Storage>(UnsignedType{0} - static_cast<UnsignedType>(a.value)));
    }
    friend constexpr Fixed operator+(Fixed a) { return a; }

    /// Exact product rounded to nearest (ties away from zero); wraps on overflow.
    friend constexpr Fixed operator*(Fixed a, Fixed b) {
        const bool negative = (a.value < 0) != (b.value < 0);
        UnsignedType magnitude = 0;
        if constexpr (kBits == 32) {
            const u64 product = static_cast<u64>(magnitudeOf(a.value)) * magnitudeOf(b.value);
            magnitude = static_cast<UnsignedType>((product + (u64{1} << (FracBits - 1))) >> FracBits);
        } else {
            const detail::U128 product = detail::mulWide(magnitudeOf(a.value), magnitudeOf(b.value));
            const detail::U128 rounded = detail::add(product, detail::shiftLeft(detail::U128{0, 1}, FracBits - 1));
            magnitude = detail::shiftRight(rounded, FracBits).lo;
        }
        return fromRaw(static_cast<Storage>(negative ? UnsignedType{0} - magnitude : magnitude));
    }

    /// Exact quotient rounded to nearest (ties away from zero); saturates to min()/max() on overflow. Division by
    /// zero is a programmer error (DG_ASSERT); with asserts off it returns max() for a > 0, min() for a < 0 and zero
    /// for 0/0.
    friend constexpr Fixed operator/(Fixed a, Fixed b) {
        DG_ASSERT_MSG(b.value != 0, "fixed-point division by zero");
        if (b.value == 0) {
            return a.value > 0 ? max() : (a.value < 0 ? min() : zero());
        }
        const bool negative = (a.value < 0) != (b.value < 0);
        const u64 divisor = magnitudeOf(b.value);
        u64 quotient = 0;
        u64 remainder = 0;
        if constexpr (kBits == 32) {
            const u64 numerator = static_cast<u64>(magnitudeOf(a.value)) << FracBits;
            quotient = numerator / divisor;
            remainder = numerator % divisor;
        } else {
            const detail::U128 numerator = detail::shiftLeft(detail::U128{0, magnitudeOf(a.value)}, FracBits);
            if (numerator.hi >= divisor) {
                return negative ? min() : max();
            }
            quotient = detail::divWide(numerator, divisor, remainder);
        }
        // remainder >= divisor - remainder is 2 * remainder >= divisor without overflowing.
        if (remainder >= divisor - remainder) {
            if (quotient == ~u64{0}) {
                return negative ? min() : max();
            }
            ++quotient;
        }
        return fromMagnitudeSaturated(quotient, negative);
    }

    constexpr Fixed& operator+=(Fixed other) { return *this = *this + other; }
    constexpr Fixed& operator-=(Fixed other) { return *this = *this - other; }
    constexpr Fixed& operator*=(Fixed other) { return *this = *this * other; }
    constexpr Fixed& operator/=(Fixed other) { return *this = *this / other; }

    friend constexpr auto operator<=>(Fixed, Fixed) = default;
    friend constexpr bool operator==(Fixed, Fixed) = default;

private:
    template <std::signed_integral, int>
    friend class Fixed;

    static constexpr Storage kFracMask = static_cast<Storage>((Storage{1} << FracBits) - 1);

    static constexpr UnsignedType magnitudeOf(Storage raw) {
        return raw < 0 ? UnsignedType{0} - static_cast<UnsignedType>(raw) : static_cast<UnsignedType>(raw);
    }

    // Applies the sign to an unsigned magnitude, clamping to [min(), max()].
    static constexpr Fixed fromMagnitudeSaturated(u64 magnitude, bool negative) {
        constexpr u64 kMaxMagnitude = u64{1} << (kBits - 1); // |min()|
        if (negative) {
            return magnitude >= kMaxMagnitude ? min()
                                              : fromRaw(static_cast<Storage>(UnsignedType{0} -
                                                                             static_cast<UnsignedType>(magnitude)));
        }
        return magnitude >= kMaxMagnitude ? max() : fromRaw(static_cast<Storage>(magnitude));
    }

    static constexpr Fixed fromBinaryExpansion(u64 integerPart, u64 fraction64) {
        const u64 fraction = (fraction64 >> (64 - FracBits)) + ((fraction64 >> (63 - FracBits)) & 1u);
        return fromRaw(static_cast<Storage>((integerPart << FracBits) + fraction));
    }

    template <typename OtherStorage, int OtherFracBits>
    static constexpr Storage convertRaw(OtherStorage raw) {
        if constexpr (OtherFracBits <= FracBits) {
            static_assert(detail::kLosslessConversion<OtherStorage, OtherFracBits, Storage, FracBits>,
                          "conversion would drop integer bits");
            // Sign-extend first, then shift as unsigned (shifting a negative signed value left is not portable C++).
            return static_cast<Storage>(static_cast<UnsignedType>(static_cast<Storage>(raw))
                                        << (FracBits - OtherFracBits));
        } else {
            constexpr int drop = OtherFracBits - FracBits;
            const bool negative = raw < 0;
            const u64 magnitude = negative ? u64{0} - static_cast<u64>(static_cast<i64>(raw)) : static_cast<u64>(raw);
            // magnitude <= 2^63, so adding the rounding half cannot overflow.
            return fromMagnitudeSaturated((magnitude + (u64{1} << (drop - 1))) >> drop, negative).value;
        }
    }

    Storage value = 0;
};

/// Q16.16: range [-32768, 32768), resolution 2^-16.
using fixed32 = Fixed<i32, 16>;
/// Q32.32: range [-2^31, 2^31), resolution 2^-32.
using fixed64 = Fixed<i64, 32>;

/// Largest integer <= x. Never overflows.
template <std::signed_integral S, int F>
[[nodiscard]] constexpr Fixed<S, F> floor(Fixed<S, F> x) {
    using U = std::make_unsigned_t<S>;
    return Fixed<S, F>::fromRaw(static_cast<S>(static_cast<U>(x.raw()) & ~((U{1} << F) - 1)));
}

/// Smallest integer >= x. Wraps for x > the largest representable integer (as integer overflow would).
template <std::signed_integral S, int F>
[[nodiscard]] constexpr Fixed<S, F> ceil(Fixed<S, F> x) {
    return floor(x + Fixed<S, F>::fromRaw(static_cast<S>((S{1} << F) - 1)));
}

/// Toward zero. Never overflows.
template <std::signed_integral S, int F>
[[nodiscard]] constexpr Fixed<S, F> trunc(Fixed<S, F> x) {
    return x.raw() < 0 ? ceil(x) : floor(x);
}

/// Nearest integer, ties away from zero. Wraps for x >= the largest representable integer + 0.5.
template <std::signed_integral S, int F>
[[nodiscard]] constexpr Fixed<S, F> round(Fixed<S, F> x) {
    const Fixed<S, F> h = Fixed<S, F>::half();
    // ceil(x - 1/2) rounds negative ties down (away from zero); x - 1/2 cannot overflow for negative x.
    return x.raw() < 0 ? ceil(x - h) : floor(x + h);
}

/// x - floor(x): always in [0, 1), also for negative x (GLSL fract semantics).
template <std::signed_integral S, int F>
[[nodiscard]] constexpr Fixed<S, F> frac(Fixed<S, F> x) {
    return x - floor(x);
}

/// Wrapping: abs(min()) == min().
template <std::signed_integral S, int F>
[[nodiscard]] constexpr Fixed<S, F> abs(Fixed<S, F> x) {
    return x.raw() < 0 ? -x : x;
}

template <std::signed_integral S, int F>
[[nodiscard]] constexpr Fixed<S, F> min(Fixed<S, F> a, Fixed<S, F> b) {
    return b < a ? b : a;
}

template <std::signed_integral S, int F>
[[nodiscard]] constexpr Fixed<S, F> max(Fixed<S, F> a, Fixed<S, F> b) {
    return a < b ? b : a;
}

/// Requires lo <= hi (asserted).
template <std::signed_integral S, int F>
[[nodiscard]] constexpr Fixed<S, F> clamp(Fixed<S, F> x, Fixed<S, F> lo, Fixed<S, F> hi) {
    DG_ASSERT(!(hi < lo));
    return x < lo ? lo : (hi < x ? hi : x);
}

/// a + (b - a) * t with b - a and the product computed exactly in a wider type, then rounded once to nearest (ties
/// away from zero). The result is therefore correct whenever it is representable, even when b - a itself is not
/// (e.g. lerp(min(), max(), half()) is about 0), and exact at t = 0 and t = 1. Results outside the range (t outside
/// [0, 1]) wrap.
template <std::signed_integral S, int F>
[[nodiscard]] constexpr Fixed<S, F> lerp(Fixed<S, F> a, Fixed<S, F> b, Fixed<S, F> t) {
    using U = std::make_unsigned_t<S>;
    // |b - a| < 2^kBits always fits the unsigned type when taken as the larger minus the smaller (mod 2^kBits).
    const bool diffNegative = b < a;
    const U diffMagnitude =
        diffNegative ? static_cast<U>(static_cast<U>(a.raw()) - static_cast<U>(b.raw()))
                     : static_cast<U>(static_cast<U>(b.raw()) - static_cast<U>(a.raw()));
    const U tMagnitude = t.raw() < 0 ? static_cast<U>(U{0} - static_cast<U>(t.raw())) : static_cast<U>(t.raw());
    U step = 0;
    if constexpr (sizeof(S) == 4) {
        // < 2^32 * 2^31 + 2^15: fits in u64.
        const u64 product = static_cast<u64>(diffMagnitude) * tMagnitude;
        step = static_cast<U>((product + (u64{1} << (F - 1))) >> F);
    } else {
        // < 2^64 * 2^63 + 2^(F-1): fits in 128 bits.
        const detail::U128 product = detail::mulWide(diffMagnitude, tMagnitude);
        const detail::U128 rounded = detail::add(product, detail::shiftLeft(detail::U128{0, 1}, F - 1));
        step = detail::shiftRight(rounded, F).lo;
    }
    const bool stepNegative = diffNegative != (t.raw() < 0);
    return Fixed<S, F>::fromRaw(
        static_cast<S>(stepNegative ? static_cast<U>(a.raw()) - step : static_cast<U>(a.raw()) + step));
}

/// Square root rounded to nearest (computed as the exact integer square root of raw * 2^F). Negative input is a
/// domain error: DG_ASSERT, and zero with asserts off.
template <std::signed_integral S, int F>
[[nodiscard]] constexpr Fixed<S, F> sqrt(Fixed<S, F> x) {
    DG_ASSERT_MSG(x.raw() >= 0, "fixed-point sqrt of a negative value");
    if (x.raw() <= 0) {
        return Fixed<S, F>::zero();
    }
    // No ties are possible: (r + 1/2)^2 = r^2 + r + 1/4 is never an integer, so remainder > r means round up.
    if constexpr (sizeof(S) == 4) {
        u64 remainder = 0;
        const u64 root = detail::isqrt(static_cast<u64>(x.raw()) << F, remainder);
        return Fixed<S, F>::fromRaw(static_cast<S>(remainder > root ? root + 1 : root));
    } else {
        detail::U128 remainder{};
        const detail::U128 root = detail::isqrt(detail::shiftLeft({0, static_cast<u64>(x.raw())}, F), remainder);
        return Fixed<S, F>::fromRaw(static_cast<S>(remainder > root ? root.lo + 1 : root.lo));
    }
}

namespace detail {

// Exact decimal -> fixed conversion for literals, so 0.1_fx32 does not depend on how the platform rounds a long double
// literal (80-bit on x64 Linux, 128-bit on Android ARM64, 64-bit on MSVC/Apple). Digits are kept as decimal and the
// fraction is converted to binary by repeated doubling, which is exact.
template <typename T, char... Chars>
consteval T parseFixedLiteral() {
    constexpr char text[] = {Chars..., '\0'};
    constexpr int kMaxDigits = 160;
    char digits[kMaxDigits] = {};
    int digitCount = 0;
    int pointPosition = -1; // number of digits before the decimal point
    int exponent = 0;
    int i = 0;
    bool decimalForm = false; // has a '.' or an exponent, so a leading 0 does not mean octal
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X' || text[1] == 'b' || text[1] == 'B')) {
        fixedCompileError("fixed-point literals must be decimal");
    }
    for (; text[i] != '\0' && text[i] != 'e' && text[i] != 'E'; ++i) {
        if (text[i] == '\'') {
            continue;
        }
        if (text[i] == '.') {
            pointPosition = digitCount;
            decimalForm = true;
            continue;
        }
        if (text[i] < '0' || text[i] > '9' || digitCount == kMaxDigits) {
            fixedCompileError("invalid or too long fixed-point literal");
        }
        digits[digitCount++] = static_cast<char>(text[i] - '0');
    }
    if (pointPosition < 0) {
        pointPosition = digitCount;
    }
    if (text[i] != '\0') {
        decimalForm = true;
        ++i;
        const bool negativeExponent = text[i] == '-';
        if (text[i] == '-' || text[i] == '+') {
            ++i;
        }
        for (; text[i] != '\0'; ++i) {
            if (text[i] < '0' || text[i] > '9') {
                fixedCompileError("invalid fixed-point literal exponent");
            }
            exponent = exponent * 10 + (text[i] - '0');
            if (exponent > 1000) {
                fixedCompileError("fixed-point literal exponent out of range");
            }
        }
        exponent = negativeExponent ? -exponent : exponent;
    }
    pointPosition += exponent;

    // C++ reads an integer literal with a leading 0 as octal (010 == 8), so 010_fx32 must be 8 as well, matching
    // fromDouble(010). Floating forms such as 010.5 or 010e1 are decimal.
    const bool octal = !decimalForm && digitCount > 1 && digits[0] == 0;
    const u64 base = octal ? 8 : 10;
    if (octal) {
        for (int d = 0; d < digitCount; ++d) {
            if (digits[d] > 7) {
                fixedCompileError("invalid digit in octal fixed-point literal");
            }
        }
    }

    // Integer part, saturating: anything >= 2^(kBits-1) is out of range for every Fixed type anyway.
    constexpr u64 kSaturated = u64{1} << 63;
    u64 integerPart = 0;
    for (int d = 0; d < pointPosition; ++d) {
        const u64 digit = d < digitCount ? static_cast<u64>(digits[d]) : 0;
        integerPart = integerPart >= kSaturated / 10 ? kSaturated : integerPart * base + digit;
    }

    // Fraction digits (pointPosition may be negative: leading zeros after the point).
    char fraction[kMaxDigits + 1100] = {};
    int fractionCount = 0;
    for (int d = pointPosition; d < digitCount; ++d) {
        fraction[fractionCount++] = d < 0 ? char{0} : digits[d];
    }
    // Doubling the decimal fraction shifts its leading binary digit out as the carry: one carry per fraction bit, and
    // one more as the rounding bit (ties away from zero means round up whenever it is set).
    u64 fractionBits = 0;
    for (int bit = 0; bit <= T::kFracBits; ++bit) {
        int carry = 0;
        for (int d = fractionCount - 1; d >= 0; --d) {
            const int doubled = fraction[d] * 2 + carry;
            fraction[d] = static_cast<char>(doubled % 10);
            carry = doubled / 10;
        }
        fractionBits = (fractionBits << 1) | static_cast<u64>(carry);
    }
    const u64 rounded = (fractionBits >> 1) + (fractionBits & 1u);
    constexpr u64 kLimit = u64{1} << (T::kBits - 1 - T::kFracBits);
    if (integerPart >= kLimit) {
        return T::max();
    }
    const u64 magnitude = (integerPart << T::kFracBits) + rounded;
    if (magnitude >= (u64{1} << (T::kBits - 1))) {
        return T::max();
    }
    return T::fromRaw(static_cast<typename T::StorageType>(magnitude));
}

} // namespace detail

namespace literals {

/// 1.5_fx32, 0.1_fx64, 2e-3_fx32: the exact decimal value rounded to nearest (ties away from zero), saturating.
/// Integer literals with a leading 0 are octal as in C++ (010_fx32 == 8), hex/binary are rejected. Apply
/// unary minus for negative values (so -32768.0_fx32 is -max(), use fixed32::min() for the minimum).
template <char... Chars>
consteval fixed32 operator""_fx32() {
    return detail::parseFixedLiteral<fixed32, Chars...>();
}

template <char... Chars>
consteval fixed64 operator""_fx64() {
    return detail::parseFixedLiteral<fixed64, Chars...>();
}

} // namespace literals

} // namespace dg

/// Exact decimal output. "{}" prints every significant digit ("1.5", "-0.25", "3"); "{:.N}" prints exactly N fraction
/// digits, rounded to nearest with ties away from zero. No other format options are supported.
template <std::signed_integral Storage, int FracBits>
struct std::formatter<dg::Fixed<Storage, FracBits>, char> {
    int precision = -1;

    constexpr auto parse(std::format_parse_context& context) {
        auto it = context.begin();
        if (it != context.end() && *it == '.') {
            ++it;
            if (it == context.end() || *it < '0' || *it > '9') {
                dg::detail::fixedCompileError("expected digits after '.' in fixed-point format spec");
            }
            precision = 0;
            while (it != context.end() && *it >= '0' && *it <= '9') {
                precision = std::min(precision * 10 + (*it - '0'), 1000);
                ++it;
            }
        }
        if (it != context.end() && *it != '}') {
            dg::detail::fixedCompileError("unsupported fixed-point format spec");
        }
        return it;
    }

    template <typename FormatContext>
    auto format(dg::Fixed<Storage, FracBits> x, FormatContext& context) const {
        const bool negative = x.raw() < 0;
        const auto raw = static_cast<dg::u64>(static_cast<dg::i64>(x.raw()));
        const dg::u64 magnitude = negative ? dg::u64{0} - raw : raw;
        const std::string text = dg::detail::formatFixed(magnitude, negative, FracBits, precision);
        return std::copy(text.begin(), text.end(), context.out());
    }
};
