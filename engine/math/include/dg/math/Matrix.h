#pragma once

#include "dg/core/Types.h"
#include "dg/math/Quat.h"
#include "dg/math/Vector.h"

#include <type_traits>

// f32 3x3 and 4x4 matrices (DESIGN.md §5.2). Same determinism contract as Vector.h.
//
// Layout: column-major, matching Vulkan/GLSL/Slang `column_major` uniform layout, so a Mat4 can be copied to a GPU
// buffer as is. columns[c] is column c; the element in row r, column c is columns[c].{x,y,z,w}[r]. Vectors are column
// vectors and transforms compose right to left: (A * B) * v applies B first.
//
// Coordinate conventions: right-handed world and view space, the camera looks down -Z with +Y up. Projections target
// Vulkan clip space: depth range [0, 1] and **Y pointing down**. The projection matrices below flip Y themselves
// (view-space +Y maps to clip-space -Y), so the renderer uses a normal, positive-height viewport. Triangles that are
// counter-clockwise as seen by the camera stay counter-clockwise on screen: use VK_FRONT_FACE_COUNTER_CLOCKWISE.
// Depth is **reversed-Z** (near plane -> 1, far plane -> 0) for every projection, including orthographic, so one
// depth setup (clear to 0, VK_COMPARE_OP_GREATER_OR_EQUAL) serves all cameras.

namespace dg {

// Matrices default to the identity (not all zeros), so a default-constructed transform is a valid no-op.
struct Mat3 {
    Vec3 columns[3] = {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};

    static constexpr Mat3 identity() { return {}; }
    static constexpr Mat3 fromColumns(const Vec3& c0, const Vec3& c1, const Vec3& c2) { return {{c0, c1, c2}}; }

