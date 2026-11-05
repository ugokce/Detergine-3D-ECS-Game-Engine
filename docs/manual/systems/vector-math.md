---
title: Vectors, quaternions and matrices
section: Systems
order: 60
status: implemented
summary: The deterministic f32 Vec2/Vec3/Vec4, Quat, Mat3 and Mat4 types shared by the sim and the renderer, with column-major Vulkan conventions and reversed-Z projections.
sources: engine/math/include/dg/math/Vector.h, engine/math/include/dg/math/Quat.h, engine/math/include/dg/math/Matrix.h, engine/math/src/Vector.cpp, engine/math/CMakeLists.txt, tests/math/VectorTest.cpp
---

## Overview

The `math` module provides the `f32` vector types used everywhere in the engine: by sim code (positions, velocities,
rotations) and by the renderer (transforms, cameras, projections). One set of types serves both, so sim state never
has to be converted before it is drawn.

| Type | Header | Layout | Default value |
|---|---|---|---|
| `dg::Vec2`, `dg::Vec3`, `dg::Vec4` | [Vector.h](engine/math/include/dg/math/Vector.h) | 2, 3 or 4 `f32` (8, 12, 16 bytes) | All zeros |
| `dg::Quat` | [Quat.h](engine/math/include/dg/math/Quat.h) | `x, y, z, w` (16 bytes) | Identity rotation |
| `dg::Mat3` | [Matrix.h](engine/math/include/dg/math/Matrix.h) | 3 column `Vec3`s (36 bytes) | Identity |
| `dg::Mat4` | [Matrix.h](engine/math/include/dg/math/Matrix.h) | 4 column `Vec4`s (64 bytes) | Identity |
| `dg::Trs` | [Matrix.h](engine/math/include/dg/math/Matrix.h) | translation, rotation, scale | Zero translation, identity rotation, unit scale |

All types are plain standard-layout aggregates with default member initializers, so they are safe as fields of
zero-initialized sim components (determinism rule 8).

Key guarantees:

* **Deterministic.** Every operation is a fixed sequence of IEEE-754 `+ - * /` and `sqrt` with an explicit
  evaluation order, plus [detmath](detmath.md) for transcendentals. No `<cmath>`. Results are bit-identical on x64
  and ARM64; a golden hash enforces it.
* **Vulkan conventions built in.** Matrices are column-major and match the GPU uniform layout. Projections produce
  Vulkan clip space with Y down and reversed-Z depth.
