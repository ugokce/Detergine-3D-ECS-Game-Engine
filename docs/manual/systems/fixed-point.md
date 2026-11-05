---
title: Fixed-point numbers
section: Systems
order: 40
status: implemented
summary: The fixed32 (Q16.16) and fixed64 (Q32.32) types, with every operation specified down to the last bit so gameplay values are identical on every platform.
sources: engine/math/include/dg/math/Fixed.h, engine/math/src/Fixed.cpp, tests/math/FixedTest.cpp
---

## Overview

`dg::fixed32` and `dg::fixed64` are two's complement **fixed-point** numbers: an integer that stores a value scaled
by a fixed power of two. They are meant for gameplay values that designers reason about in exact decimal terms and
that must never drift: hit points, currency, cooldowns, timers, resource counts. Vectors and physics use deterministic
`f32` instead (see [Vectors, quaternions and matrices](vector-math.md)); DESIGN.md §5.2 records this split.

| Type | Definition | Range | Resolution |
|---|---|---|---|
| `fixed32` | `Fixed<i32, 16>`, **Q16.16** | [-32768, 32768) | 2^-16 ≈ 0.0000153 |
| `fixed64` | `Fixed<i64, 32>`, **Q32.32** | [-2^31, 2^31) | 2^-32 ≈ 0.000000000233 |

The types live in the `math` module (`dg::math`, a `SIM` module, so it builds with the strict FP flags) and are
header-only apart from text formatting in [Fixed.cpp](engine/math/src/Fixed.cpp).

Key guarantees:

* **Bit-exact everywhere.** Every operation is integer arithmetic with a fully specified result. Nothing uses
  `__int128`, compiler builtins, or `long double`, so MSVC/clang-cl, AppleClang and the Android NDK produce the same
  bits. A golden hash over about 100 000 mixed operations enforces this.
* **One rounding rule.** Every operation that drops bits rounds to nearest, **ties away from zero**: multiplication,
  division, `sqrt`, `lerp`, narrowing conversions, `fromDouble`, literals and `{:.N}` formatting.
* **Defined overflow.** `+`, `-`, `*`, unary minus and integer construction **wrap** like two's complement integers.
  `/`, narrowing conversions, `fromDouble` and literals **saturate** to `min()` / `max()`.
* **Constexpr.** All arithmetic is `constexpr`, so the rules are also checked by `static_assert`s at compile time.
* **Safe as sim component fields.** A default-constructed value is zero, the type is trivially copyable, and its size
  equals its storage (4 or 8 bytes).

## How to use

Include the header and bring the literal operators into scope:

```cpp
#include "dg/math/Fixed.h"

#include <format>
#include <string>

using namespace dg::literals;

// Damage after armor, clamped at zero. Every step is exact or rounded once, identically on every platform.
dg::fixed32 applyDamage(dg::fixed32 hp, dg::fixed32 damage, dg::fixed32 armor) {
    const dg::fixed32 taken = damage * (1_fx32 - armor);
    return dg::max(hp - taken, dg::fixed32::zero());
}

std::string describeHealth(dg::fixed32 hp) {
    return std::format("HP {:.1}", hp); // "HP 72.5"
}
```

### Constructing values

| Form | Example | Behaviour |
|---|---|---|
| Default | `dg::fixed32 x;` | Zero. |
| Literal | `1.5_fx32`, `0.1_fx64`, `2e-3_fx32`, `1'000.25_fx32` | Exact decimal value, rounded once at compile time (`consteval`), saturating. |
| Integer | `dg::fixed32(3)`, `dg::fixed64(i64{-5})` | Explicit. Exact in range; out of range wraps modulo 2^(bits − fraction bits). |
| Raw bits | `dg::fixed32::fromRaw(6554)` | Stores the raw integer as is. |
| Floating point | `dg::fixed32::fromDouble(0.1)`, `fromFloat(0.5f)` | Authoring and load time only. Rounds, saturates, NaN becomes zero. |
| Other format | `dg::fixed64 w = x;` / `dg::fixed32(w)` | Widening is implicit and exact; narrowing is explicit, rounds and saturates. |

Literals are the preferred way to write constants. They are parsed digit by digit as exact decimal text, so
`0.1_fx32` is the same value on every compiler: it never goes through a `long double`, whose width differs between
x64 Linux (80 bit), Android ARM64 (128 bit) and MSVC/Apple (64 bit). Details:

