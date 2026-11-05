#include <gtest/gtest.h>

#include "dg/core/Hash.h"
#include "dg/math/DetMath.h"
#include "dg/math/Matrix.h"
#include "dg/math/Quat.h"
#include "dg/math/Vector.h"

#include <bit>
#include <cstdio>
#include <format>
#include <ostream>
#include <string>

// GoogleTest finds these through ADL, so failed EXPECT_EQ / Near assertions print components instead of raw bytes.
namespace dg {

inline void PrintTo(const Vec2& v, std::ostream* os) { *os << std::format("({}, {})", v.x, v.y); }
inline void PrintTo(const Vec3& v, std::ostream* os) { *os << std::format("({}, {}, {})", v.x, v.y, v.z); }
inline void PrintTo(const Vec4& v, std::ostream* os) { *os << std::format("({}, {}, {}, {})", v.x, v.y, v.z, v.w); }
inline void PrintTo(const Quat& q, std::ostream* os) { *os << std::format("quat({}, {}, {}, {})", q.x, q.y, q.z, q.w); }

inline void PrintTo(const Mat3& m, std::ostream* os) {
    *os << "cols[";
    for (const Vec3& c : m.columns) {
        PrintTo(c, os);
    }
    *os << "]";
}

inline void PrintTo(const Mat4& m, std::ostream* os) {
    *os << "cols[";
    for (const Vec4& c : m.columns) {
        PrintTo(c, os);
    }
    *os << "]";
}

} // namespace dg

using namespace dg;

namespace {

// xorshift64*: a local generator so this suite does not depend on dg::sim and its golden hash cannot be changed by Rng
// edits.
struct TestRng {
    u64 state = 0x2545'F491'4F6C'DD1Dull;

    constexpr u64 next() {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        return state * 0x2545'F491'4F6C'DD1Dull;
    }

    // Uniform in [lo, hi) from 24 random bits: exact in f32, so the inputs are identical on every platform.
    constexpr f32 uniform(f32 lo, f32 hi) {
        const f32 unit = static_cast<f32>(next() >> 40) * (1.0f / 16777216.0f);
        return lo + (hi - lo) * unit;
    }

    constexpr Vec3 vec3(f32 lo, f32 hi) { return {uniform(lo, hi), uniform(lo, hi), uniform(lo, hi)}; }

    Quat rotation() {
        const Vec3 axis = normalize(vec3(-1.0f, 1.0f));
        return Quat::fromAxisAngle(lengthSquared(axis) == 0.0f ? Vec3{0.0f, 0.0f, 1.0f} : axis,
                                   uniform(-detmath::kPi, detmath::kPi));
    }
};

constexpr Vec3 kX{1.0f, 0.0f, 0.0f};
constexpr Vec3 kY{0.0f, 1.0f, 0.0f};
constexpr Vec3 kZ{0.0f, 0.0f, 1.0f};

// dg::nearlyEqual (scalar or component-wise; the exact f32 predicate, not a double-promoted EXPECT_NEAR), reporting
// both values.
template <class T>
testing::AssertionResult Near(const T& a, const T& b, f32 epsilon) {
    if (nearlyEqual(a, b, epsilon)) {
        return testing::AssertionSuccess();
    }
    return testing::AssertionFailure() << testing::PrintToString(a) << " vs " << testing::PrintToString(b)
                                       << " (epsilon " << epsilon << ")";
}

// q and -q are the same rotation.
testing::AssertionResult SameRotation(const Quat& a, const Quat& b, f32 epsilon) {
    if (nearlyEqual(a, b, epsilon) || nearlyEqual(a, -b, epsilon)) {
        return testing::AssertionSuccess();
    }
    return testing::AssertionFailure() << testing::PrintToString(a) << " is not the rotation "
                                       << testing::PrintToString(b) << " (epsilon " << epsilon << ")";
}

f32 clipDepth(const Mat4& projection, f32 viewZ) {
    const Vec4 clip = projection * Vec4{0.0f, 0.0f, viewZ, 1.0f};
    return clip.z / clip.w;
}

// Compile-time checks: the inline operations are usable in constant expressions.
static_assert(Vec3{1.0f, 2.0f, 3.0f} + Vec3{4.0f, 5.0f, 6.0f} == Vec3{5.0f, 7.0f, 9.0f});
static_assert(dot(Vec3{1.0f, 2.0f, 3.0f}, Vec3{4.0f, 5.0f, 6.0f}) == 32.0f);
static_assert(cross(kX, kY) == kZ);
static_assert(lerp(Vec2{1.0f, 2.0f}, Vec2{3.0f, 6.0f}, 0.5f) == Vec2{2.0f, 4.0f});
static_assert(Mat4::identity() * Vec4{1.0f, 2.0f, 3.0f, 1.0f} == Vec4{1.0f, 2.0f, 3.0f, 1.0f});
static_assert(transformPoint(Mat4::translation({1.0f, 2.0f, 3.0f}), {1.0f, 1.0f, 1.0f}) == Vec3{2.0f, 3.0f, 4.0f});
static_assert((Quat::identity() * Quat::identity()) == Quat::identity());
static_assert(Vec2{} == Vec2{0.0f, 0.0f} && Vec4{} == Vec4{0.0f, 0.0f, 0.0f, 0.0f});

} // namespace

