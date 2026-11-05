#pragma once

#include "dg/core/Types.h"

// Deterministic transcendental functions (CLAUDE.md determinism rule 2).
//
// Every function is built only from IEEE-754 operations that are exactly specified on every platform: + - * /,
// sqrt, comparisons, and exact float<->integer conversions. The results are therefore bit-identical on x64 and ARM64,
// in every build configuration. They are defined out of line (DetMath.cpp) so that a caller's compile flags can never
// change them. Accuracy is close to, but not the same as, the C library; never mix the two in sim code.
//
// Domain errors return a NaN (asin/acos outside [-1, 1], log of a negative number); results otherwise follow the usual
// conventions for special values (e.g. log(0) = -inf, exp of a large argument = +inf; pow and atan2 follow C99
// Annex F). Every NaN result is the canonical quiet NaN (bits 0x7FC00000): input payloads never propagate, because x64
// and ARM64 generate and propagate NaN bits differently.
//
// Accuracy, measured against the C library in f64 (tests/math/DetMathTest.cpp runs strided sweeps): the f32
// functions compute in f64 and round once. A one-off exhaustive run over every finite f32 input measured a max error
// of 0.500000 ulp (6 digits) for sin, cos, tan, asin, acos, atan, exp, exp2, log and log2, i.e. correctly rounded
// except in rare near-halfway cases. sin/cos/tan use an exact Payne-Hanek reduction above 2^19, so this holds for huge
// arguments too. atan2 and pow (results in the normal range) measured the same on multi-million-point random sweeps.
// Assumes the default FP environment: round-to-nearest-even, no flush-to-zero / denormals-are-zero.

namespace dg::detmath {

inline constexpr f32 kPi = 3.14159265358979323846f;
inline constexpr f32 kTwoPi = 6.28318530717958647692f;
inline constexpr f32 kHalfPi = 1.57079632679489661923f;

f32 sin(f32 x);
f32 cos(f32 x);
void sincos(f32 x, f32& outSin, f32& outCos);
f32 tan(f32 x);
f32 asin(f32 x);
f32 acos(f32 x);
f32 atan(f32 x);
f32 atan2(f32 y, f32 x);
f32 exp(f32 x);
f32 exp2(f32 x);
f32 log(f32 x);
f32 log2(f32 x);
f32 pow(f32 base, f32 exponent);

// IEEE-exact helpers, provided so sim code never needs <cmath>.
f32 sqrt(f32 x);
f32 floor(f32 x);
f32 ceil(f32 x);
f32 round(f32 x); // halfway cases away from zero
f32 trunc(f32 x);
f32 abs(f32 x);
f32 fmod(f32 x, f32 y);

} // namespace dg::detmath