* Literals are non-negative; apply unary minus for negative values. Because the literal saturates first,
  `-32768.0_fx32` is `-max()`, not `min()`. Use `dg::fixed32::min()` for the minimum.
* An integer literal with a leading zero is octal, as in C++: `010_fx32 == 8_fx32`. Floating forms are decimal:
  `010.5_fx32 == 10.5_fx32`. Hexadecimal and binary literals are a compile error.
* `40000_fx32 == dg::fixed32::max()`: out-of-range literals saturate.

Integer construction rejects `bool` and the character types (`char`, `wchar_t`, `char8_t`, `char16_t`, `char32_t`)
at compile time. `char` is signed on x64 and Apple ARM64 but unsigned on Android ARM64, so the same byte would give a
different value. Cast to `i8` or `u8` first.

```cpp
#include "dg/math/Fixed.h"

using namespace dg::literals;

static_assert(dg::fixed32(40000) == dg::fixed32(40000 - 65536)); // integer construction wraps
static_assert((0.1_fx32).raw() == 6554);                           // 0.1 * 65536 = 6553.6, rounded
static_assert(dg::fixed32(static_cast<dg::u8>(200)) == 200_fx32);

constexpr dg::fixed64 kWide = 1.25_fx32;               // implicit, exact
constexpr dg::fixed32 kNarrow = dg::fixed32(0.1_fx64); // explicit, rounded to nearest
static_assert(kWide == 1.25_fx64 && kNarrow == 0.1_fx32);
```

### Arithmetic and overflow

| Operation | Result | Overflow |
|---|---|---|
| `a + b`, `a - b` | Exact | Wraps: `max() + epsilon() == min()` |
| `-a` | Exact | Wraps: `-min() == min()` |
| `a * b` | Exact product rounded once, ties away from zero | Wraps: `fixed32(256) * fixed32(128) == min()` |
| `a / b` | Exact quotient rounded once, ties away from zero | Saturates to `min()` / `max()` |
| `+=`, `-=`, `*=`, `/=` | Same as the binary operator | Same as the binary operator |
| `<`, `<=`, `==`, `<=>`, ... | Compares the raw values | — |

Division by zero is a programmer error: `DG_ASSERT_MSG` fires with "fixed-point division by zero" in Debug and
Development. In Release, where asserts are compiled out, the result is defined: `max()` for a positive numerator,
`min()` for a negative one, and zero for `0 / 0`. Do not rely on the fallback; check the divisor in gameplay code.

> **Warning:** wrapping is deterministic but almost never what gameplay wants. A `fixed32` holds at most ±32768, so
> `fixed32(300) * fixed32(200)` wraps. Use `fixed64` for anything that can grow (scores, currency, accumulated
> time), or clamp before multiplying.

### Rounding helpers, sqrt and lerp

The free functions are in namespace `dg` and are found by argument-dependent lookup, so `floor(x)` works on a fixed
value without qualification.

```cpp
#include "dg/math/Fixed.h"

using namespace dg::literals;

static_assert(floor(-2.5_fx32) == -3_fx32 && ceil(-2.5_fx32) == -2_fx32);
static_assert(round(-2.5_fx32) == -3_fx32 && trunc(-2.5_fx32) == -2_fx32); // ties away from zero
static_assert(frac(-2.25_fx32) == 0.75_fx32);                               // always in [0, 1)
static_assert(sqrt(2_fx32).raw() == 92682);                                 // correctly rounded
static_assert(lerp(2_fx32, 4_fx32, dg::fixed32::half()) == 3_fx32);
static_assert(clamp(5_fx32, 0_fx32, 1_fx32) == 1_fx32);
```

* `sqrt` returns the correctly rounded square root: it takes the exact integer square root of `raw * 2^F`. Ties
  cannot occur. A negative argument fires `DG_ASSERT_MSG` ("fixed-point sqrt of a negative value"); with asserts off
  it returns zero.
* `lerp(a, b, t)` computes `b - a` and the product with `t` exactly in a wider type and rounds once. It is exact at
  `t == 0` and `t == 1`, and correct even when `b - a` itself does not fit:
  `lerp(fixed32::min(), fixed32::max(), half())` is zero. Results outside the range (for `t` outside [0, 1]) wrap.
* `clamp` asserts `lo <= hi`.

### Converting back

