// SPDX-License-Identifier: MIT
#include "CnaRoom/Render/ReflectionMath.hpp"

#include "Microsoft/Xna/Framework/Vector4.hpp"

#include <algorithm>
#include <cmath>

using namespace Microsoft::Xna::Framework;

namespace CnaRoom::ReflectionMath {

Matrix reflectedView(const Matrix& view, const Plane& plane)
{
    return Matrix::CreateReflection(Plane::Normalize(plane)) * view;
}

bool tightenProjection(const Matrix& view, const Matrix& base, const std::array<Vector3, 4>& corners, float nearPlane, float farPlane,
                       float margin, Matrix& projection)
{
    projection = base;
    float x0 = 1.0f, x1 = -1.0f, y0 = 1.0f, y1 = -1.0f;
    const Matrix viewBase = view * base;
    for (const Vector3& corner : corners)
    {
        const Vector4 c = Vector4::Transform(Vector4(corner.X, corner.Y, corner.Z, 1.0f), viewBase);
        if (c.W <= 0.01f) return false;
        x0 = std::min(x0, c.X / c.W); x1 = std::max(x1, c.X / c.W);
        y0 = std::min(y0, c.Y / c.W); y1 = std::max(y1, c.Y / c.W);
    }
    x0 = std::clamp(x0 - margin, -1.0f, 1.0f); x1 = std::clamp(x1 + margin, -1.0f, 1.0f);
    y0 = std::clamp(y0 - margin, -1.0f, 1.0f); y1 = std::clamp(y1 + margin, -1.0f, 1.0f);
    if (x1 - x0 < 0.02f || y1 - y0 < 0.02f) return false;
    // Half extents of the base near plane: M11 = near / halfWidth, M22 = near / halfHeight.
    const float hw = nearPlane / base.M11, hh = nearPlane / base.M22;
    projection = Matrix::CreatePerspectiveOffCenter(x0 * hw, x1 * hw, y0 * hh, y1 * hh, nearPlane, farPlane);
    return true;
}

Matrix obliqueProjection(const Matrix& projection, const Matrix& view, const Plane& worldPlane, float nearNdc)
{
    const Plane p = Plane::Normalize(worldPlane);
    // The plane in view space, its normal pointing towards the visible side.
    const Vector3 pointOnPlane = p.Normal * (-p.D);
    const Vector3 pointView = Vector3::Transform(pointOnPlane, view);
    const Vector3 normalView = Vector3::TransformNormal(p.Normal, view);
    Plane clip(normalView, -Vector3::Dot(normalView, pointView));
    clip.Normalize();
    // q: the far-plane corner farthest on the plane's visible side, in view
    // space (homogeneous; its clip w is 1 by construction).
    const float sx = clip.Normal.X >= 0.0f ? 1.0f : -1.0f;
    const float sy = clip.Normal.Y >= 0.0f ? 1.0f : -1.0f;
    const Vector4 q = Vector4::Transform(Vector4(sx, sy, 1.0f, 1.0f), Matrix::Invert(projection));
    const float cq = clip.Normal.X * q.X + clip.Normal.Y * q.Y + clip.Normal.Z * q.Z + clip.D * q.W;
    const float wq = q.X * projection.M14 + q.Y * projection.M24 + q.Z * projection.M34 + q.W * projection.M44;
    Matrix result = projection;
    if (std::abs(cq) < 1e-6f) return result;
    // Third column: z_clip = alpha * (C . v) + nearNdc * w_clip; the plane
    // maps to nearNdc, q to 1.
    const float alpha = (1.0f - nearNdc) * wq / cq;
    result.M13 = alpha * clip.Normal.X + nearNdc * projection.M14;
    result.M23 = alpha * clip.Normal.Y + nearNdc * projection.M24;
    result.M33 = alpha * clip.Normal.Z + nearNdc * projection.M34;
    result.M43 = alpha * clip.D + nearNdc * projection.M44;
    return result;
}

float ndcDepth(const Vector3& world, const Matrix& view, const Matrix& projection)
{
    const Vector3 v = Vector3::Transform(world, view);
    const Vector4 c = Vector4::Transform(Vector4(v.X, v.Y, v.Z, 1.0f), projection);
    return c.W != 0.0f ? c.Z / c.W : 0.0f;
}

}  // namespace CnaRoom::ReflectionMath
