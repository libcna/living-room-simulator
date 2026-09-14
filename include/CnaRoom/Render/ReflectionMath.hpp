// SPDX-License-Identifier: MIT
#pragma once

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Plane.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <array>

/// The matrix side of planar reflections, kept free of the device so it can
/// be unit-tested: the world reflected in a plane seen by the unmoved camera,
/// a projection tightened to a quad, and the oblique near plane.
namespace CnaRoom::ReflectionMath {

/// View of the world reflected in the plane (row-vector convention: v * Reflect * View).
[[nodiscard]] Microsoft::Xna::Framework::Matrix reflectedView(const Microsoft::Xna::Framework::Matrix& view,
                                                              const Microsoft::Xna::Framework::Plane& plane);

/// Off-centre perspective through the corners' bounds in the base projection's
/// NDC (with a margin). False when a corner is behind the camera or the
/// bounds are off screen; `projection` is then left as the base.
[[nodiscard]] bool tightenProjection(const Microsoft::Xna::Framework::Matrix& view, const Microsoft::Xna::Framework::Matrix& base,
                                     const std::array<Microsoft::Xna::Framework::Vector3, 4>& corners, float nearPlane, float farPlane,
                                     float margin, Microsoft::Xna::Framework::Matrix& projection);

/// Remaps clip-space z so the world plane (normal facing the visible side)
/// lands on `nearNdc` and the far corner opposite it keeps depth 1 (Lengyel's
/// oblique frustum). Points on the other side of the plane fall below
/// nearNdc and are clipped.
[[nodiscard]] Microsoft::Xna::Framework::Matrix obliqueProjection(const Microsoft::Xna::Framework::Matrix& projection,
                                                                  const Microsoft::Xna::Framework::Matrix& view,
                                                                  const Microsoft::Xna::Framework::Plane& worldPlane, float nearNdc);

/// NDC depth (z / w) of a world point through view * projection.
[[nodiscard]] float ndcDepth(const Microsoft::Xna::Framework::Vector3& world, const Microsoft::Xna::Framework::Matrix& view,
                             const Microsoft::Xna::Framework::Matrix& projection);

}  // namespace CnaRoom::ReflectionMath