TEST(Vector, SupportsComponentWiseArithmeticAndCompoundAssignment) {
    Vec4 v{1.0f, 2.0f, 3.0f, 4.0f};
    v += Vec4{1.0f, 1.0f, 1.0f, 1.0f};
    EXPECT_EQ(v, (Vec4{2.0f, 3.0f, 4.0f, 5.0f}));
    v *= 2.0f;
    EXPECT_EQ(v, (Vec4{4.0f, 6.0f, 8.0f, 10.0f}));
    v /= Vec4{2.0f, 3.0f, 4.0f, 5.0f};
    EXPECT_EQ(v, (Vec4{2.0f, 2.0f, 2.0f, 2.0f}));
    v -= Vec4{1.0f, 0.0f, 1.0f, 0.0f};
    EXPECT_EQ(v, (Vec4{1.0f, 2.0f, 1.0f, 2.0f}));
    EXPECT_EQ(-v, (Vec4{-1.0f, -2.0f, -1.0f, -2.0f}));
    EXPECT_EQ((0.5f * Vec2{2.0f, 4.0f}), (Vec2{1.0f, 2.0f}));
    EXPECT_EQ((Vec3{3.0f, 6.0f, 9.0f} / 3.0f), (Vec3{1.0f, 2.0f, 3.0f}));
    EXPECT_EQ(vec4(Vec3{1.0f, 2.0f, 3.0f}, 1.0f).xyz(), (Vec3{1.0f, 2.0f, 3.0f}));
}

TEST(Vector, AlgebraicIdentitiesHoldForRandomInputs) {
    TestRng rng;
    for (int i = 0; i < 1000; ++i) {
        SCOPED_TRACE(std::format("iteration {}", i));
        const Vec3 a = rng.vec3(-100.0f, 100.0f);
        const Vec3 b = rng.vec3(-100.0f, 100.0f);
        // Exact: IEEE + and * are commutative, and rounding is sign-symmetric so a-b == -(b-a).
        EXPECT_EQ(a + b, b + a);
        EXPECT_EQ(a * b, b * a);
        EXPECT_EQ(dot(a, b), dot(b, a));
        EXPECT_EQ(cross(a, b), -cross(b, a));
        EXPECT_EQ(a - a, Vec3{});
        EXPECT_EQ(a * 1.0f, a);
        // Approximate: the cross product is orthogonal to both inputs, and Lagrange's identity.
        const Vec3 c = cross(a, b);
        const f32 scale = lengthSquared(a) * lengthSquared(b);
        EXPECT_LE(detmath::abs(dot(c, a)), 1e-5f * detmath::sqrt(scale) * length(a) + 1e-3f);
        EXPECT_LE(detmath::abs(dot(c, b)), 1e-5f * detmath::sqrt(scale) * length(b) + 1e-3f);
        const f32 ab = dot(a, b);
        EXPECT_TRUE(Near(lengthSquared(c), scale - ab * ab, 1e-5f * scale + 1e-3f));
    }
}

TEST(Vector, CrossProductIsRightHanded) {
    EXPECT_EQ(cross(kX, kY), kZ);
    EXPECT_EQ(cross(kY, kZ), kX);
    EXPECT_EQ(cross(kZ, kX), kY);
    EXPECT_EQ(cross(kY, kX), -kZ);
    EXPECT_EQ(cross(Vec2{1.0f, 0.0f}, Vec2{0.0f, 1.0f}), 1.0f);
}

