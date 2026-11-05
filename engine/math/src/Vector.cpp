// Out-of-line vector, quaternion and matrix operations: the heavier ones, kept here so they are compiled once with the
// engine's strict FP flags. Only IEEE + - * / sqrt and dg::detmath are used, in an explicit order.

#include "dg/math/Vector.h"

#include "dg/math/DetMath.h"
#include "dg/math/Matrix.h"
#include "dg/math/Quat.h"

namespace dg {

// ---- Quat ----

Quat Quat::fromAxisAngle(const Vec3& axis, f32 angle) {
    f32 s = 0.0f;
    f32 c = 0.0f;
    detmath::sincos(angle * 0.5f, s, c);
    return {axis.x * s, axis.y * s, axis.z * s, c};
}

Quat Quat::fromEulerXYZ(const Vec3& radians) {
    f32 sx = 0.0f;
    f32 cx = 0.0f;
    f32 sy = 0.0f;
    f32 cy = 0.0f;
    f32 sz = 0.0f;
    f32 cz = 0.0f;
    detmath::sincos(radians.x * 0.5f, sx, cx);
    detmath::sincos(radians.y * 0.5f, sy, cy);
    detmath::sincos(radians.z * 0.5f, sz, cz);
    const Quat qx{sx, 0.0f, 0.0f, cx};
    const Quat qy{0.0f, sy, 0.0f, cy};
    const Quat qz{0.0f, 0.0f, sz, cz};
    return qz * qy * qx;
}

Quat Quat::fromMat3(const Mat3& m) {
    // mRC = row R, column C.
    const f32 m00 = m.columns[0].x;
    const f32 m10 = m.columns[0].y;
    const f32 m20 = m.columns[0].z;
    const f32 m01 = m.columns[1].x;
    const f32 m11 = m.columns[1].y;
    const f32 m21 = m.columns[1].z;
    const f32 m02 = m.columns[2].x;
    const f32 m12 = m.columns[2].y;
    const f32 m22 = m.columns[2].z;

    // Shepperd: take the square root of the largest of 4w^2, 4x^2, 4y^2, 4z^2 so the divisor is never small.
    Quat q;
    const f32 trace = m00 + m11 + m22;
    if (trace > 0.0f) {
        const f32 s = detmath::sqrt(trace + 1.0f) * 2.0f;
        q = {(m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s};
    } else if (m00 > m11 && m00 > m22) {
        const f32 s = detmath::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        q = {0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s};
    } else if (m11 > m22) {
        const f32 s = detmath::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        q = {(m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s};
    } else {
        const f32 s = detmath::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        q = {(m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s};
    }
    return normalize(q);
}

Quat nlerp(const Quat& a, const Quat& b, f32 t) {
    const f32 sign = dot(a, b) < 0.0f ? -1.0f : 1.0f;
    const f32 wa = 1.0f - t;
    const f32 wb = t * sign;
    return normalize(Quat{a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb});
}

Quat slerp(const Quat& a, const Quat& b, f32 t) {
    // Above this cosine (about 1.8 degrees apart) sin(theta) is too small to divide by accurately, and nlerp is
    // indistinguishable from slerp anyway.
    constexpr f32 kNlerpThreshold = 0.9995f;

    f32 cosTheta = dot(a, b);
    Quat end = b;
    if (cosTheta < 0.0f) {
        cosTheta = -cosTheta;
        end = -b;
    }
    if (cosTheta > kNlerpThreshold) {
        return nlerp(a, end, t);
    }
    const f32 theta = detmath::acos(cosTheta);
    const f32 sinTheta = detmath::sin(theta);
    const f32 wa = detmath::sin((1.0f - t) * theta) / sinTheta;
    const f32 wb = detmath::sin(t * theta) / sinTheta;
    return {a.x * wa + end.x * wb, a.y * wa + end.y * wb, a.z * wa + end.z * wb, a.w * wa + end.w * wb};
}

Mat3 toMat3(const Quat& q) {
    const f32 xx = q.x * q.x;
    const f32 yy = q.y * q.y;
    const f32 zz = q.z * q.z;
    const f32 xy = q.x * q.y;
    const f32 xz = q.x * q.z;
    const f32 yz = q.y * q.z;
    const f32 wx = q.w * q.x;
    const f32 wy = q.w * q.y;
    const f32 wz = q.w * q.z;
    return {{
        {1.0f - 2.0f * (yy + zz), 2.0f * (xy + wz), 2.0f * (xz - wy)},
        {2.0f * (xy - wz), 1.0f - 2.0f * (xx + zz), 2.0f * (yz + wx)},
        {2.0f * (xz + wy), 2.0f * (yz - wx), 1.0f - 2.0f * (xx + yy)},
    }};
}

Mat4 toMat4(const Quat& q) { return Mat4::fromMat3(toMat3(q)); }

// ---- Mat3 ----

namespace {

// 1/det is safe to scale cofactors by: finite (det not denormal or NaN) and non-zero (det not inf).
bool usableReciprocal(f32 invDet) {
    const f32 a = detail::absF32(invDet);
    return a <= detail::kF32MaxFinite && a != 0.0f;
}

} // namespace

Mat3 inverse(const Mat3& m) {
    // The rows of the inverse are the cross products of column pairs, divided by the determinant.
    const Vec3 r0 = cross(m.columns[1], m.columns[2]);
    const Vec3 r1 = cross(m.columns[2], m.columns[0]);
    const Vec3 r2 = cross(m.columns[0], m.columns[1]);
    const f32 det = dot(m.columns[0], r0);
    // Guard on the reciprocal, not on det == 0: a denormal det makes 1/det inf and every zero cofactor 0*inf = NaN,
    // whose sign bit differs between x64 (0xFFC00000) and ARM64 (0x7FC00000).
    const f32 invDet = 1.0f / det;
    if (!usableReciprocal(invDet)) {
        return {{{}, {}, {}}};
    }
    return transpose(Mat3{{r0 * invDet, r1 * invDet, r2 * invDet}});
}

// ---- Mat4 ----

namespace {

// Row-major element names (aRC = row R, column C) and the 2x2 sub-determinants of rows 0-1 (s*) and rows 2-3 (c*)
// shared by determinant() and inverse() (Eberly, "The Laplace Expansion Theorem").
struct Laplace {
    f32 a00, a01, a02, a03;
    f32 a10, a11, a12, a13;
    f32 a20, a21, a22, a23;
    f32 a30, a31, a32, a33;
    f32 s0, s1, s2, s3, s4, s5;
    f32 c0, c1, c2, c3, c4, c5;
    f32 det;
};

Laplace laplace(const Mat4& m) {
    Laplace l{};
    l.a00 = m.columns[0].x;
    l.a10 = m.columns[0].y;
    l.a20 = m.columns[0].z;
    l.a30 = m.columns[0].w;
    l.a01 = m.columns[1].x;
    l.a11 = m.columns[1].y;
    l.a21 = m.columns[1].z;
    l.a31 = m.columns[1].w;
    l.a02 = m.columns[2].x;
    l.a12 = m.columns[2].y;
    l.a22 = m.columns[2].z;
    l.a32 = m.columns[2].w;
    l.a03 = m.columns[3].x;
    l.a13 = m.columns[3].y;
    l.a23 = m.columns[3].z;
    l.a33 = m.columns[3].w;

    l.s0 = l.a00 * l.a11 - l.a10 * l.a01;
    l.s1 = l.a00 * l.a12 - l.a10 * l.a02;
    l.s2 = l.a00 * l.a13 - l.a10 * l.a03;
    l.s3 = l.a01 * l.a12 - l.a11 * l.a02;
    l.s4 = l.a01 * l.a13 - l.a11 * l.a03;
    l.s5 = l.a02 * l.a13 - l.a12 * l.a03;

    l.c5 = l.a22 * l.a33 - l.a32 * l.a23;
    l.c4 = l.a21 * l.a33 - l.a31 * l.a23;
    l.c3 = l.a21 * l.a32 - l.a31 * l.a22;
    l.c2 = l.a20 * l.a33 - l.a30 * l.a23;
    l.c1 = l.a20 * l.a32 - l.a30 * l.a22;
    l.c0 = l.a20 * l.a31 - l.a30 * l.a21;

    l.det = l.s0 * l.c5 - l.s1 * l.c4 + l.s2 * l.c3 + l.s3 * l.c2 - l.s4 * l.c1 + l.s5 * l.c0;
    return l;
}

} // namespace

f32 determinant(const Mat4& m) { return laplace(m).det; }

Mat4 inverse(const Mat4& m) {
    const Laplace l = laplace(m);
    // Same reciprocal guard as inverse(Mat3).
    const f32 inv = 1.0f / l.det;
    if (!usableReciprocal(inv)) {
        return {{{}, {}, {}, {}}};
    }

    // bRC = row R, column C of the inverse.
    const f32 b00 = (l.a11 * l.c5 - l.a12 * l.c4 + l.a13 * l.c3) * inv;
    const f32 b01 = (-l.a01 * l.c5 + l.a02 * l.c4 - l.a03 * l.c3) * inv;
    const f32 b02 = (l.a31 * l.s5 - l.a32 * l.s4 + l.a33 * l.s3) * inv;
    const f32 b03 = (-l.a21 * l.s5 + l.a22 * l.s4 - l.a23 * l.s3) * inv;

    const f32 b10 = (-l.a10 * l.c5 + l.a12 * l.c2 - l.a13 * l.c1) * inv;
    const f32 b11 = (l.a00 * l.c5 - l.a02 * l.c2 + l.a03 * l.c1) * inv;
    const f32 b12 = (-l.a30 * l.s5 + l.a32 * l.s2 - l.a33 * l.s1) * inv;
    const f32 b13 = (l.a20 * l.s5 - l.a22 * l.s2 + l.a23 * l.s1) * inv;

    const f32 b20 = (l.a10 * l.c4 - l.a11 * l.c2 + l.a13 * l.c0) * inv;
    const f32 b21 = (-l.a00 * l.c4 + l.a01 * l.c2 - l.a03 * l.c0) * inv;
    const f32 b22 = (l.a30 * l.s4 - l.a31 * l.s2 + l.a33 * l.s0) * inv;
    const f32 b23 = (-l.a20 * l.s4 + l.a21 * l.s2 - l.a23 * l.s0) * inv;

    const f32 b30 = (-l.a10 * l.c3 + l.a11 * l.c1 - l.a12 * l.c0) * inv;
    const f32 b31 = (l.a00 * l.c3 - l.a01 * l.c1 + l.a02 * l.c0) * inv;
    const f32 b32 = (-l.a30 * l.s3 + l.a31 * l.s1 - l.a32 * l.s0) * inv;
    const f32 b33 = (l.a20 * l.s3 - l.a21 * l.s1 + l.a22 * l.s0) * inv;

    return {{{b00, b10, b20, b30}, {b01, b11, b21, b31}, {b02, b12, b22, b32}, {b03, b13, b23, b33}}};
}

Mat4 affineInverse(const Mat4& m) {
    const Mat3 linearInv = inverse(toMat3(m));
    const Vec3 t = -(linearInv * m.columns[3].xyz());
    return Mat4::fromMat3(linearInv, t);
}

Mat4 Mat4::rotation(const Quat& q) { return toMat4(q); }

Mat4 Mat4::trs(const Vec3& t, const Quat& r, const Vec3& s) {
    const Mat3 rot = toMat3(r);
    return {{vec4(rot.columns[0] * s.x, 0.0f), vec4(rot.columns[1] * s.y, 0.0f), vec4(rot.columns[2] * s.z, 0.0f),
             vec4(t, 1.0f)}};
}

Trs decompose(const Mat4& m) {
    Trs out;
    out.translation = m.columns[3].xyz();
    const Mat3 linear = toMat3(m);
    out.scale = {length(linear.columns[0]), length(linear.columns[1]), length(linear.columns[2])};
    if (determinant(linear) < 0.0f) {
        out.scale.x = -out.scale.x;
    }
    if (out.scale.x == 0.0f || out.scale.y == 0.0f || out.scale.z == 0.0f) {
        out.rotation = Quat::identity();
        return out;
    }
    const Mat3 rot{{linear.columns[0] / out.scale.x, linear.columns[1] / out.scale.y, linear.columns[2] / out.scale.z}};
    out.rotation = Quat::fromMat3(rot);
    return out;
}

Mat4 Mat4::lookAtRH(const Vec3& eye, const Vec3& target, const Vec3& up) {
    const Vec3 f = normalize(target - eye);
    const Vec3 s = normalize(cross(f, up));
    const Vec3 u = cross(s, f);
    // The rows of the rotation are the camera basis (right, up, back); the translation moves eye to the origin.
    return {{
        {s.x, u.x, -f.x, 0.0f},
        {s.y, u.y, -f.y, 0.0f},
        {s.z, u.z, -f.z, 0.0f},
        {-dot(s, eye), -dot(u, eye), dot(f, eye), 1.0f},
    }};
}

Mat4 Mat4::perspectiveRH(f32 fovY, f32 aspect, f32 zNear, f32 zFar) {
    const f32 focal = 1.0f / detmath::tan(fovY * 0.5f);
    // clip.z = a * zView + b and clip.w = -zView, so depth = (a * zView + b) / -zView: 1 at -zNear, 0 at -zFar.
    const f32 range = zFar - zNear;
    const f32 a = zNear / range;
    const f32 b = (zNear * zFar) / range;
    return {{
        {focal / aspect, 0.0f, 0.0f, 0.0f},
        {0.0f, -focal, 0.0f, 0.0f},
        {0.0f, 0.0f, a, -1.0f},
        {0.0f, 0.0f, b, 0.0f},
    }};
}

Mat4 Mat4::perspectiveInfiniteRH(f32 fovY, f32 aspect, f32 zNear) {
    const f32 focal = 1.0f / detmath::tan(fovY * 0.5f);
    // The zFar -> infinity limit of perspectiveRH: a = 0, b = zNear.
    return {{
        {focal / aspect, 0.0f, 0.0f, 0.0f},
        {0.0f, -focal, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, -1.0f},
        {0.0f, 0.0f, zNear, 0.0f},
    }};
}

Mat4 Mat4::orthographicRH(f32 left, f32 right, f32 bottom, f32 top, f32 zNear, f32 zFar) {
    const f32 width = right - left;
    const f32 height = top - bottom;
    const f32 depth = zFar - zNear;
    return {{
        {2.0f / width, 0.0f, 0.0f, 0.0f},
        {0.0f, -2.0f / height, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f / depth, 0.0f},
        {-(right + left) / width, (top + bottom) / height, zFar / depth, 1.0f},
    }};
}

} // namespace dg
