#pragma once

#include "dg/core/Types.h"
#include "dg/math/DetMath.h"

#include <bit>
#include <limits>
#include <type_traits>

// f32 vectors shared by the sim and the renderer (DESIGN.md §5.2).
//
// Determinism: every operation is a fixed sequence of IEEE-754 + - * / and sqrt, written with an explicit evaluation
// order (dot is always x*x + y*y + z*z (+ w*w), left to right). Engine targets compile with -ffp-contract=off and
// /fp:precise, so no FMA contraction or reassociation can change the bits between x64 and ARM64. No <cmath>.
//
// Vectors are plain zero-initialized aggregates, so they are safe as sim component fields (determinism rule 8).
// Division is a true division per component (never a multiply by a reciprocal), so v / s matches the scalar maths.

namespace dg {

struct Vec2 {
    f32 x = 0.0f;
    f32 y = 0.0f;

    friend constexpr bool operator==(const Vec2&, const Vec2&) = default;
};

struct Vec3 {
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 z = 0.0f;

    friend constexpr bool operator==(const Vec3&, const Vec3&) = default;
};

struct Vec4 {
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 z = 0.0f;
    f32 w = 0.0f;

    [[nodiscard]] constexpr Vec3 xyz() const { return {x, y, z}; }

    friend constexpr bool operator==(const Vec4&, const Vec4&) = default;
};

static_assert(sizeof(Vec2) == 8 && sizeof(Vec3) == 12 && sizeof(Vec4) == 16);
static_assert(std::is_standard_layout_v<Vec2> && std::is_standard_layout_v<Vec3> && std::is_standard_layout_v<Vec4>);

/// Extends a Vec3 with a w component (1 for points, 0 for directions).
constexpr Vec4 vec4(const Vec3& v, f32 w) { return {v.x, v.y, v.z, w}; }

namespace detail {

// Clears the sign bit, so abs(-0) is +0 and the result never depends on how a compiler lowers a comparison.
constexpr f32 absF32(f32 v) { return std::bit_cast<f32>(std::bit_cast<u32>(v) & 0x7FFF'FFFFu); }
// a < b ? a : b. With a NaN operand the result is the second argument; NaNs never appear in well-formed sim data.
constexpr f32 minF32(f32 a, f32 b) { return b < a ? b : a; }
constexpr f32 maxF32(f32 a, f32 b) { return a < b ? b : a; }

inline constexpr f32 kF32MinNormal = std::numeric_limits<f32>::min();
inline constexpr f32 kF32MaxFinite = std::numeric_limits<f32>::max();

// True for a positive normal finite value: the range where a squared length, or a reciprocal, can be used without
// having underflowed (losing the direction or its precision) or overflowed. Also false for NaN.
constexpr bool isPositiveNormal(f32 v) { return v >= kF32MinNormal && v <= kF32MaxFinite; }

} // namespace detail

// ---- Vec2 ----

constexpr Vec2 operator+(const Vec2& a, const Vec2& b) { return {a.x + b.x, a.y + b.y}; }
constexpr Vec2 operator-(const Vec2& a, const Vec2& b) { return {a.x - b.x, a.y - b.y}; }
constexpr Vec2 operator*(const Vec2& a, const Vec2& b) { return {a.x * b.x, a.y * b.y}; }
constexpr Vec2 operator/(const Vec2& a, const Vec2& b) { return {a.x / b.x, a.y / b.y}; }
constexpr Vec2 operator*(const Vec2& a, f32 s) { return {a.x * s, a.y * s}; }
constexpr Vec2 operator*(f32 s, const Vec2& a) { return {s * a.x, s * a.y}; }
constexpr Vec2 operator/(const Vec2& a, f32 s) { return {a.x / s, a.y / s}; }
constexpr Vec2 operator-(const Vec2& a) { return {-a.x, -a.y}; }

constexpr Vec2& operator+=(Vec2& a, const Vec2& b) { return a = a + b; }
constexpr Vec2& operator-=(Vec2& a, const Vec2& b) { return a = a - b; }
constexpr Vec2& operator*=(Vec2& a, const Vec2& b) { return a = a * b; }
constexpr Vec2& operator/=(Vec2& a, const Vec2& b) { return a = a / b; }
constexpr Vec2& operator*=(Vec2& a, f32 s) { return a = a * s; }
constexpr Vec2& operator/=(Vec2& a, f32 s) { return a = a / s; }

constexpr f32 dot(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }
/// z component of the 3D cross product: positive when b is counter-clockwise from a.
constexpr f32 cross(const Vec2& a, const Vec2& b) { return a.x * b.y - a.y * b.x; }
constexpr Vec2 min(const Vec2& a, const Vec2& b) { return {detail::minF32(a.x, b.x), detail::minF32(a.y, b.y)}; }
constexpr Vec2 max(const Vec2& a, const Vec2& b) { return {detail::maxF32(a.x, b.x), detail::maxF32(a.y, b.y)}; }
constexpr Vec2 abs(const Vec2& a) { return {detail::absF32(a.x), detail::absF32(a.y)}; }

// ---- Vec3 ----

constexpr Vec3 operator+(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
constexpr Vec3 operator-(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
constexpr Vec3 operator*(const Vec3& a, const Vec3& b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
constexpr Vec3 operator/(const Vec3& a, const Vec3& b) { return {a.x / b.x, a.y / b.y, a.z / b.z}; }
constexpr Vec3 operator*(const Vec3& a, f32 s) { return {a.x * s, a.y * s, a.z * s}; }
constexpr Vec3 operator*(f32 s, const Vec3& a) { return {s * a.x, s * a.y, s * a.z}; }
constexpr Vec3 operator/(const Vec3& a, f32 s) { return {a.x / s, a.y / s, a.z / s}; }
constexpr Vec3 operator-(const Vec3& a) { return {-a.x, -a.y, -a.z}; }

constexpr Vec3& operator+=(Vec3& a, const Vec3& b) { return a = a + b; }
constexpr Vec3& operator-=(Vec3& a, const Vec3& b) { return a = a - b; }
constexpr Vec3& operator*=(Vec3& a, const Vec3& b) { return a = a * b; }
constexpr Vec3& operator/=(Vec3& a, const Vec3& b) { return a = a / b; }
constexpr Vec3& operator*=(Vec3& a, f32 s) { return a = a * s; }
constexpr Vec3& operator/=(Vec3& a, f32 s) { return a = a / s; }

constexpr f32 dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
/// Right-handed: cross({1,0,0}, {0,1,0}) == {0,0,1}.
constexpr Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
constexpr Vec3 min(const Vec3& a, const Vec3& b) {
    return {detail::minF32(a.x, b.x), detail::minF32(a.y, b.y), detail::minF32(a.z, b.z)};
}
constexpr Vec3 max(const Vec3& a, const Vec3& b) {
    return {detail::maxF32(a.x, b.x), detail::maxF32(a.y, b.y), detail::maxF32(a.z, b.z)};
}
constexpr Vec3 abs(const Vec3& a) { return {detail::absF32(a.x), detail::absF32(a.y), detail::absF32(a.z)}; }

// ---- Vec4 ----

constexpr Vec4 operator+(const Vec4& a, const Vec4& b) { return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w}; }
constexpr Vec4 operator-(const Vec4& a, const Vec4& b) { return {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w}; }
constexpr Vec4 operator*(const Vec4& a, const Vec4& b) { return {a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w}; }
constexpr Vec4 operator/(const Vec4& a, const Vec4& b) { return {a.x / b.x, a.y / b.y, a.z / b.z, a.w / b.w}; }
constexpr Vec4 operator*(const Vec4& a, f32 s) { return {a.x * s, a.y * s, a.z * s, a.w * s}; }
constexpr Vec4 operator*(f32 s, const Vec4& a) { return {s * a.x, s * a.y, s * a.z, s * a.w}; }
constexpr Vec4 operator/(const Vec4& a, f32 s) { return {a.x / s, a.y / s, a.z / s, a.w / s}; }
constexpr Vec4 operator-(const Vec4& a) { return {-a.x, -a.y, -a.z, -a.w}; }

constexpr Vec4& operator+=(Vec4& a, const Vec4& b) { return a = a + b; }
constexpr Vec4& operator-=(Vec4& a, const Vec4& b) { return a = a - b; }
constexpr Vec4& operator*=(Vec4& a, const Vec4& b) { return a = a * b; }
constexpr Vec4& operator/=(Vec4& a, const Vec4& b) { return a = a / b; }
constexpr Vec4& operator*=(Vec4& a, f32 s) { return a = a * s; }
constexpr Vec4& operator/=(Vec4& a, f32 s) { return a = a / s; }

constexpr f32 dot(const Vec4& a, const Vec4& b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
constexpr Vec4 min(const Vec4& a, const Vec4& b) {
    return {detail::minF32(a.x, b.x), detail::minF32(a.y, b.y), detail::minF32(a.z, b.z), detail::minF32(a.w, b.w)};
}
constexpr Vec4 max(const Vec4& a, const Vec4& b) {
    return {detail::maxF32(a.x, b.x), detail::maxF32(a.y, b.y), detail::maxF32(a.z, b.z), detail::maxF32(a.w, b.w)};
}
constexpr Vec4 abs(const Vec4& a) {
    return {detail::absF32(a.x), detail::absF32(a.y), detail::absF32(a.z), detail::absF32(a.w)};
}

// ---- Generic over Vec2 / Vec3 / Vec4 ----

template <typename V>
concept FloatVector = std::is_same_v<V, Vec2> || std::is_same_v<V, Vec3> || std::is_same_v<V, Vec4>;

template <FloatVector V>
constexpr f32 lengthSquared(const V& v) {
    return dot(v, v);
}

template <FloatVector V>
inline f32 length(const V& v) {
    return detmath::sqrt(dot(v, v));
}

template <FloatVector V>
inline f32 distance(const V& a, const V& b) {
    return length(b - a);
}

/// Largest absolute component.
template <FloatVector V>
constexpr f32 maxAbsComponent(const V& v) {
    const V a = abs(v);
    if constexpr (std::is_same_v<V, Vec2>) {
        return detail::maxF32(a.x, a.y);
    } else if constexpr (std::is_same_v<V, Vec3>) {
        return detail::maxF32(detail::maxF32(a.x, a.y), a.z);
    } else {
        return detail::maxF32(detail::maxF32(detail::maxF32(a.x, a.y), a.z), a.w);
    }
}

/// Unit-length copy of v for any finite non-zero v, however tiny or huge. The zero vector (and only it, among finite
/// inputs) maps to the zero vector; check lengthSquared first when "no direction" must be handled differently.
/// Non-finite input (inf/NaN components) gives a non-finite result.
template <FloatVector V>
inline V normalize(const V& v) {
    const f32 lengthSq = dot(v, v);
    if (detail::isPositiveNormal(lengthSq)) {
        return v / detmath::sqrt(lengthSq);
    }
    // dot(v, v) underflowed to zero/denormal or overflowed to inf: rescale so the largest component is exactly +-1,
    // which puts the squared length in [1, 4]. Rare, so the common path keeps its single division.
    const f32 m = maxAbsComponent(v);
    if (m == 0.0f) {
        return V{};
    }
    const V w = v / m;
    return w / detmath::sqrt(dot(w, w));
}

/// a*(1-t) + b*t: exact at both ends (t == 0 gives a, t == 1 gives b), unlike a + (b-a)*t.
template <FloatVector V>
constexpr V lerp(const V& a, const V& b, f32 t) {
    return a * (1.0f - t) + b * t;
}

/// Component-wise max(lo, min(v, hi)). Expects lo <= hi.
template <FloatVector V>
constexpr V clamp(const V& v, const V& lo, const V& hi) {
    return max(lo, min(v, hi));
}

/// True when every component differs by at most epsilon (absolute tolerance).
template <FloatVector V>
constexpr bool nearlyEqual(const V& a, const V& b, f32 epsilon) {
    const V e = abs(a - b);
    if constexpr (std::is_same_v<V, Vec2>) {
        return e.x <= epsilon && e.y <= epsilon;
    } else if constexpr (std::is_same_v<V, Vec3>) {
        return e.x <= epsilon && e.y <= epsilon && e.z <= epsilon;
    } else {
        return e.x <= epsilon && e.y <= epsilon && e.z <= epsilon && e.w <= epsilon;
    }
}

/// Scalar absolute-tolerance comparison.
constexpr bool nearlyEqual(f32 a, f32 b, f32 epsilon) { return detail::absF32(a - b) <= epsilon; }

} // namespace dg