TEST(Vector, LengthNormalizeDistanceLerpMinMaxAbsClampBehave) {
    EXPECT_EQ(length(Vec3{3.0f, 4.0f, 0.0f}), 5.0f);
    EXPECT_EQ(lengthSquared(Vec2{3.0f, 4.0f}), 25.0f);
    EXPECT_EQ(distance(Vec3{1.0f, 1.0f, 1.0f}, Vec3{1.0f, 4.0f, 5.0f}), 5.0f);
    EXPECT_EQ(normalize(Vec3{}), Vec3{});
    EXPECT_EQ(normalize(Vec4{}), Vec4{});
    EXPECT_EQ(normalize(Vec3{0.0f, -7.0f, 0.0f}), -kY);

    TestRng rng;
    for (int i = 0; i < 1000; ++i) {
        SCOPED_TRACE(std::format("iteration {}", i));
        const Vec3 v = rng.vec3(-1000.0f, 1000.0f);
        if (lengthSquared(v) > 1e-6f) {
            EXPECT_TRUE(Near(length(normalize(v)), 1.0f, 1e-6f));
        }
        const Vec3 w = rng.vec3(-1000.0f, 1000.0f);
        EXPECT_EQ(lerp(v, w, 0.0f), v);
        EXPECT_EQ(lerp(v, w, 1.0f), w);
    }

    const Vec3 a{1.0f, -2.0f, 3.0f};
    const Vec3 b{-1.0f, 5.0f, 2.0f};
    EXPECT_EQ(min(a, b), (Vec3{-1.0f, -2.0f, 2.0f}));
    EXPECT_EQ(max(a, b), (Vec3{1.0f, 5.0f, 3.0f}));
    EXPECT_EQ(abs(a), (Vec3{1.0f, 2.0f, 3.0f}));
    EXPECT_EQ(clamp(Vec3{-5.0f, 0.5f, 5.0f}, Vec3{}, Vec3{1.0f, 1.0f, 1.0f}), (Vec3{0.0f, 0.5f, 1.0f}));
    // abs clears the sign of -0 too, which only a bit-pattern comparison can see (-0.0f == 0.0f).
    EXPECT_EQ(std::bit_cast<u32>(abs(Vec2{-0.0f, 0.0f}).x), 0u);

    // nearlyEqual itself is under test here, so call it directly rather than through Near().
    EXPECT_TRUE(nearlyEqual(Vec3{1.0f, 2.0f, 3.0f}, Vec3{1.0f, 2.0f, 3.0001f}, 1e-3f));
    EXPECT_FALSE(nearlyEqual(Vec3{1.0f, 2.0f, 3.0f}, Vec3{1.0f, 2.0f, 3.01f}, 1e-3f));
    EXPECT_TRUE(nearlyEqual(1.0f, 1.0005f, 1e-3f));
}

TEST(Quat, AxisAngleRotationConjugateInverseAndCompositionAgree) {
    const Quat q = Quat::fromAxisAngle(kZ, detmath::kHalfPi);
    EXPECT_TRUE(Near(rotate(q, kX), kY, 1e-6f));
    EXPECT_TRUE(Near(rotate(q, kY), -kX, 1e-6f));
    EXPECT_EQ(rotate(Quat::identity(), Vec3{1.0f, 2.0f, 3.0f}), (Vec3{1.0f, 2.0f, 3.0f}));
    EXPECT_TRUE(Near(q * conjugate(q), Quat::identity(), 1e-6f));
    EXPECT_EQ(inverse(Quat{0.0f, 0.0f, 0.0f, 0.0f}), Quat::identity());
    EXPECT_EQ(normalize(Quat{0.0f, 0.0f, 0.0f, 0.0f}), Quat::identity());

    // A non-unit quaternion: q * inverse(q) is still the identity.
    const Quat scaled{0.2f, -0.4f, 0.6f, 1.6f};
    EXPECT_TRUE(Near(scaled * inverse(scaled), Quat::identity(), 1e-6f));

    TestRng rng;
    for (int i = 0; i < 500; ++i) {
        SCOPED_TRACE(std::format("iteration {}", i));
        const Quat a = rng.rotation();
        const Quat b = rng.rotation();
        const Vec3 v = rng.vec3(-10.0f, 10.0f);
        EXPECT_TRUE(Near(rotate(a * b, v), rotate(a, rotate(b, v)), 1e-4f));
        EXPECT_TRUE(Near(rotate(inverse(a), rotate(a, v)), v, 1e-4f));
        EXPECT_TRUE(Near(dot(a, a), 1.0f, 1e-5f));
    }
}

TEST(Quat, RotateMatchesTheRotationMatrix) {
    TestRng rng;
    for (int i = 0; i < 500; ++i) {
        SCOPED_TRACE(std::format("iteration {}", i));
        const Quat q = rng.rotation();
        const Vec3 v = rng.vec3(-10.0f, 10.0f);
        const Vec3 byQuat = rotate(q, v);
        EXPECT_TRUE(Near(byQuat, toMat3(q) * v, 1e-4f));
        EXPECT_TRUE(Near(byQuat, transformVector(toMat4(q), v), 1e-4f));
        EXPECT_TRUE(Near(byQuat, transformPoint(Mat4::rotation(q), v), 1e-4f));
        // Rotation matrices are orthonormal with determinant +1.
        const Mat3 m = toMat3(q);
        EXPECT_TRUE(Near(transpose(m) * m, Mat3::identity(), 1e-5f));
        EXPECT_TRUE(Near(determinant(m), 1.0f, 1e-5f));
        // Round trip through the matrix recovers the rotation.
        EXPECT_TRUE(SameRotation(Quat::fromMat3(m), q, 1e-5f));
    }
}