| Call | Result |
|---|---|
| `x.toInt()` | Integer part, rounded toward zero: `(-2.5_fx32).toInt() == -2`. Returns the storage type. |
| `x.raw()` | The raw storage integer. Use this to hash or serialize a value. |
| `x.toDouble()` | Exact for `fixed32`; for `fixed64` values with more than 53 significant bits, rounded to nearest even. |
| `x.toFloat()` | Divides in `f32`; exact only while the raw value has at most 24 significant bits. |

`toDouble`, `toFloat`, `fromDouble` and `fromFloat` are for tooling, display and asset loading. Per-tick sim math
stays in fixed point. `fromDouble` itself is deterministic (it only uses exact double operations), but a value that
round-trips through floating point is easy to get wrong.

### Formatting

`std::format` prints exact decimal text using integer arithmetic only, so logs and desync dumps show the same digits
on every platform:

| Spec | Output |
|---|---|
| `{}` | Every significant digit: `1.5`, `-0.25`, `3`, `32767.9999847412109375` for `fixed32::max()` |
| `{:.N}` | Exactly N fraction digits, rounded ties away from zero: `std::format("{:.2}", -2.125_fx32)` is `-2.13` |

No other format options exist. An unsupported spec in a compile-time format string is a compile error; in a runtime
format string (`std::vformat`) it asserts in development builds. With asserts off (Release, Shipping) the parser stops
before the closing `}`, so `std::vformat` throws `std::format_error`, which terminates the program where exceptions
are disabled. A bare `.` with no digits (`{:.}`) asserts as well and, with asserts off, formats with precision 0.

### Choosing fixed point or float

| Use fixed point when | Use `f32` (with detmath) when |
|---|---|
| The value is a count, amount or duration that designers specify in decimal (HP, gold, cooldown seconds) | The value is geometric: positions, directions, rotations, velocities |
| Repeated accumulation must not drift and equality tests must be exact | The value feeds physics or rendering, which work in `f32` |
| The range is known and small (`fixed32`) or moderate (`fixed64`) | The range is wide (very large and very small values in one quantity) |

Both are deterministic in Detergine; the choice is about range and exactness, not about safety.

> **Tip:** fixed point is exact for sums, not for every fraction. `fixed64(1) / fixed64(60)` rounds, so adding it 60
> times does not give exactly one second. Count ticks as an integer and multiply by the tick length when you need an
> exact elapsed time.

## How to edit

Everything lives in two files:

| File | Contents |
|---|---|
| [Fixed.h](engine/math/include/dg/math/Fixed.h) | `dg::Fixed`, the `fixed32`/`fixed64` aliases, free functions, literal parsing, the `std::formatter` specialization, and the `dg::detail` helpers (`U128`, `mulWide`, `divWide`, `isqrt`) |
| [Fixed.cpp](engine/math/src/Fixed.cpp) | `detail::formatFixed` (exact decimal text) and `detail::fixedCompileError` |

Invariants every change must keep:

