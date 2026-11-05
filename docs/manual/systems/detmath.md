---
title: Deterministic math (detmath)
section: Systems
order: 50
status: implemented
summary: dg::detmath, the engine's own f32 transcendental functions, which return bit-identical results on x64 and ARM64 and are the only math functions allowed in sim code.
sources: engine/math/include/dg/math/DetMath.h, engine/math/src/DetMath.cpp, tests/math/DetMathTest.cpp
---

## Overview

`dg::detmath` provides `sin`, `cos`, `tan`, their inverses, `exp`, `log`, `pow` and a few exact helpers for `f32`.
Every function returns the **same bits on every platform**, in every build configuration. It exists because of
determinism rule 2 (CLAUDE.md): sim code must not call `std::` transcendental math.

### Why `std::` math is banned in sim code

IEEE-754 requires `+ - * /` and `sqrt` to be correctly rounded, so they give identical results everywhere. It does
not require that for `sin`, `exp`, `pow` or `atan2`. Each C library (MSVC's CRT, Apple's libm, Android's bionic,
glibc) uses its own algorithm, and the results often differ in the last bit. In a lockstep or rollback game, one
differing bit in one angle is enough: positions drift apart over the following ticks, the state hashes no longer
match, and the session desyncs. Compilers can also replace or constant-fold library calls differently. detmath avoids
both problems by building every function from operations whose results IEEE-754 fully specifies.

### Key guarantees

* **Bit-identical results.** Only `+ - * /` on `f64`, comparisons, exact conversions, and library functions whose
  results are exact by definition (`sqrt`, `floor`, `ceil`, `trunc`, `round`, `fmod`, `fabs`, `frexp`, `ldexp`) are used.
  [DetMath.cpp](engine/math/src/DetMath.cpp) also disables FP contraction with a pragma, in addition to the engine-wide
  `-ffp-contract=off` / `/fp:precise` flags (see [Determinism model](../architecture/determinism.md)).
* **Out-of-line definitions.** The functions are compiled once, in the `math` module, so a caller's compile flags
  cannot change them.
* **Near-correct rounding.** Each `f32` function computes in `f64` and rounds once. A one-off exhaustive run over every
  finite `f32` input measured a maximum error of **0.500000 ulp** (to 6 digits) for `sin`, `cos`, `tan`, `asin`,
  `acos`, `atan`, `exp`, `exp2`, `log` and `log2`: correctly rounded except in rare near-halfway cases. `atan2` and
  `pow` (for results in the normal range) measured the same on multi-million-point random sweeps.
* **One canonical NaN.** Every NaN result has the bits `0x7FC00000`. Input NaN payloads are never propagated.

> **Note:** an **ulp** (unit in the last place) is the gap between a floating-point value and the next one of the same
> magnitude. An error of 0.5 ulp means the result is the representable value nearest to the exact answer.

detmath results are close to, but not always identical to, the C library's. Never mix the two in sim code: compute
the same quantity with detmath everywhere, including in tools that predict sim results.

## How to use

```cpp
#include "dg/math/DetMath.h"

namespace dm = dg::detmath;

// Heading angle of a 2D direction, and a point on a circle around a centre.
dg::f32 headingOf(dg::f32 dx, dg::f32 dy) { return dm::atan2(dy, dx); }

void pointOnCircle(dg::f32 radius, dg::f32 angle, dg::f32& outX, dg::f32& outY) {
    dg::f32 s = 0.0f;
    dg::f32 c = 0.0f;
    dm::sincos(angle, s, c); // one range reduction, same bits as dm::sin and dm::cos
    outX = radius * c;
    outY = radius * s;
}

// Exponential decay per tick: dt is the fixed sim step.
dg::f32 decay(dg::f32 value, dg::f32 ratePerSecond, dg::f32 dt) { return value * dm::exp(-ratePerSecond * dt); }
```

Common patterns:

* Use `dm::sqrt`, `dm::floor`, `dm::abs` and the other exact helpers instead of `<cmath>` in sim code. They give the
  same values as the standard functions (only NaN handling differs), so the sim never needs `<cmath>` at all.
* Use `sincos` when you need both values. It shares the range reduction and returns exactly the same bits as separate
  `sin` and `cos` calls.
* Angles are radians. The constants `dg::detmath::kPi`, `kTwoPi` and `kHalfPi` are `f32`.
* Presentation code (rendering, audio, UI) may use either library, but values that feed back into the sim must come
  from detmath. Vector, quaternion and projection functions in `dg::math` already use detmath internally
  (see [Vectors, quaternions and matrices](vector-math.md)).

### Assumed floating-point environment

detmath assumes the platform default: round-to-nearest-even, with flush-to-zero and denormals-are-zero both off.
Nothing in the engine changes it. Denormal `f32` inputs are fine, because they become normal numbers in `f64`.