TEST(Quat, FromEulerXyzAppliesXThenYThenZ) {
    const Vec3 angles{0.3f, -1.1f, 2.0f};
    const Quat composed = Quat::fromAxisAngle(kZ, angles.z) * Quat::fromAxisAngle(kY, angles.y) *
                          Quat::fromAxisAngle(kX, angles.x);
    const Quat euler = Quat::fromEulerXYZ(angles);
    EXPECT_TRUE(Near(euler, composed, 1e-6f));

    const Vec3 v{1.0f, 2.0f, 3.0f};
    const Vec3 stepwise = rotate(Quat::fromAxisAngle(kZ, angles.z),
                                 rotate(Quat::fromAxisAngle(kY, angles.y), rotate(Quat::fromAxisAngle(kX, angles.x), v)));
    EXPECT_TRUE(Near(rotate(euler, v), stepwise, 1e-5f));
    EXPECT_TRUE(Near(Quat::fromEulerXYZ({}), Quat::identity(), 0.0f));
    EXPECT_TRUE(Near(rotate(Quat::fromEulerXYZ({detmath::kHalfPi, 0.0f, 0.0f}), kY), kZ, 1e-6f));
}

TEST(Quat, SlerpHitsEndpointsTakesTheShortestPathAndFallsBackToNlerp) {
    TestRng rng;
    for (int i = 0; i < 200; ++i) {
        SCOPED_TRACE(std::format("iteration {}", i));
        const Quat a = rng.rotation();
        const Quat b = rng.rotation();
        EXPECT_TRUE(SameRotation(slerp(a, b, 0.0f), a, 1e-6f));
        EXPECT_TRUE(SameRotation(slerp(a, b, 1.0f), b, 1e-5f));
        EXPECT_TRUE(Near(dot(slerp(a, b, 0.37f), slerp(a, b, 0.37f)), 1.0f, 1e-5f));
        // Shortest path: b and -b give the same result.
        EXPECT_TRUE(Near(slerp(a, b, 0.6f), slerp(a, -b, 0.6f), 1e-6f));
    }

    // The midpoint between identity and a 120 degree turn is a 60 degree turn.
    const f32 angle = 2.0f * detmath::kPi / 3.0f;
    const Quat mid = slerp(Quat::identity(), Quat::fromAxisAngle(kY, angle), 0.5f);
    EXPECT_TRUE(Near(mid, Quat::fromAxisAngle(kY, angle * 0.5f), 1e-6f));
    // Constant angular velocity: a quarter of the way is a 30 degree turn.
    const Quat quarter = slerp(Quat::identity(), Quat::fromAxisAngle(kY, angle), 0.25f);
    EXPECT_TRUE(Near(quarter, Quat::fromAxisAngle(kY, angle * 0.25f), 1e-6f));

    // Nearly parallel inputs take the nlerp path and still give a unit quaternion between them.
    const Quat a = Quat::fromAxisAngle(kX, 0.1f);
    const Quat b = Quat::fromAxisAngle(kX, 0.1001f);
    const Quat halfway = slerp(a, b, 0.5f);
    EXPECT_TRUE(Near(dot(halfway, halfway), 1.0f, 1e-6f));
    EXPECT_TRUE(Near(halfway, Quat::fromAxisAngle(kX, 0.10005f), 1e-6f));
    EXPECT_EQ(slerp(a, a, 0.5f), nlerp(a, a, 0.5f));
}