* **Integer-only and portable.** No floating point inside arithmetic, no `__int128`, no `_umul128`/`__builtin_*`.
  The portable `detail::U128` with `mulWide` (four 32×32 partial products) and `divWide` (Hacker's Delight `divlu`)
  exists for exactly this reason.
* **No signed overflow.** Wrapping is done on the unsigned representation, because signed overflow and left shifts of
  negative values are undefined or implementation-defined in C++. Keep that pattern.
* **Ties away from zero** for every rounding operation, so all operations agree with each other and with literals.
* **`constexpr` arithmetic.** The compile-time tests in `FixedTest.cpp` depend on it.
* **Results are part of the sim contract.** Changing any result changes state hashes and breaks recorded replays. If
  a change is intentional, update the golden hash in the test, say so in the commit message, and treat it like a
  network-protocol change (CLAUDE.md, *Multiplayer & headless*).

To add an operation:

1. Write it as a `constexpr` template over `<std::signed_integral S, int F>` in namespace `dg`, using only integer
   arithmetic, and document its rounding and overflow behaviour in its doc comment.
2. Add `static_assert`s for the exact rules, a runtime test against a wide reference (see
   `FixedReference.MultiplyAndDivideMatchBigIntegerReferenceOverFullRange` for the pattern), and death tests for any
   new `DG_ASSERT`.
3. If the operation is meant for sim code, add it to the golden mix in `FixedGolden` and record the new hash on every
   platform (see [Testing](#testing)).
4. Update this page.

A new layout (for example Q24.8) is a new alias of `Fixed<Storage, FracBits>`. The class supports 32- and 64-bit
storage and `0 < FracBits < bits - 1`; `formatFixed` supports at most 60 fraction bits.

## Reference

### `dg::Fixed<Storage, FracBits>` members

| Member | Description |
|---|---|
| `StorageType`, `UnsignedType` | `Storage` and its unsigned counterpart |
| `kFracBits`, `kBits` | Fraction bits and total bits |
| `Fixed()` | Zero |
| `explicit Fixed(I value)` | From any integral type except `bool` and character types; wraps out of range |
| `Fixed(Fixed<S2, F2> other)` | Implicit when lossless (fixed32 → fixed64), explicit otherwise; rounds and saturates |
| `static fromRaw(Storage)`, `raw()` | Raw access |
| `static fromDouble(f64)`, `static fromFloat(f32)` | Rounds to nearest (ties away), saturates, NaN → zero |
| `toDouble()`, `toFloat()`, `toInt()` | Conversions (see [Converting back](#converting-back)) |
| `zero()`, `one()`, `half()`, `epsilon()` | 0, 1, 0.5 and the smallest positive value (raw 1) |
| `min()`, `max()` | Smallest and largest representable values |
| `pi()`, `twoPi()`, `e()` | Rounded to nearest from 64 fraction bits of the exact expansion (`fixed32::pi().raw() == 0x3243F`) |

### Free functions (namespace `dg`)

| Function | Behaviour |
|---|---|
| `floor(x)` | Largest integer ≤ x; never overflows |
| `ceil(x)` | Smallest integer ≥ x; wraps above the largest representable integer |
| `trunc(x)` | Toward zero; never overflows |
| `round(x)` | Nearest integer, ties away from zero; wraps at the top of the range |
| `frac(x)` | `x - floor(x)`, in [0, 1) also for negative x (GLSL `fract`) |
| `abs(x)` | Wraps: `abs(min()) == min()` |
| `min(a, b)`, `max(a, b)` | Usual meaning |
| `clamp(x, lo, hi)` | Asserts `lo <= hi` |
| `lerp(a, b, t)` | Exact intermediate, one rounding; exact at t = 0 and 1 |
| `sqrt(x)` | Correctly rounded; asserts x ≥ 0, returns zero for negative input with asserts off |

### Literals (namespace `dg::literals`)

| Literal | Type |
|---|---|
| `operator""_fx32` | `fixed32` |
| `operator""_fx64` | `fixed64` |

## Testing

[FixedTest.cpp](tests/math/FixedTest.cpp) builds the `dg-fixed-tests` executable, labelled `unit` and `determinism`.
It contains:

* Compile-time `static_assert`s for constants, literals, wrapping, rounding, conversions and the character-type
  rejection.
* Death tests (`FixedDeath.*`) for division by zero, negative `sqrt`, `divWide` preconditions and inverted `clamp`
  bounds. They exist only when asserts are enabled; the Release fallbacks are tested by
  `FixedDivision.ByZeroSaturatesWhenAssertsAreOff` and `FixedSqrt.OfNegativeValueIsZeroWhenAssertsAreOff`.
* Reference tests: `FixedReference.*` compares multiplication and division with exact big-integer results over the
  full range, and `FixedSqrt.IsExactlyRoundedAcrossTheRange` compares `sqrt` with a brute-force reference.
* `FixedGolden.MixedOperationsHashMatchesRecordedValue`: about 100 000 mixed `fixed32`/`fixed64` operations driven by
  a fixed xorshift sequence, with every result's raw bits hashed with FNV-1a. The golden value
  `0x508880CD2D9E3341` was recorded on macOS ARM64 (AppleClang) in Debug and Release. Every platform and
  configuration must reproduce it exactly; a mismatch means fixed-point semantics differ on that platform. CI runs the
  test on macOS ARM64, Windows x64 and Linux x64; the Android ARM64 job only builds, so the value has not yet been
  confirmed on Android.

```bash
./dg test -L determinism            # all determinism suites, Development configuration
./dg test release -L determinism    # Release: covers the asserts-off fallbacks
ctest --test-dir build/<preset> -C Development -R dg-fixed-tests --output-on-failure
```

See [Testing](../tooling/testing.md) for labels and the golden-hash policy.