## How to edit

| File | Contents |
|---|---|
| [DetMath.h](engine/math/include/dg/math/DetMath.h) | Declarations, constants and the accuracy contract |
| [DetMath.cpp](engine/math/src/DetMath.cpp) | Polynomials, range reductions and special-value handling |
| [DetMathTest.cpp](tests/math/DetMathTest.cpp) | Accuracy sweeps, special values, identities and the golden hash |

How the implementation works:

* **Range reduction.** `sin`, `cos` and `tan` reduce the argument modulo π/2. Below 2^19 they use a three-part
  Cody-Waite split of π/2 (fdlibm's constants), which is exact enough there. From 2^19 up they use an integer-only
  **Payne-Hanek reduction** with a 128-bit window of 2/π bits, which is exact for the whole `f32` range. `exp` uses a
  Cody-Waite split of ln 2; `exp2` reduces by the integer part, so `exp2` of an integer is exact.
* **Polynomials.** Truncated Taylor series with coefficients `1/n!` or `1/n` rounded to nearest `f64`, written as
  hex-float literals so the source pins the exact bits. On the reduced intervals the truncation error is below 1e-16
  relative.
* **Inverse trigonometry.** `atan` reduces to |u| ≤ tan(π/12). `asin` and `acos` are written in terms of `atan` in
  forms that avoid cancellation near ±1.
* **Logarithms.** `log` splits the argument into exponent and mantissa (`frexp`) and evaluates `2·atanh(s)`. `pow` is
  `exp(y · log|x|)` in `f64`, with the C99 Annex F special cases handled first.
* **Overflow.** `toF32` rounds the `f64` result to `f32` and maps values at or above FLT_MAX + ½ ulp to infinity
  explicitly, because converting an out-of-range `double` to `float` is undefined behaviour in C++.

### Adding a function deterministically

1. **Use only exactly specified operations**: `+ - * /` and `sqrt` in `f64`, comparisons, exact conversions, and
   `floor`/`ceil`/`trunc`/`round`/`fmod`/`fabs`/`frexp`/`ldexp`. Never call another library transcendental, `std::fma`, or a
   compiler intrinsic that may lower differently per platform.
2. **Fix the evaluation order.** Write polynomials in explicit Horner form, one operation per step. Do not rely on the
   compiler to group terms, and do not add anything that would need FP contraction or reassociation.
3. **Pin constants as hex floats** (`0x1.921fb54442d18p+1`), not decimal text, so their bits are visible in the source.
4. **Compute in `f64`, round once** to `f32` through `toF32`.
5. **Handle special values first**: NaN, infinities, signed zeros, domain errors, and overflow and underflow
   thresholds. Return `canonicalNaN()` for every NaN result.
6. **Define it out of line** in `DetMath.cpp`, declare it in `DetMath.h` in namespace `dg::detmath`, and document its
   domain and special values in the header comment.
7. **Test it**: add an accuracy sweep against the C library in `f64` (`measureUnary` or a random sweep), special-value
   tests that compare bits, and add the function to the golden hash in
   `DetMathDeterminism.GoldenBitHashMatchesAcrossPlatforms`. Record the new golden value and confirm it on x64 and
   ARM64 (see [Testing](#testing)).
8. Update this page.

> **Important:** changing the result of any existing function, even by one ulp for one input, changes sim state
> hashes and invalidates recorded replays. Update the golden constant only for an intentional change, and say so in
> the commit message.

## Reference

### Functions

All functions take and return `f32`.

| Function | Domain | Special values |
|---|---|---|
| `sin(x)`, `cos(x)` | Finite x | ±inf or NaN → NaN. `sin(±0) = ±0`. Accurate for huge arguments too (Payne-Hanek). |
| `sincos(x, outSin, outCos)` | Finite x | Writes the same bits as `sin(x)` and `cos(x)`; both NaN for non-finite x. |
| `tan(x)` | Finite x | ±inf or NaN → NaN. `tan(±0) = ±0`. Stays accurate next to the poles. |
| `asin(x)` | [-1, 1] | Outside → NaN. `asin(±1) = ±π/2`, `asin(±0) = ±0`. |
| `acos(x)` | [-1, 1] | Outside → NaN. `acos(1) = 0`, `acos(-1) = π`. |
| `atan(x)` | All | `atan(±inf) = ±π/2`, `atan(±0) = ±0`. |
| `atan2(y, x)` | All | C99 Annex F: `atan2(±0, +0) = ±0`, `atan2(±0, -0) = ±π`, `atan2(±inf, ±inf) = ±π/4` or `±3π/4`. Any NaN → NaN. |
| `exp(x)` | All | x > 89 → +inf, x < -110 → +0, `exp(-inf) = 0`. Between those clamps the `f64` core decides overflow and underflow. |
| `exp2(x)` | All | x ≥ 128 → +inf, x < -160 → +0. Exact for integer x in range. |
| `log(x)` | x ≥ 0 | x < 0 → NaN, `log(±0) = -inf`, `log(+inf) = +inf`. |
| `log2(x)` | x ≥ 0 | As `log`. Exact for powers of two. |
| `pow(base, exponent)` | See Annex F | `pow(x, ±0) = 1` and `pow(1, y) = 1` even for NaN; negative base with a non-integer exponent → NaN; `pow(±0, y)` and `pow(±inf, y)` follow Annex F signs; `pow(-1, ±inf) = 1`. |
| `sqrt(x)` | x ≥ 0 | x < 0 → NaN, `sqrt(-0) = -0`. Correctly rounded. |
| `floor(x)`, `ceil(x)`, `trunc(x)` | All | Exact. NaN → canonical NaN. |
| `round(x)` | All | Exact; halfway cases away from zero. NaN → canonical NaN. |
| `abs(x)` | All | Exact. NaN → canonical NaN. |
| `fmod(x, y)` | Finite x, y ≠ 0 | x infinite, y zero or any NaN → NaN. Exact. |

"NaN" in the table always means the canonical quiet NaN, bits `0x7FC00000`.

### Constants

| Constant | Value |
|---|---|
| `dg::detmath::kPi` | π as `f32` |
| `dg::detmath::kTwoPi` | 2π as `f32` |
| `dg::detmath::kHalfPi` | π/2 as `f32` |

### Canonical NaN

x64 and ARM64 generate and propagate NaN bits differently: the default NaN produced by an invalid operation has the
sign bit set on x64 (`0xFFC00000`) and clear on ARM64 (`0x7FC00000`), and payload propagation rules differ. A sim
state that contains a NaN would therefore hash differently on the two architectures even if every other value
matched. detmath returns `0x7FC00000` for every NaN result, whatever the input, so a NaN that does reach sim state
hashes the same everywhere. The vector and matrix code takes the same care (see
[Vectors, quaternions and matrices](vector-math.md)).

### Accuracy

| Measurement | Result |
|---|---|
| One-off exhaustive run over all finite `f32` inputs (sin, cos, tan, asin, acos, atan, exp, exp2, log, log2) | Max 0.500000 ulp |
| Multi-million-point random sweeps (atan2, and pow with results in the normal range) | Max 0.500000 ulp |
| Thresholds asserted by the test suite | 2 ulp for every function, 4 ulp for `pow` |

The test thresholds leave margin on purpose: they catch a broken polynomial or reduction without failing on a
platform-independent half-ulp case. The measured values come from the test output, which prints the maximum error and
the worst input for every sweep.

## Testing

[DetMathTest.cpp](tests/math/DetMathTest.cpp) builds the `dg-detmath-tests` executable, labelled `unit` and
`determinism`.

| Suite | What it checks |
|---|---|
| `DetMathAccuracy` | Strided sweeps over the bit patterns of each domain plus random sweeps, against the C library in `f64`. Includes huge arguments (Payne-Hanek), inputs next to the poles of `tan`, and every `f32` in [0.99, 1] for `asin`/`acos`. |
| `DetMathSpecialValues` | Bitwise checks of NaN inputs, domain errors, signed zeros, denormals, overflow boundaries, and the Annex F rules for `atan2` and `pow`. |
| `DetMathIdentities` | Symmetries and identities, and that `sincos` matches `sin` and `cos` bit for bit. |
| `DetMathDeterminism` | The golden hash. |

The C library appears only as an accuracy reference, computed in `f64` so its own error is negligible. It is never
part of a determinism check.

`DetMathDeterminism.GoldenBitHashMatchesAcrossPlatforms` evaluates every function over about 6500 inputs (awkward
values, neighbours of multiples of π/2, random bit patterns including NaN payloads, and random values in [-10, 10]),
the binary functions over a 160 × 160 grid, and 4096 extra `pow` cases. It hashes all result bits with FNV-1a. The
golden value `0x1BFF52C5DF7021AE` was recorded on macOS ARM64 (AppleClang) and is identical in Debug with
ASan/UBSan and in Release. x64 (MSVC, clang-cl, clang) and Android ARM64 must reproduce it exactly. CI runs the test
on Windows x64 and Linux x64 as well; the Android ARM64 job only builds, so the value has not yet been confirmed on
Android. The test prints the hash on every run, but ctest hides the output of passing tests: run `ctest` with `-V`,
or run the `dg-detmath-tests` binary directly, to see it.

```bash
./dg test -L determinism
ctest --test-dir build/<preset> -C Development -R dg-detmath-tests --output-on-failure
ctest --test-dir build/<preset> -C Development -R dg-detmath-tests -V   # also shows the printed golden hash
```

See [Testing](../tooling/testing.md) for labels and the golden-hash policy.