TEST(Matrix, MultiplicationTransposeAndDeterminantAreConsistent) {
    const Mat4 t = Mat4::translation({1.0f, 2.0f, 3.0f});
    const Mat4 s = Mat4::scale({2.0f, 3.0f, 4.0f});
    // Right to left: scale first, then translate.
    EXPECT_EQ(transformPoint(t * s, {1.0f, 1.0f, 1.0f}), (Vec3{3.0f, 5.0f, 7.0f}));
    EXPECT_EQ(transformPoint(s * t, {1.0f, 1.0f, 1.0f}), (Vec3{4.0f, 9.0f, 16.0f}));
    EXPECT_EQ(transformVector(t, {1.0f, 1.0f, 1.0f}), (Vec3{1.0f, 1.0f, 1.0f}));
    EXPECT_EQ(Mat4::identity() * t, t);
    EXPECT_EQ(transpose(transpose(t)), t);
    EXPECT_EQ(transpose(t).columns[0].w, 1.0f);
    EXPECT_EQ(determinant(s), 24.0f);
    EXPECT_EQ(determinant(toMat3(s)), 24.0f);
    EXPECT_EQ(determinant(t), 1.0f);

    TestRng rng;
    for (int i = 0; i < 200; ++i) {
        SCOPED_TRACE(std::format("iteration {}", i));
        Mat4 m;
        for (Vec4& c : m.columns) {
            c = {rng.uniform(-2.0f, 2.0f), rng.uniform(-2.0f, 2.0f), rng.uniform(-2.0f, 2.0f), rng.uniform(-2.0f, 2.0f)};
        }
        Mat4 n;
        for (Vec4& c : n.columns) {
            c = {rng.uniform(-2.0f, 2.0f), rng.uniform(-2.0f, 2.0f), rng.uniform(-2.0f, 2.0f), rng.uniform(-2.0f, 2.0f)};
        }
        const Vec4 v{rng.uniform(-2.0f, 2.0f), rng.uniform(-2.0f, 2.0f), rng.uniform(-2.0f, 2.0f), 1.0f};
        EXPECT_TRUE(Near((m * n) * v, m * (n * v), 1e-4f));
        EXPECT_TRUE(Near(transpose(m * n), transpose(n) * transpose(m), 1e-5f));
        EXPECT_TRUE(Near(determinant(transpose(m)), determinant(m), 1e-3f));
        EXPECT_TRUE(Near(determinant(m * n), determinant(m) * determinant(n), 1e-2f));
    }
}

TEST(Matrix, InvertsRandomWellConditionedMatrices) {
    TestRng rng;
    for (int i = 0; i < 500; ++i) {
        SCOPED_TRACE(std::format("iteration {}", i));
        // Strictly diagonally dominant, hence invertible with a small condition number.
        Mat4 m;
        for (usize c = 0; c < 4; ++c) {
            m.columns[c] = {rng.uniform(-1.0f, 1.0f), rng.uniform(-1.0f, 1.0f), rng.uniform(-1.0f, 1.0f),
                            rng.uniform(-1.0f, 1.0f)};
        }
        m.columns[0].x += 5.0f;
        m.columns[1].y += 5.0f;
        m.columns[2].z -= 5.0f;
        m.columns[3].w += 5.0f;
        const Mat4 inv = inverse(m);
        EXPECT_TRUE(Near(inv * m, Mat4::identity(), 1e-5f));
        EXPECT_TRUE(Near(m * inv, Mat4::identity(), 1e-5f));

        const Mat3 m3 = toMat3(m);
        EXPECT_TRUE(Near(inverse(m3) * m3, Mat3::identity(), 1e-5f));

        // Affine fast path agrees with the general inverse.
        const Mat4 affine = Mat4::trs(rng.vec3(-50.0f, 50.0f), rng.rotation(), rng.vec3(0.5f, 3.0f));
        EXPECT_TRUE(Near(affineInverse(affine) * affine, Mat4::identity(), 1e-5f));
        EXPECT_TRUE(Near(affineInverse(affine), inverse(affine), 1e-4f));
    }

    // Singular matrices return all zeros instead of NaNs.
    const Mat4 singular = Mat4::scale({1.0f, 0.0f, 1.0f});
    EXPECT_EQ(inverse(singular), (Mat4{{{}, {}, {}, {}}}));
    EXPECT_EQ(inverse(toMat3(singular)), (Mat3{{{}, {}, {}}}));
}

TEST(Matrix, TrsComposeAndDecomposeRoundTrip) {
    TestRng rng;
    for (int i = 0; i < 500; ++i) {
        SCOPED_TRACE(std::format("iteration {}", i));
        const Vec3 t = rng.vec3(-100.0f, 100.0f);
        const Quat r = rng.rotation();
        const Vec3 s = rng.vec3(0.1f, 10.0f);
        const Mat4 m = Mat4::trs(t, r, s);
        EXPECT_TRUE(Near(m, Mat4::translation(t) * Mat4::rotation(r) * Mat4::scale(s), 1e-4f));

        const Trs d = decompose(m);
        EXPECT_EQ(d.translation, t);
        EXPECT_TRUE(Near(d.scale, s, 1e-5f * 10.0f));
        EXPECT_TRUE(SameRotation(d.rotation, r, 1e-5f));
        EXPECT_TRUE(Near(Mat4::trs(d.translation, d.rotation, d.scale), m, 1e-4f));
    }

    // Mirroring shows up as a negative scale.x and still reproduces the matrix.
    const Mat4 mirrored = Mat4::trs({1.0f, 2.0f, 3.0f}, Quat::fromAxisAngle(kY, 0.5f), {-2.0f, 1.0f, 3.0f});
    const Trs d = decompose(mirrored);
    EXPECT_LT(d.scale.x, 0.0f);
    EXPECT_TRUE(Near(Mat4::trs(d.translation, d.rotation, d.scale), mirrored, 1e-5f));
    // Zero scale: rotation is unrecoverable and reported as the identity.
    EXPECT_EQ(decompose(Mat4::scale({1.0f, 0.0f, 1.0f})).rotation, Quat::identity());
}

