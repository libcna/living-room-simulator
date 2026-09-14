// SPDX-License-Identifier: MIT
#pragma once

#include "CnaRoom/Geometry/MeshData.hpp"

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

namespace CnaRoom::Geometry {

/**
 * @brief Procedural geometry in real-world metres.
 *
 * Every primitive is emitted in CNA's front-face convention (a face is kept by
 * CullCounterClockwise when its vertices are clockwise seen from the front),
 * with normals, tangents and texture coordinates in metres divided by
 * @c uvMetres so a texture tile is a stated physical size.
 */
class MeshBuilder
{
public:
    /// Adds a quad from four corners given counter-clockwise as seen from the
    /// front; the builder reorders them for CNA. UVs are derived from the
    /// corners' projection onto the quad's own axes.
    void addQuad(const Microsoft::Xna::Framework::Vector3& a,
                 const Microsoft::Xna::Framework::Vector3& b,
                 const Microsoft::Xna::Framework::Vector3& c,
                 const Microsoft::Xna::Framework::Vector3& d, float uvMetres,
                 const Microsoft::Xna::Framework::Vector2& uvOffset = {0.0f, 0.0f});

    /// A quad whose texture spans it exactly once: a at (0, 1), b at (1, 1),
    /// c at (1, 0), d at (0, 0) -- a picture, a window seen from outside.
    void addQuadUv(const Microsoft::Xna::Framework::Vector3& a,
                   const Microsoft::Xna::Framework::Vector3& b,
                   const Microsoft::Xna::Framework::Vector3& c,
                   const Microsoft::Xna::Framework::Vector3& d);

    /// A single triangle, corners counter-clockwise as seen from the front.
    void addTriangleFace(const Microsoft::Xna::Framework::Vector3& a,
                         const Microsoft::Xna::Framework::Vector3& b,
                         const Microsoft::Xna::Framework::Vector3& c, float uvMetres);

    /// Axis-aligned box between two corners; faces outward. Faces can be
    /// omitted (e.g. the back of a wall) through the mask (1<<face):
    /// 0 +X, 1 -X, 2 +Y, 3 -Y, 4 +Z, 5 -Z.
    void addBox(const Microsoft::Xna::Framework::Vector3& min,
                const Microsoft::Xna::Framework::Vector3& max, float uvMetres,
                unsigned faceMask = 0x3F);

    /// Box with UV per face on a world grid (texture continuity across boxes).
    void addBoxWorldUv(const Microsoft::Xna::Framework::Vector3& min,
                       const Microsoft::Xna::Framework::Vector3& max, float uvMetres,
                       unsigned faceMask = 0x3F);

    /// A closed cylinder along Y.
    void addCylinder(const Microsoft::Xna::Framework::Vector3& base, float radius, float height,
                     int segments, float uvMetres, bool caps = true);

    /// A UV sphere.
    void addSphere(const Microsoft::Xna::Framework::Vector3& centre, float radius, int slices,
                   int stacks, float uvMetres);

    /// A torus section (for handles, rings) around Y.
    void addTorus(const Microsoft::Xna::Framework::Vector3& centre, float ringRadius,
                  float tubeRadius, int ringSegments, int tubeSegments, float uvMetres);

    /// A rectangle in the XZ plane at height y facing up (or down).
    void addFloor(float x0, float z0, float x1, float z1, float y, float uvMetres,
                  bool faceUp = true);

    /// Transforms everything added so far by a matrix.
    void transform(const Microsoft::Xna::Framework::Matrix& world);

    [[nodiscard]] MeshData& mesh() { return mesh_; }
    [[nodiscard]] const MeshData& mesh() const { return mesh_; }
    [[nodiscard]] MeshData take();

    /// Recomputes per-vertex tangents from the UV layout (after transform or
    /// for imported data).
    static void computeTangents(MeshData& mesh);

private:
    void addTriangle(std::uint32_t a, std::uint32_t b, std::uint32_t c);
    MeshData mesh_;
};

}  // namespace CnaRoom::Geometry
