// SPDX-License-Identifier: MIT
// Planar reflection matrices without a device: the reflected view, the
// oblique near plane laid on the glass, and the projection tightened to a quad.
#include "CnaRoom/Render/ReflectionMath.hpp"

#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Plane.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace CnaRoom::ReflectionMath;

namespace {

int failures = 0;

void check(bool condition, const std::string& what)
{
    if (!condition)
    {
        ++failures;
        std::printf("FAIL: %s\n", what.c_str());
    }
}

}  // namespace

int main()
{
    // A camera in a room looking +Z at a mirror wall at z = 2 (normal -Z into the room).
    const Vector3 eye(0.3f, 1.5f, -1.0f);
    const Matrix view = Matrix::CreateLookAt(eye, eye + Vector3(0.0f, 0.0f, 1.0f), Vector3::Up);
    const float nearPlane = 0.05f, farPlane = 300.0f;
    const Matrix projection = Matrix::CreatePerspectiveFieldOfView(MathHelper::ToRadians(60.0f), 16.0f / 9.0f, nearPlane, farPlane);
    const Plane wall(Vector3(0.0f, 0.0f, -1.0f), 2.0f);   // -z + 2 = 0

    // The reflected view sees a room point where its mirror image is.
    const Matrix mirrored = reflectedView(view, wall);
    const Vector3 roomPoint(0.5f, 1.0f, 0.0f);
    const Vector3 imagePoint(0.5f, 1.0f, 4.0f);   // reflected in z = 2
    const Vector3 a = Vector3::Transform(roomPoint, mirrored);
    const Vector3 b = Vector3::Transform(imagePoint, view);
    check(Vector3::Distance(a, b) < 1e-4f, "reflected view places the room point at its mirror image");

    // Oblique near plane: the glass lands on -1, the room side inside the
    // range, the wall side below it (clipped).
    const Plane visibleSide(Vector3(0.0f, 0.0f, 1.0f), -2.0f);   // normal away from the camera: the mirror image lies beyond
    const Matrix oblique = obliqueProjection(projection, mirrored, visibleSide, -1.0f);
    const Vector3 onGlass(0.2f, 1.2f, 2.0f);
    const float glassDepth = ndcDepth(onGlass, mirrored, oblique);
    check(std::fabs(glassDepth + 1.0f) < 1e-3f, "glass maps to NDC -1, got " + std::to_string(glassDepth));
    const float roomDepth = ndcDepth(roomPoint, mirrored, oblique);
    check(roomDepth > -1.0f && roomDepth < 1.0f, "room side inside the depth range, got " + std::to_string(roomDepth));
    const float wallDepth = ndcDepth(Vector3(0.2f, 1.2f, 2.3f), mirrored, oblique);
    check(wallDepth < -1.0f, "behind the glass is clipped, got " + std::to_string(wallDepth));
    // Far end: a point far into the room keeps a depth below 1 (monotonic).
    const float farDepth = ndcDepth(Vector3(0.0f, 1.0f, -40.0f), mirrored, oblique);
    check(farDepth > roomDepth && farDepth <= 1.0f, "depth grows with distance and stays within 1, got " + std::to_string(farDepth));
    // x and y are untouched by the remap.
    const Vector3 v = Vector3::Transform(roomPoint, mirrored);
    const Vector4 c0 = Vector4::Transform(Vector4(v.X, v.Y, v.Z, 1.0f), projection);
    const Vector4 c1 = Vector4::Transform(Vector4(v.X, v.Y, v.Z, 1.0f), oblique);
    check(std::fabs(c0.X / c0.W - c1.X / c1.W) < 1e-5f && std::fabs(c0.Y / c0.W - c1.Y / c1.W) < 1e-5f, "oblique remap leaves x and y alone");

    // Tightened projection: the quad's corners end up just inside the frame.
    const std::array<Vector3, 4> corners = {Vector3(-0.4f, 0.7f, 2.0f), Vector3(0.4f, 0.7f, 2.0f), Vector3(0.4f, 2.0f, 2.0f),
                                            Vector3(-0.4f, 2.0f, 2.0f)};
    Matrix tight;
    check(tightenProjection(mirrored, projection, corners, nearPlane, farPlane, 0.03f, tight), "quad in front of the camera tightens");
    float minX = 1.0f, maxX = -1.0f, minY = 1.0f, maxY = -1.0f;
    for (const Vector3& corner : corners)
    {
        const Vector3 cv = Vector3::Transform(corner, mirrored);
        const Vector4 c = Vector4::Transform(Vector4(cv.X, cv.Y, cv.Z, 1.0f), tight);
        minX = std::min(minX, c.X / c.W); maxX = std::max(maxX, c.X / c.W);
        minY = std::min(minY, c.Y / c.W); maxY = std::max(maxY, c.Y / c.W);
    }
    check(minX > -1.0f && maxX < 1.0f && minY > -1.0f && maxY < 1.0f, "corners inside the tightened frame");
    check(maxX - minX > 1.5f && maxY - minY > 1.5f, "the quad fills most of the tightened frame ("
                                                        + std::to_string(maxX - minX) + " x " + std::to_string(maxY - minY) + ")");
    // A quad behind the camera does not tighten.
    const std::array<Vector3, 4> behind = {Vector3(-0.4f, 0.7f, -3.0f), Vector3(0.4f, 0.7f, -3.0f), Vector3(0.4f, 2.0f, -3.0f),
                                           Vector3(-0.4f, 2.0f, -3.0f)};
    Matrix untouched;
    check(!tightenProjection(view, projection, behind, nearPlane, farPlane, 0.03f, untouched), "a quad behind the camera is rejected");

    // A horizontal plane (the wet street): a camera on the pavement looking
    // across the road, slightly down. A house above the plane lands inside
    // the frame below the horizon, the ground under the plane is clipped.
    {
        const Vector3 walker(0.0f, 1.3f, -8.0f);
        const Vector3 ahead = walker + Vector3(0.0f, -std::sin(MathHelper::ToRadians(10.0f)), -std::cos(MathHelper::ToRadians(10.0f)));
        const Matrix walkView = Matrix::CreateLookAt(walker, ahead, Vector3::Up);
        const Plane water(Vector3::Up, 0.23f);   // y + 0.23 = 0
        const Matrix mirroredWalk = reflectedView(walkView, water);
        const Vector3 house(0.0f, 5.0f, -19.0f);
        const Vector3 houseImage(0.0f, -5.46f, -19.0f);
        check(Vector3::Distance(Vector3::Transform(house, mirroredWalk), Vector3::Transform(houseImage, walkView)) < 1e-3f,
              "a house above the water lands at its image below it");
        // The remap keeps the side holding the far corner it picks: for a
        // street seen from above that is the ground's side when the plane is
        // handed over flipped (as a wall is), so the plane goes in unflipped.
        const Matrix wrongWay = obliqueProjection(projection, mirroredWalk, Plane(-water.Normal, -water.D), -1.0f);
        check(ndcDepth(house, mirroredWalk, wrongWay) < -1.0f, "the flipped plane would clip the house (the rule PlanarReflection::prepare corrects)");
        const Matrix obliqueWalk = obliqueProjection(projection, mirroredWalk, water, -1.0f);
        const Vector3 hv = Vector3::Transform(house, mirroredWalk);
        const Vector4 hc = Vector4::Transform(Vector4(hv.X, hv.Y, hv.Z, 1.0f), obliqueWalk);
        check(hc.W > 0.0f && std::fabs(hc.X / hc.W) < 1.0f && hc.Y / hc.W < 0.0f && hc.Y / hc.W > -1.0f,
              "the house image sits in the frame below the horizon (" + std::to_string(hc.X / hc.W) + ", " + std::to_string(hc.Y / hc.W) + ")");
        const float houseDepth = ndcDepth(house, mirroredWalk, obliqueWalk);
        check(houseDepth > -1.0f && houseDepth < 1.0f, "the house is inside the depth range, got " + std::to_string(houseDepth));
        const float roadDepth = ndcDepth(Vector3(0.0f, -0.37f, -12.0f), mirroredWalk, obliqueWalk);
        check(roadDepth < -1.0f, "the road under the water plane is clipped, got " + std::to_string(roadDepth));
        // A lamp head in the frame (its image 31 degrees below the eye line, 8 m ahead).
        const float lampDepth = ndcDepth(Vector3(3.0f, 3.0f, -16.0f), mirroredWalk, obliqueWalk);
        check(lampDepth > -1.0f && lampDepth < 1.0f, "a lamp head is inside the depth range, got " + std::to_string(lampDepth));
    }

    if (failures == 0) std::printf("cna_room_reflection_tests: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
