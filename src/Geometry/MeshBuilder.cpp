// SPDX-License-Identifier: MIT
#include "CnaRoom/Geometry/MeshBuilder.hpp"

#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

using namespace Microsoft::Xna::Framework;

namespace CnaRoom::Geometry {

namespace {

Vector3 normalised(const Vector3& v, const Vector3& fallback)
{
    const float l2 = v.X * v.X + v.Y * v.Y + v.Z * v.Z;
    if (l2 < 1e-12f) return fallback;
    const float inv = 1.0f / std::sqrt(l2);
    return Vector3(v.X * inv, v.Y * inv, v.Z * inv);
}

Vector3 cross(const Vector3& a, const Vector3& b)
{
    return Vector3(a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X);
}

float dot(const Vector3& a, const Vector3& b) { return a.X * b.X + a.Y * b.Y + a.Z * b.Z; }

Vector3 transformNormal(const Vector3& n, const Matrix& m)
{
    return normalised(Vector3(n.X * m.M11 + n.Y * m.M21 + n.Z * m.M31,
                              n.X * m.M12 + n.Y * m.M22 + n.Z * m.M32,
                              n.X * m.M13 + n.Y * m.M23 + n.Z * m.M33),
                      n);
}

Vector3 transformPoint(const Vector3& p, const Matrix& m)
{
    return Vector3(p.X * m.M11 + p.Y * m.M21 + p.Z * m.M31 + m.M41,
                   p.X * m.M12 + p.Y * m.M22 + p.Z * m.M32 + m.M42,
                   p.X * m.M13 + p.Y * m.M23 + p.Z * m.M33 + m.M43);
}

}  // namespace

BoundingBox MeshData::bounds() const
{
    if (vertices.empty()) return BoundingBox(Vector3::Zero, Vector3::Zero);
    Vector3 lo(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
               std::numeric_limits<float>::max());
    Vector3 hi(-lo.X, -lo.Y, -lo.Z);
    for (const Vertex& v : vertices)
    {
        lo = Vector3::Min(lo, v.Position);
        hi = Vector3::Max(hi, v.Position);
    }
    return BoundingBox(lo, hi);
}

void MeshData::append(const MeshData& other)
{
    const std::uint32_t base = static_cast<std::uint32_t>(vertices.size());
    vertices.insert(vertices.end(), other.vertices.begin(), other.vertices.end());
    indices.reserve(indices.size() + other.indices.size());
    for (std::uint32_t i : other.indices) indices.push_back(base + i);
}

void MeshBuilder::addTriangle(std::uint32_t a, std::uint32_t b, std::uint32_t c)
{
    // Readable counter-clockwise-from-front order becomes CNA's clockwise.
    mesh_.indices.push_back(a);
    mesh_.indices.push_back(c);
    mesh_.indices.push_back(b);
}

void MeshBuilder::addQuad(const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& d,
                          float uvMetres, const Vector2& uvOffset)
{
    const Vector3 normal = normalised(cross(b - a, d - a), Vector3::Up);
    Vector3 u = normalised(b - a, Vector3::Right);
    Vector3 v = normalised(cross(normal, u), Vector3::Up);
    const float scale = 1.0f / std::max(uvMetres, 1e-4f);
    const Vector4 tangent(u.X, u.Y, u.Z, 1.0f);
    const std::uint32_t base = static_cast<std::uint32_t>(mesh_.vertices.size());
    for (const Vector3* p : {&a, &b, &c, &d})
    {
        const Vector3 rel = *p - a;
        const Vector2 uv(dot(rel, u) * scale + uvOffset.X, -dot(rel, v) * scale + uvOffset.Y);
        mesh_.vertices.emplace_back(*p, normal, tangent, uv);
    }
    addTriangle(base, base + 1, base + 2);
    addTriangle(base, base + 2, base + 3);
}

void MeshBuilder::addQuadUv(const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& d)
{
    const Vector3 normal = normalised(cross(b - a, d - a), Vector3::Up);
    const Vector3 u = normalised(b - a, Vector3::Right);
    const Vector4 tangent(u.X, u.Y, u.Z, 1.0f);
    const std::uint32_t base = static_cast<std::uint32_t>(mesh_.vertices.size());
    const Vector2 uvs[4] = {Vector2(0.0f, 1.0f), Vector2(1.0f, 1.0f), Vector2(1.0f, 0.0f), Vector2(0.0f, 0.0f)};
    int i = 0;
    for (const Vector3* p : {&a, &b, &c, &d}) mesh_.vertices.emplace_back(*p, normal, tangent, uvs[i++]);
    addTriangle(base, base + 1, base + 2);
    addTriangle(base, base + 2, base + 3);
}

void MeshBuilder::addTriangleFace(const Vector3& a, const Vector3& b, const Vector3& c, float uvMetres)
{
    const Vector3 normal = normalised(cross(b - a, c - a), Vector3::Up);
    Vector3 u = normalised(b - a, Vector3::Right);
    Vector3 v = normalised(cross(normal, u), Vector3::Up);
    const float scale = 1.0f / std::max(uvMetres, 1e-4f);
    const Vector4 tangent(u.X, u.Y, u.Z, 1.0f);
    const std::uint32_t base = static_cast<std::uint32_t>(mesh_.vertices.size());
    for (const Vector3* p : {&a, &b, &c})
    {
        const Vector3 rel = *p - a;
        const Vector2 uv(dot(rel, u) * scale, -dot(rel, v) * scale);
        mesh_.vertices.emplace_back(*p, normal, tangent, uv);
    }
    addTriangle(base, base + 1, base + 2);
}

void MeshBuilder::addBox(const Vector3& mn, const Vector3& mx, float uvMetres, unsigned faceMask)
{
    const Vector3 p000(mn.X, mn.Y, mn.Z), p100(mx.X, mn.Y, mn.Z), p010(mn.X, mx.Y, mn.Z),
        p110(mx.X, mx.Y, mn.Z), p001(mn.X, mn.Y, mx.Z), p101(mx.X, mn.Y, mx.Z),
        p011(mn.X, mx.Y, mx.Z), p111(mx.X, mx.Y, mx.Z);
    if (faceMask & (1u << 0)) addQuad(p101, p100, p110, p111, uvMetres);  // +X
    if (faceMask & (1u << 1)) addQuad(p000, p001, p011, p010, uvMetres);  // -X
    if (faceMask & (1u << 2)) addQuad(p011, p111, p110, p010, uvMetres);  // +Y
    if (faceMask & (1u << 3)) addQuad(p000, p100, p101, p001, uvMetres);  // -Y
    if (faceMask & (1u << 4)) addQuad(p001, p101, p111, p011, uvMetres);  // +Z
    if (faceMask & (1u << 5)) addQuad(p100, p000, p010, p110, uvMetres);  // -Z
}

void MeshBuilder::addBoxWorldUv(const Vector3& mn, const Vector3& mx, float uvMetres,
                                unsigned faceMask)
{
    const float s = 1.0f / std::max(uvMetres, 1e-4f);
    const Vector3 p000(mn.X, mn.Y, mn.Z), p100(mx.X, mn.Y, mn.Z), p010(mn.X, mx.Y, mn.Z),
        p110(mx.X, mx.Y, mn.Z), p001(mn.X, mn.Y, mx.Z), p101(mx.X, mn.Y, mx.Z),
        p011(mn.X, mx.Y, mx.Z), p111(mx.X, mx.Y, mx.Z);
    // Offsets place the quad's origin corner on the world grid.
    if (faceMask & (1u << 0)) addQuad(p101, p100, p110, p111, uvMetres, Vector2(-mx.Z * s, mn.Y * s));
    if (faceMask & (1u << 1)) addQuad(p000, p001, p011, p010, uvMetres, Vector2(mn.Z * s, mn.Y * s));
    if (faceMask & (1u << 2)) addQuad(p011, p111, p110, p010, uvMetres, Vector2(mn.X * s, -mx.Z * s));
    if (faceMask & (1u << 3)) addQuad(p000, p100, p101, p001, uvMetres, Vector2(mn.X * s, mn.Z * s));
    if (faceMask & (1u << 4)) addQuad(p001, p101, p111, p011, uvMetres, Vector2(mn.X * s, mn.Y * s));
    if (faceMask & (1u << 5)) addQuad(p100, p000, p010, p110, uvMetres, Vector2(-mx.X * s, mn.Y * s));
}

void MeshBuilder::addCylinder(const Vector3& base, float radius, float height, int segments,
                              float uvMetres, bool caps)
{
    segments = std::max(segments, 3);
    const float s = 1.0f / std::max(uvMetres, 1e-4f);
    const std::uint32_t start = static_cast<std::uint32_t>(mesh_.vertices.size());
    const float circumference = 2.0f * MathHelper::Pi * radius;
    for (int i = 0; i <= segments; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(segments);
        const float angle = t * MathHelper::TwoPi;
        const Vector3 n(std::cos(angle), 0.0f, std::sin(angle));
        const Vector4 tangent(-std::sin(angle), 0.0f, std::cos(angle), 1.0f);
        const Vector3 bottom = base + n * radius;
        const Vector3 top = bottom + Vector3(0.0f, height, 0.0f);
        mesh_.vertices.emplace_back(bottom, n, tangent, Vector2(t * circumference * s, height * s));
        mesh_.vertices.emplace_back(top, n, tangent, Vector2(t * circumference * s, 0.0f));
    }
    for (int i = 0; i < segments; ++i)
    {
        const std::uint32_t b0 = start + static_cast<std::uint32_t>(i) * 2;
        const std::uint32_t t0 = b0 + 1, b1 = b0 + 2, t1 = b0 + 3;
        addTriangle(b0, t0, t1);
        addTriangle(b0, t1, b1);
    }
    if (!caps) return;
    for (int cap = 0; cap < 2; ++cap)
    {
        const float y = cap == 0 ? base.Y : base.Y + height;
        const Vector3 n(0.0f, cap == 0 ? -1.0f : 1.0f, 0.0f);
        const std::uint32_t centre = static_cast<std::uint32_t>(mesh_.vertices.size());
        mesh_.vertices.emplace_back(Vector3(base.X, y, base.Z), n, Vector4(1.0f, 0.0f, 0.0f, 1.0f),
                                    Vector2(base.X * s, base.Z * s));
        for (int i = 0; i <= segments; ++i)
        {
            const float angle = static_cast<float>(i) / static_cast<float>(segments) * MathHelper::TwoPi;
            const Vector3 p(base.X + std::cos(angle) * radius, y, base.Z + std::sin(angle) * radius);
            mesh_.vertices.emplace_back(p, n, Vector4(1.0f, 0.0f, 0.0f, 1.0f),
                                        Vector2(p.X * s, p.Z * s));
        }
        for (int i = 0; i < segments; ++i)
        {
            const std::uint32_t a = centre + 1 + static_cast<std::uint32_t>(i);
            if (cap == 0) addTriangle(centre, a, a + 1);
            else addTriangle(centre, a + 1, a);
        }
    }
}

void MeshBuilder::addSphere(const Vector3& centre, float radius, int slices, int stacks,
                            float uvMetres)
{
    slices = std::max(slices, 3);
    stacks = std::max(stacks, 2);
    const float s = 1.0f / std::max(uvMetres, 1e-4f);
    const std::uint32_t start = static_cast<std::uint32_t>(mesh_.vertices.size());
    for (int j = 0; j <= stacks; ++j)
    {
        const float v = static_cast<float>(j) / static_cast<float>(stacks);
        const float phi = v * MathHelper::Pi;
        for (int i = 0; i <= slices; ++i)
        {
            const float u = static_cast<float>(i) / static_cast<float>(slices);
            const float theta = u * MathHelper::TwoPi;
            const Vector3 n(std::sin(phi) * std::cos(theta), std::cos(phi),
                            std::sin(phi) * std::sin(theta));
            const Vector4 tangent(-std::sin(theta), 0.0f, std::cos(theta), 1.0f);
            mesh_.vertices.emplace_back(centre + n * radius, n, tangent,
                                        Vector2(u * MathHelper::TwoPi * radius * s,
                                                v * MathHelper::Pi * radius * s));
        }
    }
    const std::uint32_t row = static_cast<std::uint32_t>(slices + 1);
    for (int j = 0; j < stacks; ++j)
        for (int i = 0; i < slices; ++i)
        {
            const std::uint32_t a = start + static_cast<std::uint32_t>(j) * row
                                    + static_cast<std::uint32_t>(i);
            const std::uint32_t b = a + row;
            addTriangle(a, a + 1, b + 1);
            addTriangle(a, b + 1, b);
        }
}

void MeshBuilder::addTorus(const Vector3& centre, float ringRadius, float tubeRadius,
                           int ringSegments, int tubeSegments, float uvMetres)
{
    ringSegments = std::max(ringSegments, 3);
    tubeSegments = std::max(tubeSegments, 3);
    const float s = 1.0f / std::max(uvMetres, 1e-4f);
    const std::uint32_t start = static_cast<std::uint32_t>(mesh_.vertices.size());
    for (int i = 0; i <= ringSegments; ++i)
    {
        const float u = static_cast<float>(i) / static_cast<float>(ringSegments);
        const float a = u * MathHelper::TwoPi;
        const Vector3 ring(std::cos(a), 0.0f, std::sin(a));
        const Vector4 tangent(-std::sin(a), 0.0f, std::cos(a), 1.0f);
        for (int j = 0; j <= tubeSegments; ++j)
        {
            const float v = static_cast<float>(j) / static_cast<float>(tubeSegments);
            const float b = v * MathHelper::TwoPi;
            const Vector3 n = ring * std::cos(b) + Vector3(0.0f, std::sin(b), 0.0f);
            const Vector3 p = centre + ring * ringRadius + n * tubeRadius;
            mesh_.vertices.emplace_back(p, n, tangent,
                                        Vector2(u * MathHelper::TwoPi * ringRadius * s,
                                                v * MathHelper::TwoPi * tubeRadius * s));
        }
    }
    const std::uint32_t row = static_cast<std::uint32_t>(tubeSegments + 1);
    for (int i = 0; i < ringSegments; ++i)
        for (int j = 0; j < tubeSegments; ++j)
        {
            const std::uint32_t a = start + static_cast<std::uint32_t>(i) * row
                                    + static_cast<std::uint32_t>(j);
            const std::uint32_t b = a + row;
            addTriangle(a, b, b + 1);
            addTriangle(a, b + 1, a + 1);
        }
}

void MeshBuilder::addFloor(float x0, float z0, float x1, float z1, float y, float uvMetres,
                           bool faceUp)
{
    const float s = 1.0f / std::max(uvMetres, 1e-4f);
    const Vector3 a(x0, y, z1), b(x1, y, z1), c(x1, y, z0), d(x0, y, z0);
    if (faceUp) addQuad(a, b, c, d, uvMetres, Vector2(x0 * s, z1 * s));
    else addQuad(d, c, b, a, uvMetres, Vector2(x0 * s, z0 * s));
}

void MeshBuilder::transform(const Matrix& world)
{
    for (Vertex& v : mesh_.vertices)
    {
        v.Position = transformPoint(v.Position, world);
        v.Normal = transformNormal(v.Normal, world);
        const Vector3 t = transformNormal(Vector3(v.Tangent.X, v.Tangent.Y, v.Tangent.Z), world);
        v.Tangent = Vector4(t.X, t.Y, t.Z, v.Tangent.W);
    }
}

MeshData MeshBuilder::take()
{
    MeshData out = std::move(mesh_);
    mesh_ = MeshData{};
    return out;
}

void MeshBuilder::computeTangents(MeshData& mesh)
{
    std::vector<Vector3> tan(mesh.vertices.size(), Vector3::Zero);
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
    {
        const Vertex& v0 = mesh.vertices[mesh.indices[i]];
        const Vertex& v1 = mesh.vertices[mesh.indices[i + 1]];
        const Vertex& v2 = mesh.vertices[mesh.indices[i + 2]];
        const Vector3 e1 = v1.Position - v0.Position, e2 = v2.Position - v0.Position;
        const Vector2 d1 = v1.TextureCoordinate - v0.TextureCoordinate;
        const Vector2 d2 = v2.TextureCoordinate - v0.TextureCoordinate;
        const float det = d1.X * d2.Y - d2.X * d1.Y;
        if (std::fabs(det) < 1e-10f) continue;
        const float r = 1.0f / det;
        const Vector3 t = (e1 * d2.Y - e2 * d1.Y) * r;
        for (int k = 0; k < 3; ++k) tan[mesh.indices[i + static_cast<std::size_t>(k)]] += t;
    }
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i)
    {
        Vertex& v = mesh.vertices[i];
        const Vector3 n = v.Normal;
        Vector3 t = tan[i] - n * dot(n, tan[i]);
        t = normalised(t, normalised(cross(n, Vector3::Up), Vector3::Right));
        v.Tangent = Vector4(t.X, t.Y, t.Z, 1.0f);
    }
}

}  // namespace CnaRoom::Geometry