TEST(Matrix, ReversedZPerspectiveAndOrthographicMapDepthToVulkanClipSpace) {
    // Vulkan conventions: depth in [0, 1] (reversed: near -> 1, far -> 0) and clip-space Y pointing down.
    const f32 zNear = 0.1f;
    const f32 zFar = 1000.0f;
    const f32 fovY = detmath::kHalfPi;
    const Mat4 p = Mat4::perspectiveRH(fovY, 16.0f / 9.0f, zNear, zFar);
    EXPECT_TRUE(Near(clipDepth(p, -zNear), 1.0f, 1e-6f));
    EXPECT_TRUE(Near(clipDepth(p, -zFar), 0.0f, 1e-6f));
    // Monotonic: farther is smaller.
    EXPECT_GT(clipDepth(p, -1.0f), clipDepth(p, -10.0f));
    EXPECT_GT(clipDepth(p, -10.0f), clipDepth(p, -100.0f));
    // Points behind the near plane fall outside [0, 1].
    EXPECT_GT(clipDepth(p, -0.05f), 1.0f);

    // Y flip: the top edge of the frustum at the near plane lands on clip-space y = -1 (top of a Vulkan viewport).
    const f32 tanHalf = detmath::tan(fovY * 0.5f);
    const Vec4 top = p * Vec4{0.0f, zNear * tanHalf, -zNear, 1.0f};
    EXPECT_TRUE(Near(top.y / top.w, -1.0f, 1e-6f));
    const Vec4 right = p * Vec4{zNear * tanHalf * 16.0f / 9.0f, 0.0f, -zNear, 1.0f};
    EXPECT_TRUE(Near(right.x / right.w, 1.0f, 1e-5f));

    const Mat4 inf = Mat4::perspectiveInfiniteRH(fovY, 1.0f, zNear);
    EXPECT_EQ(clipDepth(inf, -zNear), 1.0f);
    EXPECT_GT(clipDepth(inf, -1e6f), 0.0f);
    EXPECT_LT(clipDepth(inf, -1e6f), 1e-6f);
    EXPECT_TRUE(Near(clipDepth(inf, -1.0f), zNear, 1e-7f));

    const Mat4 o = Mat4::orthographicRH(-4.0f, 4.0f, -3.0f, 3.0f, 1.0f, 101.0f);
    EXPECT_TRUE(Near(clipDepth(o, -1.0f), 1.0f, 1e-6f));
    EXPECT_TRUE(Near(clipDepth(o, -101.0f), 0.0f, 1e-6f));
    EXPECT_TRUE(Near(clipDepth(o, -51.0f), 0.5f, 1e-6f));
    const Vec4 corner = o * Vec4{4.0f, 3.0f, -1.0f, 1.0f};
    EXPECT_TRUE(Near(corner.x, 1.0f, 1e-6f));
    EXPECT_TRUE(Near(corner.y, -1.0f, 1e-6f));
}

TEST(Matrix, LookAtRhProducesAnOrthonormalViewBasis) {
    TestRng rng;
    for (int i = 0; i < 200; ++i) {
        SCOPED_TRACE(std::format("iteration {}", i));
        const Vec3 eye = rng.vec3(-50.0f, 50.0f);
        const Vec3 target = eye + rng.vec3(-10.0f, 10.0f) + Vec3{0.0f, 0.0f, 25.0f};
        const Mat4 view = Mat4::lookAtRH(eye, target, kY);
        const Mat3 basis = toMat3(view);
        EXPECT_TRUE(Near(transpose(basis) * basis, Mat3::identity(), 1e-5f));
        EXPECT_TRUE(Near(determinant(basis), 1.0f, 1e-5f));
        EXPECT_TRUE(Near(transformPoint(view, eye), Vec3{}, 1e-4f));
        // The target lies straight ahead on -Z, and world up stays in the upper half of the view.
        const Vec3 viewTarget = transformPoint(view, target);
        EXPECT_TRUE(Near(viewTarget, Vec3{0.0f, 0.0f, -distance(eye, target)}, 1e-3f));
        EXPECT_GT(transformVector(view, kY).y, 0.0f);
        EXPECT_TRUE(Near(affineInverse(view) * view, Mat4::identity(), 1e-5f));
    }
    const Mat4 view = Mat4::lookAtRH({0.0f, 0.0f, 5.0f}, {}, kY);
    EXPECT_TRUE(Near(view, Mat4::translation({0.0f, 0.0f, -5.0f}), 0.0f));
}

