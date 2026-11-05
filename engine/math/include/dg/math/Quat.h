#pragma once

#include "dg/core/Types.h"
#include "dg/math/DetMath.h"
#include "dg/math/Vector.h"

#include <type_traits>

// f32 rotation quaternions (DESIGN.md §5.2). Same determinism contract as Vector.h: IEEE + - * / sqrt in a fixed
// order plus dg::detmath for the transcendentals, never <cmath>.
//
// Conventions: Hamilton product, right-handed, active rotations. (a * b) rotates by b first, then by a. Angles are in
// radians and positive angles rotate counter-clockwise when looking down the axis towards the origin.

namespace dg {

struct Mat3;
struct Mat4;

// Defaults to the identity rotation rather than all zeros: a zero quaternion is not a rotation.
struct Quat {
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 z = 0.0f;
    f32 w = 1.0f;

    static constexpr Quat identity() { return {0.0f, 0.0f, 0.0f, 1.0f}; }

    /// Rotation of `angle` radians about `axis`, which must be unit length.
    static Quat fromAxisAngle(const Vec3& axis, f32 angle);

    /// Euler angles in radians, applied as extrinsic rotations about the fixed X, then Y, then Z axes:
    /// q = qZ * qY * qX (equivalently intrinsic Z, Y', X''). This matches Mat4::rotation(qZ) * ... * rotation(qX).
    static Quat fromEulerXYZ(const Vec3& radians);

    /// Rotation part of an orthonormal, right-handed matrix (Shepperd's method, normalized). Declared here, defined
    /// with the matrix code; include dg/math/Matrix.h to call it.
    static Quat fromMat3(const Mat3& m);

    [[nodiscard]] constexpr Vec3 xyz() const { return {x, y, z}; }

    friend constexpr bool operator==(const Quat&, const Quat&) = default;
};

static_assert(sizeof(Quat) == 16 && std::is_standard_layout_v<Quat>);

/// Hamilton product: rotates by b, then by a.
constexpr Quat operator*(const Quat& a, const Quat& b) {
    return {
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
    };
}

constexpr Quat operator-(const Quat& q) { return {-q.x, -q.y, -q.z, -q.w}; }

constexpr f32 dot(const Quat& a, const Quat& b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
constexpr Quat conjugate(const Quat& q) { return {-q.x, -q.y, -q.z, q.w}; }

namespace detail {

constexpr f32 maxAbsComponent(const Quat& q) { return dg::maxAbsComponent(Vec4{q.x, q.y, q.z, q.w}); }

} // namespace detail

/// Inverse of any finite non-zero quaternion (equal to the conjugate for unit quaternions), including ones whose
/// squared length underflows or overflows f32. The zero quaternion maps to the identity rather than producing NaNs.
constexpr Quat inverse(const Quat& q) {
    const f32 lengthSq = dot(q, q);
    if (detail::isPositiveNormal(lengthSq)) {
        return {-q.x / lengthSq, -q.y / lengthSq, -q.z / lengthSq, q.w / lengthSq};
    }
    // Rescaled path, as in normalize(Vec): conj(q)/|q|^2 = conj(w) / |w|^2 / m with w = q / m.
    const f32 m = detail::maxAbsComponent(q);
    if (m == 0.0f) {
        return Quat::identity();
    }
    const Quat w{q.x / m, q.y / m, q.z / m, q.w / m};
    const f32 wLengthSq = dot(w, w);
    return {-w.x / wLengthSq / m, -w.y / wLengthSq / m, -w.z / wLengthSq / m, w.w / wLengthSq / m};
}

/// Unit-length copy of any finite non-zero q, however tiny or huge. The zero quaternion maps to the identity, so
/// callers never see a NaN for finite input.
inline Quat normalize(const Quat& q) {
    const f32 lengthSq = dot(q, q);
    if (detail::isPositiveNormal(lengthSq)) {
        const f32 len = detmath::sqrt(lengthSq);
        return {q.x / len, q.y / len, q.z / len, q.w / len};
    }
    const f32 m = detail::maxAbsComponent(q);
    if (m == 0.0f) {
        return Quat::identity();
    }
    const Quat w{q.x / m, q.y / m, q.z / m, q.w / m};
    const f32 len = detmath::sqrt(dot(w, w));
    return {w.x / len, w.y / len, w.z / len, w.w / len};
}

/// Rotates v by the unit quaternion q: v + w*t + cross(u, t) with t = 2*cross(u, v), u = q.xyz (15 mul, 15 add,
/// cheaper than q * v * conj(q)).
constexpr Vec3 rotate(const Quat& q, const Vec3& v) {
    const Vec3 u = q.xyz();
    const Vec3 t = cross(u, v) * 2.0f;
    return v + t * q.w + cross(u, t);
}

/// Normalized linear interpolation along the shortest path. Cheap but not constant velocity. Not bit-exact at t == 0:
/// the result is a renormalized, so it can differ from a by a few ulp when dot(a, a) is not exactly 1.
Quat nlerp(const Quat& a, const Quat& b, f32 t);

/// Spherical linear interpolation along the shortest path (b is negated when dot(a, b) < 0, so the result at t == 1
/// may be -b, which is the same rotation). Falls back to nlerp when the inputs are nearly parallel, where slerp's
/// sin(theta) denominator would lose all precision; on that path t == 0 inherits nlerp's few-ulp renormalization error
/// instead of returning a exactly. Inputs must be unit length.
Quat slerp(const Quat& a, const Quat& b, f32 t);

/// Rotation matrices of a unit quaternion. Declared here, defined with the matrix code; include dg/math/Matrix.h.
Mat3 toMat3(const Quat& q);
Mat4 toMat4(const Quat& q);

/// True when every component differs by at most epsilon. q and -q are the same rotation but are not nearly equal
/// here; compare rotate() results, or dot(a, b) against 1, when sign-agnostic equality is wanted.
constexpr bool nearlyEqual(const Quat& a, const Quat& b, f32 epsilon) {
    return nearlyEqual(Vec4{a.x, a.y, a.z, a.w}, Vec4{b.x, b.y, b.z, b.w}, epsilon);
}

} // namespace dg