    friend constexpr bool operator==(const Mat3&, const Mat3&) = default;
};

struct Mat4 {
    Vec4 columns[4] = {
        {1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};

    static constexpr Mat4 identity() { return {}; }
    static constexpr Mat4 fromColumns(const Vec4& c0, const Vec4& c1, const Vec4& c2, const Vec4& c3) {
        return {{c0, c1, c2, c3}};
    }
    /// Embeds a 3x3 linear part with a translation column.
    static constexpr Mat4 fromMat3(const Mat3& m, const Vec3& t = {}) {
        return {{vec4(m.columns[0], 0.0f), vec4(m.columns[1], 0.0f), vec4(m.columns[2], 0.0f),
                 vec4(t, 1.0f)}};
    }

    static constexpr Mat4 translation(const Vec3& t) {
        return {{{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {t.x, t.y, t.z, 1.0f}}};
    }
    static constexpr Mat4 scale(const Vec3& s) {
        return {{{s.x, 0.0f, 0.0f, 0.0f}, {0.0f, s.y, 0.0f, 0.0f}, {0.0f, 0.0f, s.z, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}}};
    }
    /// Rotation matrix of a unit quaternion.
    static Mat4 rotation(const Quat& q);
    /// translation(t) * rotation(r) * scale(s): scales first, then rotates, then translates.
    static Mat4 trs(const Vec3& t, const Quat& r, const Vec3& s);

    /// Right-handed view matrix: the camera at `eye` looks towards `target`; `up` must not be parallel to the view
    /// direction. The result maps eye to the origin and the view direction to -Z.
    static Mat4 lookAtRH(const Vec3& eye, const Vec3& target, const Vec3& up);

    /// Perspective projection, reversed-Z, Vulkan clip space (see the file comment): view depth -zNear maps to 1 and
    /// -zFar to 0. fovY is the full vertical field of view in radians; aspect is width / height.
    /// Requires 0 < zNear < zFar.
    static Mat4 perspectiveRH(f32 fovY, f32 aspect, f32 zNear, f32 zFar);
    /// Same, with the far plane at infinity: depth = zNear / viewDistance, approaching 0 but never reaching it.
    static Mat4 perspectiveInfiniteRH(f32 fovY, f32 aspect, f32 zNear);
    /// Orthographic projection, reversed-Z, Vulkan clip space: view depth -zNear maps to 1 and -zFar to 0; Y is
    /// flipped like the perspective projections.
    static Mat4 orthographicRH(f32 left, f32 right, f32 bottom, f32 top, f32 zNear, f32 zFar);

    friend constexpr bool operator==(const Mat4&, const Mat4&) = default;
};

static_assert(sizeof(Mat3) == 36 && sizeof(Mat4) == 64);
static_assert(std::is_standard_layout_v<Mat3> && std::is_standard_layout_v<Mat4>);

/// Translation, rotation and scale of an affine transform (see decompose()).
struct Trs {
    Vec3 translation;
    Quat rotation;
    Vec3 scale{1.0f, 1.0f, 1.0f};
};

// ---- Mat3 ----

/// m * v = c0*v.x + c1*v.y + c2*v.z, summed left to right.
constexpr Vec3 operator*(const Mat3& m, const Vec3& v) {
    return m.columns[0] * v.x + m.columns[1] * v.y + m.columns[2] * v.z;
}

constexpr Mat3 operator*(const Mat3& a, const Mat3& b) {
    return {{a * b.columns[0], a * b.columns[1], a * b.columns[2]}};
}

constexpr Mat3 transpose(const Mat3& m) {
    const Vec3& c0 = m.columns[0];
    const Vec3& c1 = m.columns[1];
    const Vec3& c2 = m.columns[2];
    return {{{c0.x, c1.x, c2.x}, {c0.y, c1.y, c2.y}, {c0.z, c1.z, c2.z}}};
}

/// Scalar triple product dot(c0, cross(c1, c2)).
constexpr f32 determinant(const Mat3& m) { return dot(m.columns[0], cross(m.columns[1], m.columns[2])); }

/// Inverse via the adjugate. A singular or numerically singular matrix (1/determinant not finite or 0, e.g. a scale
/// of 1e-13, or a NaN determinant) returns the zero matrix instead of NaNs/infs, whose bit patterns differ between
/// x64 and ARM64. Entries large enough to overflow the cofactors (beyond ~1e19) are outside the contract.
Mat3 inverse(const Mat3& m);

/// Upper-left 3x3 block (the linear part of an affine transform).
constexpr Mat3 toMat3(const Mat4& m) {
    return {{m.columns[0].xyz(), m.columns[1].xyz(), m.columns[2].xyz()}};
}

// ---- Mat4 ----

/// m * v = c0*v.x + c1*v.y + c2*v.z + c3*v.w, summed left to right.
constexpr Vec4 operator*(const Mat4& m, const Vec4& v) {
    return m.columns[0] * v.x + m.columns[1] * v.y + m.columns[2] * v.z + m.columns[3] * v.w;
}

constexpr Mat4 operator*(const Mat4& a, const Mat4& b) {
    return {{a * b.columns[0], a * b.columns[1], a * b.columns[2], a * b.columns[3]}};
}

/// Transforms a point (w = 1) by an affine matrix; no perspective divide.
constexpr Vec3 transformPoint(const Mat4& m, const Vec3& p) {
    return (m.columns[0] * p.x + m.columns[1] * p.y + m.columns[2] * p.z + m.columns[3]).xyz();
}

/// Transforms a direction (w = 0): translation is ignored.
constexpr Vec3 transformVector(const Mat4& m, const Vec3& v) {
    return (m.columns[0] * v.x + m.columns[1] * v.y + m.columns[2] * v.z).xyz();
}

constexpr Mat4 transpose(const Mat4& m) {
    const Vec4& c0 = m.columns[0];
    const Vec4& c1 = m.columns[1];
    const Vec4& c2 = m.columns[2];
    const Vec4& c3 = m.columns[3];
    return {{{c0.x, c1.x, c2.x, c3.x}, {c0.y, c1.y, c2.y, c3.y}, {c0.z, c1.z, c2.z, c3.z}, {c0.w, c1.w, c2.w, c3.w}}};
}

f32 determinant(const Mat4& m);

/// General inverse (Laplace expansion over 2x2 sub-determinants). A singular or numerically singular matrix
/// (1/determinant not finite) returns the zero matrix instead of NaNs/infs, as inverse(Mat3) does. Entries large
/// enough to overflow the cofactors (beyond ~1e12) are outside the contract. Prefer affineInverse() for transforms
/// whose last row is (0, 0, 0, 1).
Mat4 inverse(const Mat4& m);

/// Inverse of an affine transform (last row (0, 0, 0, 1), which is assumed, not checked): inverts the 3x3 part and
/// maps the translation back. Cheaper and usually more accurate than inverse(). A (numerically) singular linear part
/// gives a zero linear part and a zero translation.
Mat4 affineInverse(const Mat4& m);

/// Splits an affine transform into translation(t) * rotation(r) * scale(s). Scale is the column lengths; a negative
/// determinant (mirroring) is reported as a negative scale.x. Shear is not representable and is folded into the
/// rotation estimate. If any scale component is 0 the rotation cannot be recovered and is the identity.
Trs decompose(const Mat4& m);

constexpr bool nearlyEqual(const Mat3& a, const Mat3& b, f32 epsilon) {
    return nearlyEqual(a.columns[0], b.columns[0], epsilon) && nearlyEqual(a.columns[1], b.columns[1], epsilon) &&
           nearlyEqual(a.columns[2], b.columns[2], epsilon);
}

constexpr bool nearlyEqual(const Mat4& a, const Mat4& b, f32 epsilon) {
    return nearlyEqual(a.columns[0], b.columns[0], epsilon) && nearlyEqual(a.columns[1], b.columns[1], epsilon) &&
           nearlyEqual(a.columns[2], b.columns[2], epsilon) && nearlyEqual(a.columns[3], b.columns[3], epsilon);
}

} // namespace dg