TEST(MatrixRegression, InverseTreatsNumericallySingularMatricesAsSingular) {
    // A denormal determinant used to pass the det == 0 guard; 1/det overflowed to inf and every zero cofactor became
    // 0*inf = NaN, with a different NaN sign bit on x64 and ARM64.
    const Mat3 zero3{{{}, {}, {}}};
    const Mat4 zero4{{{}, {}, {}, {}}};
    for (const f32 s : {1e-13f, 1e-14f, 1e-15f, 1e-20f}) {
        const Mat4 tiny = Mat4::scale({s, s, s});
        EXPECT_EQ(inverse(toMat3(tiny)), zero3) << std::format("scale {}", s);
        EXPECT_EQ(inverse(tiny), zero4) << std::format("scale {}", s);
        const Mat4 affine = Mat4::trs({1.0f, -2.0f, 3.0f}, Quat::fromAxisAngle(kY, 0.5f), {s, s, s});
        // Zero linear part and translation; the affine row stays (0, 0, 0, 1).
        EXPECT_EQ(affineInverse(affine), Mat4::fromMat3(zero3, Vec3{})) << std::format("scale {}", s);
    }
    // A NaN determinant is rejected too, instead of spreading NaNs.
    const f32 nan = std::bit_cast<f32>(0x7FC0'0000u);
    EXPECT_EQ(inverse(Mat4::scale({nan, 1.0f, 1.0f})), zero4);
    EXPECT_EQ(inverse(Mat3{{{nan, 0.0f, 0.0f}, kY, kZ}}), zero3);

    // Small but representable scales still invert: det 1e-36 is a normal f32 and 1/det is finite.
    const Mat4 small = Mat4::scale({1e-12f, 1e-12f, 1e-12f});
    const Mat4 smallInv = inverse(small);
    EXPECT_TRUE(Near(smallInv.columns[0].x * 1e-12f, 1.0f, 1e-6f));
    EXPECT_EQ(smallInv.columns[0].y, 0.0f);
    EXPECT_EQ(smallInv.columns[3].w, 1.0f);
    EXPECT_TRUE(Near(inverse(toMat3(small)).columns[2].z * 1e-12f, 1.0f, 1e-6f));
    EXPECT_TRUE(Near(affineInverse(small).columns[1].y * 1e-12f, 1.0f, 1e-6f));
}

TEST(VectorRegression, NormalizeKeepsTheDirectionOfTinyAndHugeFiniteVectors) {
    // dot(v, v) underflowing to 0 (or a denormal) or overflowing to inf used to give the zero vector or lose precision.
    const Vec3 tiny = normalize(Vec3{1e-23f, 0.0f, 0.0f});
    EXPECT_EQ(tiny, kX);
    const Vec3 huge = normalize(Vec3{2e19f, 0.0f, 0.0f});
    EXPECT_EQ(huge, kX);
    const Vec3 denormal = normalize(Vec3{3e-21f, 4e-21f, 0.0f});
    EXPECT_TRUE(Near(denormal, Vec3{0.6f, 0.8f, 0.0f}, 1e-6f));
    EXPECT_TRUE(Near(length(denormal), 1.0f, 1e-6f));
    EXPECT_TRUE(Near(normalize(Vec3{-3e30f, 0.0f, 4e30f}), Vec3{-0.6f, 0.0f, 0.8f}, 1e-6f));
    EXPECT_TRUE(Near(normalize(Vec2{1e-40f, -1e-40f}), Vec2{0.70710678f, -0.70710678f}, 1e-6f));
    EXPECT_TRUE(Near(length(normalize(Vec4{1e-30f, 2e-30f, -3e-30f, 1e-31f})), 1.0f, 1e-6f));
    EXPECT_EQ(normalize(Vec3{-0.0f, 0.0f, -0.0f}), Vec3{});

    // Quaternions get the same treatment in normalize and inverse.
    EXPECT_EQ(normalize(Quat{1e-23f, 0.0f, 0.0f, 0.0f}), (Quat{1.0f, 0.0f, 0.0f, 0.0f}));
    EXPECT_EQ(normalize(Quat{0.0f, 0.0f, 3e20f, 0.0f}), (Quat{0.0f, 0.0f, 1.0f, 0.0f}));
    EXPECT_EQ(normalize(Quat{0.0f, 0.0f, 0.0f, 0.0f}), Quat::identity());
    const Quat tinyInv = inverse(Quat{0.0f, 1e-23f, 0.0f, 0.0f});
    EXPECT_TRUE(Near(tinyInv.y * -1e-23f, 1.0f, 1e-6f));
    EXPECT_EQ(tinyInv.x, 0.0f);
    EXPECT_EQ(tinyInv.z, 0.0f);
    EXPECT_EQ(tinyInv.w, 0.0f);
    const Quat hugeInv = inverse(Quat{0.0f, 0.0f, 0.0f, 4e19f});
    EXPECT_TRUE(Near(hugeInv.w * 4e19f, 1.0f, 1e-6f));
    EXPECT_EQ(inverse(Quat{0.0f, 0.0f, 0.0f, 0.0f}), Quat::identity());
}

TEST(QuatRegression, SlerpIsExactAtTZeroOffTheNlerpPathAndNlerpOnlyUpToRenormalization) {
    TestRng rng;
    for (int i = 0; i < 200; ++i) {
        SCOPED_TRACE(std::format("iteration {}", i));
        const Quat a = rng.rotation();
        const Quat b = rng.rotation();
        if (detail::absF32(dot(a, b)) < 0.999f) {
            EXPECT_EQ(slerp(a, b, 0.0f), a);
        }
        // nlerp renormalizes, so t == 0 is only within a few ulp of a (documented).
        EXPECT_TRUE(Near(nlerp(a, b, 0.0f), a, 1e-6f));
    }
}

TEST(MathGolden, DeterministicBatchOfOperationsMatchesRecordedBitPatterns) {
    // Recorded on macOS ARM64 (AppleClang). Must match on every platform and build configuration: if it changes,
    // an operation's evaluation order (or detmath) changed, which breaks sim determinism and replays.
    constexpr u64 kGolden = 0xcdff'd048'06b2'1b0cull;

    Fnv1a64 hash;
    const auto put = [&hash](f32 v) { hash.updateValue(std::bit_cast<u32>(v)); };
    const auto put3 = [&put](const Vec3& v) {
        put(v.x);
        put(v.y);
        put(v.z);
    };
    const auto put4 = [&put](const Vec4& v) {
        put(v.x);
        put(v.y);
        put(v.z);
        put(v.w);
    };
    const auto putQ = [&put](const Quat& q) {
        put(q.x);
        put(q.y);
        put(q.z);
        put(q.w);
    };
    const auto putM = [&put4](const Mat4& m) {
        for (const Vec4& c : m.columns) {
            put4(c);
        }
    };

    TestRng rng;
    for (int i = 0; i < 256; ++i) {
        const Vec3 a = rng.vec3(-100.0f, 100.0f);
        const Vec3 b = rng.vec3(-100.0f, 100.0f);
        const f32 t = rng.uniform(0.0f, 1.0f);
        put3(a + b);
        put3(a - b);
        put3(a * b);
        put3(a / (b + Vec3{200.0f, 200.0f, 200.0f}));
        put(dot(a, b));
        put3(cross(a, b));
        put(length(a));
        put(distance(a, b));
        put3(normalize(a));
        put3(lerp(a, b, t));
        put3(clamp(a, -abs(b), abs(b)));

        const Quat q = rng.rotation();
        const Quat r = Quat::fromEulerXYZ(rng.vec3(-3.0f, 3.0f));
        putQ(q * r);
        putQ(inverse(q));
        putQ(normalize(Quat{a.x, a.y, a.z, b.x}));
        put3(rotate(q, a));
        putQ(slerp(q, r, t));
        putQ(nlerp(q, r, t));

        const Mat4 m = Mat4::trs(a, q, rng.vec3(0.5f, 2.0f));
        const Mat4 n = Mat4::trs(b, r, rng.vec3(0.5f, 2.0f));
        putM(m * n);
        putM(inverse(m * n));
        putM(affineInverse(m));
        put(determinant(m * n));
        const Trs d = decompose(m * n);
        put3(d.translation);
        putQ(d.rotation);
        put3(d.scale);
        putM(Mat4::lookAtRH(a, b, kY));
        putM(Mat4::perspectiveRH(0.5f + t, 1.0f + t, 0.01f + t, 100.0f + 1000.0f * t));
        putM(Mat4::perspectiveInfiniteRH(0.5f + t, 1.5f, 0.1f));
        putM(Mat4::orthographicRH(-a.x, b.x + 300.0f, -10.0f, 10.0f, 0.5f, 50.0f + t));
    }

    const u64 digest = hash.digest();
    // Printed on every run so a deliberate change can be re-recorded without editing the test first.
    std::printf("vector golden hash: 0x%016llx\n", static_cast<unsigned long long>(digest));
    EXPECT_EQ(digest, kGolden) << std::hex << std::showbase << "got " << digest << ", expected " << kGolden;
}