* **No NaNs from degenerate finite input.** Normalizing a zero vector, inverting a singular matrix, and similar cases
  return defined values instead of NaN or infinity (see [Degenerate input](#degenerate-input)).

### Where it sits in the build

[engine/math/CMakeLists.txt](engine/math/CMakeLists.txt) declares the module:

```cmake
dg_add_module(math SIM
    SOURCES
        src/DetMath.cpp
        src/Fixed.cpp
        src/Vector.cpp
    PUBLIC_DEPS
        dg::core)
```

This creates the static library `dg_math` with the alias `dg::math`, public headers in `engine/math/include`, and
marks it as a sim target (`SIM`), so it carries the strict FP flags. The module also holds
[fixed-point numbers](fixed-point.md) and [detmath](detmath.md). Link it with
`target_link_libraries(<target> PRIVATE dg::math)`.

## How to use

```cpp
#include "dg/math/Matrix.h" // also brings in Vector.h and Quat.h

// Moves a unit along its facing direction and builds its model matrix.
struct Mover {
    dg::Vec3 position;
    dg::Quat rotation; // identity by default
    dg::f32 speed = 0.0f;
};

void step(Mover& m, dg::f32 dt) {
    const dg::Vec3 forward = dg::rotate(m.rotation, dg::Vec3{0.0f, 0.0f, -1.0f}); // -Z is forward
    m.position += forward * (m.speed * dt);
}

dg::Mat4 modelMatrix(const Mover& m) { return dg::Mat4::trs(m.position, m.rotation, {1.0f, 1.0f, 1.0f}); }
```

### Vectors

Vectors support component-wise `+ - * /`, scalar `*` and `/`, unary minus, the compound forms, and `==` (exact
IEEE value comparison per component, not bitwise). Division is always a true division per component, never a multiply by a reciprocal, so
`v / s` matches the scalar maths.

```cpp
#include "dg/math/Vector.h"

dg::Vec3 steer(const dg::Vec3& position, const dg::Vec3& target, dg::f32 maxStep) {
    const dg::Vec3 toTarget = target - position;
    const dg::f32 distanceSq = dg::lengthSquared(toTarget);
    if (distanceSq <= maxStep * maxStep) {
        return target;
    }
    return position + dg::normalize(toTarget) * maxStep;
}
```

`dot(a, b)` is always `a.x*b.x + a.y*b.y + a.z*b.z`, summed left to right. `cross(Vec3)` is right-handed:
`cross({1,0,0}, {0,1,0}) == {0,0,1}`. `cross(Vec2)` returns the z component of the 3D cross product, positive when
`b` is counter-clockwise from `a`. `lerp(a, b, t)` is `a*(1-t) + b*t`, so it is exact at both ends.

### Quaternions

Quaternions use the Hamilton product and active, right-handed rotations. `a * b` rotates by `b` first, then by `a`.
Angles are radians; a positive angle rotates counter-clockwise when you look down the axis towards the origin.

```cpp
#include "dg/math/Matrix.h"

dg::Quat turnTowards(const dg::Quat& current, const dg::Quat& goal, dg::f32 t) {
    return dg::slerp(current, goal, t); // shortest path; falls back to nlerp when nearly parallel
}

dg::Quat yawPitch(dg::f32 yaw, dg::f32 pitch) {
    const dg::Quat qYaw = dg::Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, yaw);   // axis must be unit length
    const dg::Quat qPitch = dg::Quat::fromAxisAngle({1.0f, 0.0f, 0.0f}, pitch);
    return qYaw * qPitch; // pitch first, then yaw
}
```

`Quat::fromEulerXYZ(radians)` applies extrinsic rotations about the fixed X, then Y, then Z axes:
`q = qZ * qY * qX` (equivalently intrinsic Z, Y′, X″).

### Matrices

Matrices are **column-major**: `columns[c]` is column `c`, and the element in row `r`, column `c` is
`columns[c].{x,y,z,w}[r]`. Vectors are column vectors, and transforms compose right to left: `(A * B) * v` applies
`B` first. A `Mat4` can be copied into a GPU buffer as is; it matches the Vulkan/GLSL/Slang `column_major` layout.

```cpp
#include "dg/math/Matrix.h"

#include "dg/math/DetMath.h"

dg::Mat4 viewProjection(const dg::Vec3& eye, const dg::Vec3& target, dg::f32 aspect) {
    const dg::Mat4 view = dg::Mat4::lookAtRH(eye, target, {0.0f, 1.0f, 0.0f});
    const dg::Mat4 projection = dg::Mat4::perspectiveRH(dg::detmath::kHalfPi, aspect, 0.1f, 1000.0f);
    return projection * view; // view first, then projection
}

dg::Vec3 toLocal(const dg::Mat4& model, const dg::Vec3& worldPoint) {
    return dg::transformPoint(dg::affineInverse(model), worldPoint);
}
```

### Coordinate and projection conventions

| Convention | Value |
|---|---|
| Handedness | Right-handed world and view space |
| Camera | Looks down -Z, +Y is up |
| Clip space | Vulkan: depth range [0, 1], **Y points down** |
| Y flip | Done by the projection matrices (view +Y maps to clip -Y), so use a normal, positive-height viewport |
| Front face | Counter-clockwise triangles stay counter-clockwise on screen: `VK_FRONT_FACE_COUNTER_CLOCKWISE` |
| Depth | **Reversed-Z** for every projection, including orthographic: near plane → 1, far plane → 0 |
| Depth setup | Clear depth to 0 and compare with `VK_COMPARE_OP_GREATER_OR_EQUAL`, for every camera |

**Reversed-Z** stores depth so that the near plane maps to 1 and the far plane to 0. Floating-point depth has most of
its precision near 0, which reversed-Z places at far distances, where perspective needs it most.
`perspectiveInfiniteRH` puts the far plane at infinity: depth is `zNear / viewDistance`, approaching 0 but never
reaching it.

### Degenerate input

| Call | Degenerate input | Result |
|---|---|---|
| `normalize(Vec)` | Zero vector | Zero vector (check `lengthSquared` first if "no direction" needs special handling) |
| `normalize(Vec)` | Tiny or huge finite vector whose squared length underflows or overflows | Correct unit vector (rescaled path) |
| `normalize(Vec)` | Inf or NaN components | Non-finite result |
| `normalize(Quat)`, `inverse(Quat)` | Zero quaternion | Identity |
| `normalize(Quat)`, `inverse(Quat)` | Tiny or huge finite quaternion | Correct result (rescaled path) |
| `inverse(Mat3)`, `inverse(Mat4)` | Singular or numerically singular (1/det not finite or zero, NaN det) | All-zero matrix |
| `affineInverse(Mat4)` | Singular linear part | Zero linear part and a negative-zero translation (bits `0x80000000`, equal to zero under `==`); last row stays (0, 0, 0, 1) |
| `decompose(Mat4)` | Any scale component is 0 | Rotation is the identity |
| `decompose(Mat4)` | Negative determinant (mirroring) | Reported as a negative `scale.x` |
| `decompose(Mat4)` | Shear | Not representable; folded into the rotation estimate |
| `slerp(a, b, t)` | `dot(a, b) > 0.9995` after the sign flip (rotations less than about 3.6 degrees apart) | Uses `nlerp` |
| `min`, `max` | NaN component | The first argument's component, so `clamp` returns `lo` for a NaN component (the comment in `Vector.h` saying "the second argument" is wrong) |
| `Mat4::lookAtRH` | `up` parallel to the view direction | Not supported: the right vector normalizes to zero and the matrix is singular |

The singular-matrix guard tests the reciprocal of the determinant, not `det == 0`. A denormal determinant would pass
an equality test, make `1/det` infinite, and turn every zero cofactor into `0 * inf = NaN`, whose sign bit differs
between x64 (`0xFFC00000`) and ARM64 (`0x7FC00000`). Cofactor overflow (entries beyond about 1e19 for `Mat3` and
1e12 for `Mat4`) is outside the contract.

### Determinism notes

* The inline (`constexpr`) operations are compiled in the caller's translation unit. That is safe only because every
  engine target, sim or not, is built with `-ffp-contract=off -fno-fast-math` (or `/fp:precise`, or
  `/clang:-ffp-contract=off` for clang-cl) through `dg_configure_target`. Code that uses these headers from a target
  created outside `dg_add_module`, `dg_add_app` or `dg_configure_target` loses that guarantee.
* The heavier operations (`slerp`, `fromMat3`, `inverse`, `decompose`, projections) are out of line in
  [Vector.cpp](engine/math/src/Vector.cpp), so they are compiled once with the engine's flags.
* `nlerp` renormalizes, so at `t == 0` it is within a few ulp of `a`, not bit-exact. `slerp` is exact at `t == 0` off
  the `nlerp` path. At `t == 1`, `slerp` may return `-b`, which is the same rotation.
* `nearlyEqual(Quat)` compares components, so `q` and `-q` are not nearly equal even though they represent the same
  rotation. Compare `rotate` results, or `dot(a, b)` against 1, for sign-agnostic equality.
* `==` compares values exactly, as IEEE comparison does: `-0.0f == 0.0f`, and a NaN component is never equal.

## How to edit

| File | Contents |
|---|---|
| [Vector.h](engine/math/include/dg/math/Vector.h) | `Vec2`/`Vec3`/`Vec4`, their operators, and the generic functions over the `FloatVector` concept |
| [Quat.h](engine/math/include/dg/math/Quat.h) | `Quat`, the Hamilton product, `rotate`, `inverse`, `normalize`; declarations of the out-of-line functions |
| [Matrix.h](engine/math/include/dg/math/Matrix.h) | `Mat3`, `Mat4`, `Trs`, products, transpose, transforms, and the projection declarations |
| [Vector.cpp](engine/math/src/Vector.cpp) | Out-of-line quaternion and matrix code |
| [CMakeLists.txt](engine/math/CMakeLists.txt) | The `math` module declaration |

Invariants every change must keep:

* **Only IEEE `+ - * /`, `sqrt` and `dg::detmath`.** No `<cmath>`, no `std::fma`, no SIMD intrinsics whose results
  could differ between SSE and NEON.
* **Explicit evaluation order.** Write sums left to right and keep each operation visible. Do not "simplify" an
  expression algebraically: `a*(1-t) + b*t` and `a + (b-a)*t` round differently.
* **True division.** Do not replace `/ s` with `* (1 / s)`.
* **Defined results for degenerate finite input.** New functions must not return NaN or infinity for finite input
  where a defined fallback exists. Document the fallback in the doc comment and in the table on this page.
* **Stable conventions.** Column-major layout, right-handed space, Vulkan clip space, reversed-Z. Changing any of them
  changes every renderer and sim consumer.
* **Results are part of the sim contract.** Any change to an existing function's bits changes the golden hash in
  `MathGolden` and the sim state hash, and invalidates replays. Make such changes deliberately, update the golden
  value, and say so in the commit message.

To add an operation: put small, branch-free ones in the header as `constexpr`; put heavier ones in `Vector.cpp`.
Then add unit tests to [VectorTest.cpp](tests/math/VectorTest.cpp), add the operation to the golden batch if sim code
will use it, and update this page.

## Reference

### Vector functions (`Vec2`, `Vec3`, `Vec4` unless noted)

| Function | Description |
|---|---|
| `+ - * /` (vector and scalar), unary `-`, `+= -= *= /=` | Component-wise |
| `dot(a, b)` | Left-to-right sum of products |
| `cross(Vec2, Vec2)` | Scalar z of the 3D cross product |
| `cross(Vec3, Vec3)` | Right-handed cross product |
| `min(a, b)`, `max(a, b)`, `abs(v)` | Component-wise; `abs` clears the sign bit (`abs(-0) = +0`) |
| `lengthSquared(v)`, `length(v)`, `distance(a, b)` | `length` uses `detmath::sqrt` |
| `maxAbsComponent(v)` | Largest absolute component |
| `normalize(v)` | Unit-length copy; see [Degenerate input](#degenerate-input) |
| `lerp(a, b, t)` | `a*(1-t) + b*t`, exact at t = 0 and 1 |
| `clamp(v, lo, hi)` | `max(lo, min(v, hi))`, expects `lo <= hi` |
| `nearlyEqual(a, b, epsilon)` | Every component within an absolute tolerance; also a scalar `f32` overload |
| `vec4(Vec3, w)`, `Vec4::xyz()` | Extend with w (1 for points, 0 for directions), or drop it |

### Quaternion functions

| Function | Description |
|---|---|
| `Quat::identity()` | (0, 0, 0, 1) |
| `Quat::fromAxisAngle(axis, angle)` | Axis must be unit length |
| `Quat::fromEulerXYZ(radians)` | `qZ * qY * qX` |
| `Quat::fromMat3(m)` | Rotation of an orthonormal, right-handed matrix (Shepperd's method, normalized); needs `Matrix.h` |
| `q.xyz()` | Vector part |
| `a * b` | Hamilton product: `b` first, then `a` |
| `-q`, `conjugate(q)`, `dot(a, b)` | Usual meaning |
| `inverse(q)` | Conjugate divided by squared length; identity for zero |
| `normalize(q)` | Unit-length copy; identity for zero |
| `rotate(q, v)` | Rotates `v` by unit `q` (18 multiplies, 12 adds or subtracts; the `Quat.h` comment says 15 and 15, which is wrong) |
| `nlerp(a, b, t)`, `slerp(a, b, t)` | Shortest-path interpolation; inputs to `slerp` must be unit length |
| `toMat3(q)`, `toMat4(q)` | Rotation matrices; need `Matrix.h` |
| `nearlyEqual(a, b, epsilon)` | Component-wise; not sign-agnostic |

### Matrix functions

| Function | Description |
|---|---|
| `Mat3::identity()`, `Mat3::fromColumns(c0, c1, c2)` | Construction |
| `Mat4::identity()`, `Mat4::fromColumns(c0, c1, c2, c3)` | Construction |
| `Mat4::fromMat3(m, t = {})` | Linear part plus translation column |
| `Mat4::translation(t)`, `Mat4::scale(s)`, `Mat4::rotation(q)` | Basic transforms |
| `Mat4::trs(t, r, s)` | `translation(t) * rotation(r) * scale(s)`: scale, then rotate, then translate |
| `Mat4::lookAtRH(eye, target, up)` | View matrix: eye to the origin, view direction to -Z |
| `Mat4::perspectiveRH(fovY, aspect, zNear, zFar)` | Full vertical FOV in radians, aspect = width / height, requires 0 < zNear < zFar |
| `Mat4::perspectiveInfiniteRH(fovY, aspect, zNear)` | Far plane at infinity |
| `Mat4::orthographicRH(left, right, bottom, top, zNear, zFar)` | Reversed-Z, Y flipped |
| `m * v`, `a * b` | Column combination summed left to right |
| `transpose(m)`, `determinant(m)`, `inverse(m)` | `Mat3` and `Mat4` |
| `toMat3(Mat4)` | Upper-left 3×3 block |
| `transformPoint(m, p)`, `transformVector(m, v)` | w = 1 (no perspective divide) and w = 0 |
| `affineInverse(m)` | Assumes the last row is (0, 0, 0, 1); cheaper and usually more accurate than `inverse` |
| `decompose(m)` | Returns a `Trs` |
| `nearlyEqual(a, b, epsilon)` | Every column within an absolute tolerance |

## Testing

[VectorTest.cpp](tests/math/VectorTest.cpp) builds the `dg-vector-tests` executable, labelled `unit` and
`determinism`. It covers:

* Arithmetic, algebraic identities on random inputs, and the handedness of `cross`.
* Quaternion axis-angle construction, composition, `rotate` against the rotation matrix, Euler order, and `slerp`
  endpoints, shortest path and the `nlerp` fallback.
* Matrix products, transpose, determinant, inverses of random well-conditioned matrices, the `trs`/`decompose`
  round trip, reversed-Z depth and the Y flip of every projection, and the orthonormality of `lookAtRH`.
* Regression tests (`MatrixRegression`, `VectorRegression`, `QuatRegression`) for numerically singular matrices,
  tiny and huge vectors and quaternions, and `slerp`/`nlerp` at `t == 0`.
* `MathGolden.DeterministicBatchOfOperationsMatchesRecordedBitPatterns`: a fixed batch of vector, quaternion and
  matrix operations whose result bits are hashed with FNV-1a. The golden value `0xcdffd04806b21b0c` was recorded on
  macOS ARM64 (AppleClang) and must match on every platform and configuration. CI runs the test on Windows x64 and
  Linux x64 as well; the Android ARM64 job only builds, so the value has not yet been confirmed on Android. The test
  prints the hash on every run; use `ctest -V` or run `dg-vector-tests` directly to see it, since ctest hides the
  output of passing tests. If it changes, an operation's
  evaluation order or detmath changed, which breaks sim determinism and replays.

```bash
./dg test -L determinism
ctest --test-dir build/<preset> -C Development -R dg-vector-tests --output-on-failure
```

See [Testing](../tooling/testing.md) for labels and the golden-hash policy.
