// SPDX-License-Identifier: MIT
// The world outside the windows: a residential street with pavements, a road,
// terraced houses opposite, neighbours either side, trees, hedges, street
// lights and a distant skyline. Everything is procedural geometry on the
// project's procedural materials; every item is flagged `exterior` so it
// keeps the sky's image-based lighting instead of the interior probes.
#include "CnaRoom/Scene/RoomScene.hpp"
#include "CnaRoom/Render/Material.hpp"
#include "CnaRoom/Render/MaterialLibrary.hpp"
#include "CnaRoom/Render/PlanarReflection.hpp"

#include "CnaRoom/Assets/Image.hpp"
#include "CnaRoom/Geometry/MeshBuilder.hpp"
#include "CnaRoom/Render/SceneRenderer.hpp"

#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"

#include <algorithm>
#include <map>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using CnaRoom::Geometry::MeshBuilder;
using CnaRoom::Geometry::MeshData;

namespace CnaRoom {

namespace {

constexpr unsigned kFacePosX = 1u << 0, kFaceNegX = 1u << 1, kFacePosY = 1u << 2, kFaceNegY = 1u << 3,
                   kFacePosZ = 1u << 4, kFaceNegZ = 1u << 5, kAllFaces = 0x3Fu;

/// Deterministic pseudo-random numbers for the street layout.
struct Dice
{
    std::uint32_t state;
    explicit Dice(std::uint32_t seed) : state(seed * 2654435761u + 12345u) {}
    float next()
    {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        return static_cast<float>(state & 0xFFFFFFu) / static_cast<float>(0x1000000u);
    }
    float range(float lo, float hi) { return lo + (hi - lo) * next(); }
    int pick(int n) { return std::min(n - 1, static_cast<int>(next() * static_cast<float>(n))); }
};

/// One street elevation: buildings share their front plane and face `facing`
/// (+Z for the row opposite, which looks back at the room).
struct Building
{
    float x0, x1;        ///< extent along the street
    float depth;         ///< back wall distance from the front plane
    int storeys;
    float storeyHeight;
    int material;        ///< index into the facade material list
    bool pitched;        ///< pitched tiled roof versus flat parapet
    bool doorLeft;
    float doorX;
};

/// Mesh builders keyed by a 12 m segment along the street. PbrEffect lights
/// every draw with one punctual light, so exterior geometry is split into
/// pieces small enough that "the nearest street lamp" is the right answer.
struct ChunkSet
{
    static constexpr float kWidth = 12.0f;
    std::map<int, MeshBuilder> chunks;
    MeshBuilder& at(float x) { return chunks[static_cast<int>(std::floor(x / kWidth))]; }
};

}  // namespace

void RoomScene::buildExterior()
{
    const RoomLayout& L = layout_;
    const float ground = -L.floorThickness;             // the house floor sits 0.25 m above ground
    const float wallFace = -L.halfDepth - L.exteriorWallThickness;   // -2.6: the window wall's outer face
    const float hedgeZ = wallFace - 3.7f;               // front garden depth
    const float pavementZ = hedgeZ - 0.5f;              // near pavement starts behind the hedge
    const float kerbZ = pavementZ - 2.2f;               // road edge
    const float roadWidth = 7.4f;
    const float farKerbZ = kerbZ - roadWidth;
    const float farPavementZ = farKerbZ - 2.4f;         // opposite building line
    const float roadY = ground - 0.12f;
    const float streetHalf = 48.0f;

    const auto placeChunks = [&](ChunkSet& set, const char* material, const std::string& name, bool castsShadow,
                                 bool shadowOnly = false) {
        for (auto& [chunk, builder] : set.chunks)
            place(builder.take(), material, name + "#" + std::to_string(chunk), Matrix::getIdentityProperty(), castsShadow, true,
                  shadowOnly);
        set.chunks.clear();
    };
    const int chunkCount = static_cast<int>(std::ceil(2.0f * streetHalf / ChunkSet::kWidth));
    const auto chunkRange = [&](int i, float& x0, float& x1) {
        x0 = -streetHalf + static_cast<float>(i) * ChunkSet::kWidth;
        x1 = std::min(streetHalf, x0 + ChunkSet::kWidth);
    };

    // ---- ground: lawn strip, pavements, road with kerbs, distant plane ----
    {
        // The distant plane leaves the road's trench out: the road sits 12 cm
        // under the pavements and a plane 2 cm under them had been covering it
        // (the "road" in every street view was this grass, and the asphalt,
        // its paint and its wet reflections never showed).
        MeshBuilder far;
        const float trenchNear = kerbZ + 0.2f, trenchFar = farKerbZ - 0.2f;
        far.addFloor(-400.0f, trenchNear, 400.0f, 400.0f, ground - 0.02f, 4.0f, true);      // our side of the road
        far.addFloor(-400.0f, -400.0f, 400.0f, trenchFar, ground - 0.02f, 4.0f, true);      // beyond the opposite pavement
        far.addFloor(-400.0f, trenchFar, -streetHalf, trenchNear, ground - 0.02f, 4.0f, true);   // past the street's ends
        far.addFloor(streetHalf, trenchFar, 400.0f, trenchNear, ground - 0.02f, 4.0f, true);
        place(far.take(), "grass", "exterior_far_ground", Matrix::getIdentityProperty(), false, true);

        ChunkSet lawn, paving, kerbs, road, paint;
        const float centreZ = (kerbZ + farKerbZ) * 0.5f;
        for (int i = 0; i < chunkCount; ++i)
        {
            float x0, x1;
            chunkRange(i, x0, x1);
            const float xm = (x0 + x1) * 0.5f;
            lawn.at(xm).addFloor(x0, hedgeZ, x1, wallFace + 8.0f, ground, 2.0f, true);
            paving.at(xm).addFloor(x0, kerbZ, x1, pavementZ, ground + 0.001f, 1.2f, true);
            paving.at(xm).addFloor(x0, farPavementZ, x1, farKerbZ, ground + 0.001f, 1.2f, true);
            kerbs.at(xm).addBoxWorldUv(Vector3(x0, roadY - 0.05f, kerbZ - 0.15f), Vector3(x1, ground, kerbZ), 1.0f, kFacePosY | kFaceNegZ);
            kerbs.at(xm).addBoxWorldUv(Vector3(x0, roadY - 0.05f, farKerbZ), Vector3(x1, ground, farKerbZ + 0.15f), 1.0f, kFacePosY | kFacePosZ);
            road.at(xm).addFloor(x0, farKerbZ + 0.15f, x1, kerbZ - 0.15f, roadY, 3.0f, true);
            for (float x = x0 + 1.0f; x + 2.0f <= x1; x += 6.0f)
                paint.at(xm).addFloor(x, centreZ - 0.06f, x + 2.0f, centreZ + 0.06f, roadY + 0.004f, 1.0f, true);
            paint.at(xm).addFloor(x0, farKerbZ + 2.2f, x1, farKerbZ + 2.3f, roadY + 0.004f, 1.0f, true);   // parking bay line
        }
        // Garden paths from the pavement to the houses either side of ours.
        for (float px : {-9.2f, 9.8f, 1.9f})
            paving.at(px).addFloor(px - 0.6f, pavementZ, px + 0.6f, wallFace, ground + 0.002f, 1.2f, true);
        placeChunks(lawn, "grass", "exterior_lawn", false);
        placeChunks(paving, "paving", "exterior_pavement", false);
        placeChunks(kerbs, "concrete", "exterior_kerbs", false);
        placeChunks(road, "asphalt", "exterior_road", false);
        placeChunks(paint, "road_paint", "exterior_road_paint", false);
        // The wet street: the road and pavements mirror the houses, lamps and
        // sky while it rains (an exterior-only capture in a plane just above
        // the pavements, added over the plain surfaces by their Fresnel share
        // times the wetness). The plane sits above every ground surface and
        // the ground itself is left out of the capture, or the mirrored
        // camera under the road would look up at the undersides of the lawn
        // and pavement and mirror nothing else; the road lies 14 cm under
        // the plane, which no one sees from a window.
        const float waterY = ground + 0.02f;
        ReflectionPlane street;
        street.name = "wet-street";
        street.plane = Plane(Vector3::Up, -waterY);   // y - waterY = 0
        street.corners = {Vector3(-16.0f, waterY, hedgeZ - 0.3f), Vector3(16.0f, waterY, hedgeZ - 0.3f),
                          Vector3(16.0f, waterY, farPavementZ - 0.5f), Vector3(-16.0f, waterY, farPavementZ - 0.5f)};
        street.exteriorOnly = true;
        street.skipBelow = ground + 0.15f;
        street.enabled = false;
        streetReflection_ = renderer_.addReflectionPlane(street);
        for (const char* name : {"asphalt", "paving"})
            if (Material* m = materials_.edit(name))
            {
                m->reflectionPlane = streetReflection_;
                m->reflectionOverlay = true;
                m->reflectionF0 = 0.02f;         // a film of water
                m->reflectionFresnel = true;
                m->reflectionTint = Vector3::Zero;   // dry: the weather sets it
                // Asphalt puddles in its ruts; paving slabs hold water in their joints and dips.
                m->reflectionPuddles = std::string(name) == "asphalt" ? 0.85f : 0.6f;
                m->reflectionPuddleScale = std::string(name) == "asphalt" ? 1.8f : 0.9f;
            }
    }

    // ---- hedge along the front gardens, with gaps for the paths ----
    {
        ChunkSet hedge, fringe;
        const float top = ground + 1.05f;
        std::vector<float> gaps = {-9.2f, 9.8f, 1.9f};
        std::sort(gaps.begin(), gaps.end());
        // A clipped hedge is a box only from a distance: leaf clumps (masked
        // spheres) straddle the top edges and the cut ends so the silhouette
        // breaks up and the sun catches the leaves.
        const auto run = [&](float xa, float xb, float chunkKey) {
            hedge.at(chunkKey).addBoxWorldUv(Vector3(xa, ground, hedgeZ), Vector3(xb, top, hedgeZ + 0.55f), 1.0f, kAllFaces & ~kFaceNegY);
            Dice dice(2000u + static_cast<std::uint32_t>(std::lround((xa + 100.0f) * 7.0f)));
            MeshBuilder& leaves = fringe.at(chunkKey);
            for (float x = xa + 0.10f; x < xb - 0.05f; x += dice.range(0.13f, 0.21f))
            {
                const float side = dice.next();
                const float z = side < 0.4f ? hedgeZ + dice.range(0.03f, 0.12f)
                                : side < 0.8f ? hedgeZ + 0.55f - dice.range(0.03f, 0.12f)
                                              : hedgeZ + dice.range(0.15f, 0.40f);
                leaves.addSphere(Vector3(x, top - dice.range(0.06f, 0.14f), z), dice.range(0.14f, 0.22f), 7, 4, 1.0f);
            }
            for (float xe : {xa + 0.02f, xb - 0.02f})
                for (int k = 0; k < 3; ++k)
                    leaves.addSphere(Vector3(xe, ground + 0.25f + static_cast<float>(k) * 0.3f + dice.range(-0.05f, 0.05f),
                                             hedgeZ + 0.275f + dice.range(-0.2f, 0.2f)),
                                     dice.range(0.12f, 0.18f), 7, 4, 1.0f);
        };
        for (int i = 0; i < chunkCount; ++i)
        {
            float x0, x1;
            chunkRange(i, x0, x1);
            float cursor = x0;
            for (float g : gaps)
            {
                if (g + 0.8f <= x0 || g - 0.8f >= x1) continue;
                if (g - 0.8f > cursor) run(cursor, g - 0.8f, (x0 + x1) * 0.5f);
                cursor = std::max(cursor, g + 0.8f);
            }
            if (cursor < x1) run(cursor, x1, (x0 + x1) * 0.5f);
        }
        placeChunks(hedge, "hedge", "exterior_hedge", true);
        placeChunks(fringe, "hedge_fringe", "exterior_hedge_fringe", true);
    }

    // ---- buildings ----
    const char* const facadeMaterials[] = {"facade_render", "brick_red", "facade_render_2", "facade_render_3", "facade_render_4"};
    ChunkSet facades[5];
    ChunkSet frames, glassDark, glassLit, glassLitDim, glassLitCool, roofsTile, roofsSlate, doors, chimneys, sills, gutters, plinths;
    Dice dice(20260912u);

    // Adds one building whose front plane is z = front, facing +Z (towards
    // the room) when facing > 0, else -Z.
    const auto addBuilding = [&](const Building& b, float front, float facing) {
        const float h = static_cast<float>(b.storeys) * b.storeyHeight;
        const float zFront = front, zBack = front - facing * b.depth;
        const float zMin = std::min(zFront, zBack), zMax = std::max(zFront, zBack);
        const float bx = (b.x0 + b.x1) * 0.5f;
        MeshBuilder& wall = facades[b.material].at(bx);
        // The body stops a reveal's depth behind the front plane; the front is
        // a skin in pieces around the openings, so every window and door sits
        // in a real recess with reveals that take the sun's shadow.
        constexpr float kReveal = 0.20f;
        const float zSkinIn = zFront - facing * kReveal;
        const float zBodyMin = std::min(zSkinIn, zBack), zBodyMax = std::max(zSkinIn, zBack);
        wall.addBoxWorldUv(Vector3(b.x0, ground, zBodyMin), Vector3(b.x1, h, zBodyMax), 2.0f, kAllFaces & ~kFaceNegY);
        const float zSkin0 = std::min(zFront, zSkinIn), zSkin1 = std::max(zFront, zSkinIn);
        const auto skin = [&](float x0, float y0, float x1, float y1) {
            if (x1 - x0 < 0.005f || y1 - y0 < 0.005f) return;
            wall.addBoxWorldUv(Vector3(x0, y0, zSkin0), Vector3(x1, y1, zSkin1), 2.0f, kAllFaces & ~kFaceNegY);
        };
        // A plinth: a concrete band at the foot of the front, a little proud.
        const float zPlinth1 = zFront + facing * 0.025f;
        plinths.at(bx).addBoxWorldUv(Vector3(b.x0, ground, std::min(zFront, zPlinth1)), Vector3(b.x1, ground + 0.45f, std::max(zFront, zPlinth1)), 1.0f, kAllFaces & ~kFaceNegY);
        // Windows: bays every ~2.6 m, one door bay on the ground floor.
        const float width = b.x1 - b.x0;
        const int bays = std::max(1, static_cast<int>(width / 2.6f));
        const float bay = width / static_cast<float>(bays);
        const float winW = std::min(1.2f, bay * 0.5f), winH = 1.45f;
        const float zFace = zSkinIn + facing * 0.012f;                        // glass just proud of the recess's back
        const float zFrame0 = zSkinIn, zFrame1 = zSkinIn + facing * 0.06f;   // the frame in the recess
        const int doorBay = b.doorLeft ? 0 : bays - 1;
        for (int s = 0; s < b.storeys; ++s)
        {
            const float floorY = ground + static_cast<float>(s) * b.storeyHeight;
            const float ceilY = s == b.storeys - 1 ? h : floorY + b.storeyHeight;
            const float sill = floorY + (s == 0 ? 1.0f : 0.95f);
            for (int i = 0; i < bays; ++i)
            {
                const float cx = b.x0 + (static_cast<float>(i) + 0.5f) * bay;
                const float bayX0 = b.x0 + static_cast<float>(i) * bay, bayX1 = bayX0 + bay;
                if (s == 0 && i == doorBay)
                {
                    // Door: a dark panel at the back of its recess with a frame and a step.
                    const float dw = 1.0f, dh = 2.15f;
                    skin(bayX0, floorY, cx - dw * 0.5f - 0.08f, ceilY);
                    skin(cx + dw * 0.5f + 0.08f, floorY, bayX1, ceilY);
                    skin(cx - dw * 0.5f - 0.08f, ground + dh + 0.08f, cx + dw * 0.5f + 0.08f, ceilY);
                    doors.at(bx).addBoxWorldUv(Vector3(cx - dw * 0.5f, ground, std::min(zFace, zSkinIn)),
                                        Vector3(cx + dw * 0.5f, ground + dh, std::max(zFace, zSkinIn)), 1.0f, kAllFaces);
                    frames.at(bx).addBoxWorldUv(Vector3(cx - dw * 0.5f - 0.08f, ground, std::min(zFrame0, zFrame1)),
                                         Vector3(cx - dw * 0.5f, ground + dh + 0.08f, std::max(zFrame0, zFrame1)), 0.5f, kAllFaces);
                    frames.at(bx).addBoxWorldUv(Vector3(cx + dw * 0.5f, ground, std::min(zFrame0, zFrame1)),
                                         Vector3(cx + dw * 0.5f + 0.08f, ground + dh + 0.08f, std::max(zFrame0, zFrame1)), 0.5f, kAllFaces);
                    frames.at(bx).addBoxWorldUv(Vector3(cx - dw * 0.5f - 0.08f, ground + dh, std::min(zFrame0, zFrame1)),
                                         Vector3(cx + dw * 0.5f + 0.08f, ground + dh + 0.08f, std::max(zFrame0, zFrame1)), 0.5f, kAllFaces);
                    chimneys.at(bx).addBoxWorldUv(Vector3(cx - 0.7f, ground, std::min(zFront, zFront + facing * 0.35f)),
                                           Vector3(cx + 0.7f, ground + 0.15f, std::max(zFront, zFront + facing * 0.35f)), 1.0f, kAllFaces & ~kFaceNegY);
                    continue;
                }
                const float x0 = cx - winW * 0.5f, x1 = cx + winW * 0.5f;
                const float y0 = sill, y1 = sill + winH;
                // The skin around this window: piers either side, the spandrel below, the lintel above.
                skin(bayX0, floorY, x0, ceilY);
                skin(x1, floorY, bayX1, ceilY);
                skin(x0, floorY, x1, y0);
                skin(x0, y1, x1, ceilY);
                // Lit at night: about a third of the windows, fixed per window;
                // most warm, some dim behind curtains, a few in television blue.
                const bool lit = dice.next() < 0.35f;
                // The kind comes from a hash of the window's place, not the
                // dice: an extra draw would reshuffle every building after it.
                const float kind = std::fmod(std::fabs(std::sin(x0 * 12.9898f + y0 * 78.233f + zFace * 37.719f) * 43758.5453f), 1.0f);
                MeshBuilder& glass = !lit ? glassDark.at(bx) : kind < 0.55f ? glassLit.at(bx) : kind < 0.85f ? glassLitDim.at(bx) : glassLitCool.at(bx);
                if (facing > 0.0f) glass.addQuadUv(Vector3(x0, y0, zFace), Vector3(x1, y0, zFace), Vector3(x1, y1, zFace), Vector3(x0, y1, zFace));
                else glass.addQuadUv(Vector3(x1, y0, zFace), Vector3(x0, y0, zFace), Vector3(x0, y1, zFace), Vector3(x1, y1, zFace));
                const float zf0 = std::min(zFrame0, zFrame1), zf1 = std::max(zFrame0, zFrame1);
                const float t = 0.07f;
                frames.at(bx).addBoxWorldUv(Vector3(x0 - t, y0 - t, zf0), Vector3(x0, y1 + t, zf1), 0.5f, kAllFaces);
                frames.at(bx).addBoxWorldUv(Vector3(x1, y0 - t, zf0), Vector3(x1 + t, y1 + t, zf1), 0.5f, kAllFaces);
                frames.at(bx).addBoxWorldUv(Vector3(x0, y1, zf0), Vector3(x1, y1 + t, zf1), 0.5f, kAllFaces);
                frames.at(bx).addBoxWorldUv(Vector3(x0 - t, y0 - t, zf0), Vector3(x1 + t, y0, zf1), 0.5f, kAllFaces);
                // Glazing bar, and a stone sill through the recess and a little proud of the front.
                frames.at(bx).addBoxWorldUv(Vector3(cx - 0.025f, y0, zf0), Vector3(cx + 0.025f, y1, zf0 + 0.03f), 0.5f, kAllFaces);
                const float zs1 = zFront + facing * 0.06f;
                sills.at(bx).addBoxWorldUv(Vector3(x0 - 0.06f, y0 - 0.07f, std::min(zSkinIn, zs1)), Vector3(x1 + 0.06f, y0, std::max(zSkinIn, zs1)), 0.5f, kAllFaces);
            }
        }
        // Rainwater: a gutter along the front eave (or the parapet's foot) and a downpipe down one end.
        {
            const float gutterZ = b.pitched ? zFront + facing * 0.35f : zFront + facing * 0.02f;
            const float gutterY = b.pitched ? h - 0.02f : h + 0.45f;
            gutters.at(bx).addBoxWorldUv(Vector3(b.x0 - 0.05f, gutterY, std::min(gutterZ - 0.06f, gutterZ + 0.06f)),
                                          Vector3(b.x1 + 0.05f, gutterY + 0.11f, std::max(gutterZ - 0.06f, gutterZ + 0.06f)), 0.5f, kAllFaces);
            const float pipeX = b.doorLeft ? b.x1 - 0.22f : b.x0 + 0.22f;
            const float pipeZ = zFront + facing * 0.09f;
            gutters.at(bx).addCylinder(Vector3(pipeX, ground + 0.02f, pipeZ), 0.045f, gutterY - ground - 0.02f, 8, 0.5f, false);
            // The swan neck from the gutter to the wall.
            gutters.at(bx).addBoxWorldUv(Vector3(pipeX - 0.045f, gutterY - 0.02f, std::min(pipeZ, gutterZ)), Vector3(pipeX + 0.045f, gutterY + 0.06f, std::max(pipeZ, gutterZ)), 0.5f, kAllFaces);
        }
        // Roof.
        if (b.pitched)
        {
            MeshBuilder& roof = (b.material == 1 || b.material == 3) ? roofsTile.at(bx) : roofsSlate.at(bx);
            const float rise = b.depth * 0.42f;
            const float ridgeZ = (zMin + zMax) * 0.5f;
            const float eave = 0.35f;
            const Vector3 fl(b.x0 - 0.05f, h, zMax + eave), fr(b.x1 + 0.05f, h, zMax + eave);
            const Vector3 bl(b.x0 - 0.05f, h, zMin - eave), br(b.x1 + 0.05f, h, zMin - eave);
            const Vector3 rl(b.x0 - 0.05f, h + rise, ridgeZ), rr(b.x1 + 0.05f, h + rise, ridgeZ);
            roof.addQuad(fl, fr, rr, rl, 1.0f);   // +Z slope
            roof.addQuad(br, bl, rl, rr, 1.0f);   // -Z slope
            // Gables in the wall material.
            wall.addTriangleFace(Vector3(b.x1, h, zMax), Vector3(b.x1, h, zMin), Vector3(b.x1, h + rise, ridgeZ), 2.0f);
            wall.addTriangleFace(Vector3(b.x0, h, zMin), Vector3(b.x0, h, zMax), Vector3(b.x0, h + rise, ridgeZ), 2.0f);
            // A chimney stack near the ridge.
            const float cx = b.x0 + 1.0f + dice.range(0.0f, std::max(0.5f, width - 2.0f));
            chimneys.at(bx).addBoxWorldUv(Vector3(cx - 0.45f, h + rise * 0.5f, ridgeZ - 0.4f), Vector3(cx + 0.45f, h + rise + 0.8f, ridgeZ + 0.4f), 1.0f, kAllFaces & ~kFaceNegY);
        }
        else
        {
            // Parapet and a flat dark roof.
            wall.addBoxWorldUv(Vector3(b.x0, h, zMin), Vector3(b.x1, h + 0.5f, zMax), 2.0f, kAllFaces & ~kFaceNegY);
            roofsSlate.at(bx).addFloor(b.x0 + 0.25f, zMin + 0.25f, b.x1 - 0.25f, zMax - 0.25f, h + 0.1f, 1.0f, true);
        }
    };

    // The terrace opposite: a continuous row with small gaps.
    {
        float x = -streetHalf;
        int lastMaterial = -1;
        while (x < streetHalf)
        {
            Building b;
            b.x0 = x;
            b.x1 = x + dice.range(5.5f, 9.5f);
            b.depth = dice.range(9.0f, 12.0f);
            b.storeys = 2 + dice.pick(3);
            b.storeyHeight = dice.range(2.9f, 3.3f);
            do { b.material = dice.pick(5); } while (b.material == lastMaterial);
            lastMaterial = b.material;
            b.pitched = dice.next() < 0.75f;
            b.doorLeft = dice.next() < 0.5f;
            addBuilding(b, farPavementZ, 1.0f);
            x = b.x1 + (dice.next() < 0.3f ? dice.range(1.5f, 4.0f) : 0.0f);
        }
    }
    // Neighbours either side of our house, sharing its front plane.
    {
        Building left{-16.5f, -L.halfWidth - L.exteriorWallThickness - 1.6f, 11.0f, 2, 3.0f, 3, true, false, 0.0f};
        addBuilding(left, wallFace, -1.0f);
        Building right{L.halfWidth + L.exteriorWallThickness + 1.6f, 17.0f, 11.0f, 2, 3.0f, 0, true, true, 0.0f};
        addBuilding(right, wallFace, -1.0f);
        // Our own house above and around the room: the storey above and the roof.
        const float x0 = -L.halfWidth - L.exteriorWallThickness, x1 = L.halfWidth + L.interiorWallThickness;
        facades[2].at(0.0f).addBoxWorldUv(Vector3(x0, L.ceilingHeight, wallFace), Vector3(x1, L.ceilingHeight + 3.2f, L.halfDepth + 6.0f), 2.0f, kAllFaces & ~kFaceNegY);
        // The room's own front face, as pieces around the two openings (piers,
        // spandrels, lintels). Until M9 this was one skin across the whole
        // front, windows included: from inside nothing showed, but as a
        // shadow caster it kept every ray of direct sun out of the room, and
        // what read as sun on the floor was leakage through the wall's base.
        MeshBuilder& front = facades[2].at(0.0f);
        const float yBottom = -L.floorThickness, yTop = L.ceilingHeight + 0.01f;
        const float zFront = wallFace - 0.001f;
        float cursor = x0;
        for (int w = 0; w < L.windowCount; ++w)
        {
            const float wx0 = L.windowCentreX[w] - L.windowWidth * 0.5f, wx1 = L.windowCentreX[w] + L.windowWidth * 0.5f;
            front.addBoxWorldUv(Vector3(cursor, yBottom, zFront), Vector3(wx0, yTop, wallFace), 2.0f, kFaceNegZ);
            front.addBoxWorldUv(Vector3(wx0, yBottom, zFront), Vector3(wx1, L.windowSillHeight, wallFace), 2.0f, kFaceNegZ);
            front.addBoxWorldUv(Vector3(wx0, L.windowTop(), zFront), Vector3(wx1, yTop, wallFace), 2.0f, kFaceNegZ);
            cursor = wx1;
        }
        front.addBoxWorldUv(Vector3(cursor, yBottom, zFront), Vector3(x1, yTop, wallFace), 2.0f, kFaceNegZ);
    }
    // Street furniture where the room can see it, across the road (the hedge
    // hides the near pavement from inside): a bench and a litter bin on the
    // far pavement, a bicycle leaning on the terrace opposite. Tubes are
    // cylinders built along Y and turned onto their segment.
    {
        ChunkSet metal, wood, rubber, stone;
        const auto tube = [&](MeshBuilder& into, const Vector3& a, const Vector3& b, float radius, int segments) {
            Vector3 d = b - a;
            const float length = d.Length();
            if (length < 1e-4f) return;
            d.Normalize();
            MeshBuilder t;
            t.addCylinder(Vector3::Zero, radius, length, segments, 0.3f, true);
            Vector3 axis = Vector3::Cross(Vector3::Up, d);
            Matrix rotation = Matrix::getIdentityProperty();
            if (axis.LengthSquared() > 1e-8f)
            {
                axis.Normalize();
                rotation = Matrix::CreateFromAxisAngle(axis, std::acos(std::clamp(Vector3::Dot(Vector3::Up, d), -1.0f, 1.0f)));
            }
            else if (d.Y < 0.0f)
                rotation = Matrix::CreateRotationX(MathHelper::Pi);
            t.transform(rotation * Matrix::CreateTranslation(a));
            MeshData data = t.take();
            MeshBuilder::computeTangents(data);
            const std::uint32_t base = static_cast<std::uint32_t>(into.mesh().vertices.size());
            for (const auto& v : data.vertices) into.mesh().vertices.push_back(v);
            for (std::uint32_t i : data.indices) into.mesh().indices.push_back(base + i);
        };
        // The bench: two cast ends, five slats for the seat and three for the
        // back, which is on the house side (-Z) so it faces the road.
        {
            const float bx = -4.5f, bz = farKerbZ - 1.4f;
            MeshBuilder& ends = stone.at(bx);
            for (float sx : {bx - 0.8f, bx + 0.8f})
            {
                ends.addBoxWorldUv(Vector3(sx - 0.04f, ground, bz - 0.25f), Vector3(sx + 0.04f, ground + 0.44f, bz + 0.25f), 0.5f, kAllFaces & ~kFaceNegY);
                ends.addBoxWorldUv(Vector3(sx - 0.04f, ground + 0.44f, bz - 0.25f), Vector3(sx + 0.04f, ground + 0.92f, bz - 0.17f), 0.5f, kAllFaces);
            }
            MeshBuilder& slats = wood.at(bx);
            for (int i = 0; i < 5; ++i)
            {
                const float z0 = bz - 0.17f + static_cast<float>(i) * 0.10f;
                slats.addBoxWorldUv(Vector3(bx - 0.85f, ground + 0.44f, z0), Vector3(bx + 0.85f, ground + 0.48f, z0 + 0.08f), 0.5f, kAllFaces);
            }
            for (int i = 0; i < 3; ++i)
            {
                const float y0 = ground + 0.56f + static_cast<float>(i) * 0.12f;
                slats.addBoxWorldUv(Vector3(bx - 0.85f, y0, bz - 0.23f), Vector3(bx + 0.85f, y0 + 0.08f, bz - 0.19f), 0.5f, kAllFaces);
            }
        }
        // The litter bin at the far kerb: a drum with a lid ring and a slot hood.
        {
            const float bx = 8.2f, bz = farKerbZ - 0.55f;
            MeshBuilder& drum = metal.at(bx);
            drum.addCylinder(Vector3(bx, ground, bz), 0.21f, 0.85f, 18, 0.5f, true);
            drum.addCylinder(Vector3(bx, ground + 0.85f, bz), 0.23f, 0.05f, 18, 0.5f, true);
            drum.addCylinder(Vector3(bx, ground + 0.90f, bz), 0.16f, 0.12f, 14, 0.5f, true);
        }
        // The bicycle: leaning on the terrace opposite, wheels along the street.
        {
            const float bx = 6.3f, bz = farPavementZ + 0.30f;
            const float lean = MathHelper::ToRadians(-11.0f);   // toward the wall (-Z)
            MeshBuilder& frame = metal.at(bx);
            MeshBuilder& tyres = rubber.at(bx);
            MeshBuilder bike, wheels;
            const float R = 0.34f;
            const Vector3 rear(-0.52f, R, 0.0f), front(0.52f, R, 0.0f), bb(-0.02f, 0.30f, 0.0f);
            const Vector3 seat(-0.20f, 0.92f, 0.0f), head(0.36f, 0.86f, 0.0f), headBottom(0.44f, 0.62f, 0.0f);
            tube(bike, bb, seat, 0.016f, 8);          // seat tube
            tube(bike, bb, headBottom, 0.016f, 8);    // down tube
            tube(bike, seat, head, 0.016f, 8);        // top tube
            tube(bike, bb, rear, 0.010f, 6);          // chain stay
            tube(bike, seat, rear, 0.010f, 6);        // seat stay
            tube(bike, head, front, 0.012f, 6);       // fork
            tube(bike, seat, seat + Vector3(0.0f, 0.10f, 0.0f), 0.012f, 6);   // seat post
            tube(bike, head, head + Vector3(0.0f, 0.08f, 0.0f), 0.014f, 6);   // stem
            tube(bike, head + Vector3(0.0f, 0.08f, -0.24f), head + Vector3(0.0f, 0.08f, 0.24f), 0.011f, 6);   // handlebar
            bike.addBox(Vector3(-0.30f, 1.00f, -0.05f), Vector3(-0.08f, 1.05f, 0.05f), 0.3f);   // saddle
            for (const Vector3& hub : {rear, front})
            {
                bike.addCylinder(hub + Vector3(0.0f, 0.0f, -0.03f), 0.03f, 0.06f, 8, 0.3f, true);
                for (int k = 0; k < 8; ++k)
                {
                    const float a = static_cast<float>(k) / 8.0f * MathHelper::TwoPi;
                    tube(bike, hub, hub + Vector3(std::cos(a) * (R - 0.02f), std::sin(a) * (R - 0.02f), 0.0f), 0.003f, 4);
                }
                MeshBuilder ring;   // the torus lies in XZ around Y; turned upright
                ring.addTorus(Vector3::Zero, R, 0.018f, 28, 8, 0.3f);
                ring.transform(Matrix::CreateRotationX(MathHelper::PiOver2) * Matrix::CreateTranslation(hub));
                MeshData data = ring.take();
                MeshBuilder::computeTangents(data);
                const std::uint32_t base = static_cast<std::uint32_t>(wheels.mesh().vertices.size());
                for (const auto& v : data.vertices) wheels.mesh().vertices.push_back(v);
                for (std::uint32_t i : data.indices) wheels.mesh().indices.push_back(base + i);
            }
            // The hub cylinders were built along Y; they read as axle caps either way.
            const Matrix pose = Matrix::CreateRotationX(-lean) * Matrix::CreateTranslation(bx, ground, bz);
            bike.transform(pose);
            wheels.transform(pose);
            for (MeshBuilder* src : {&bike})
            {
                MeshData data = src->take();
                MeshBuilder::computeTangents(data);
                const std::uint32_t base = static_cast<std::uint32_t>(frame.mesh().vertices.size());
                for (const auto& v : data.vertices) frame.mesh().vertices.push_back(v);
                for (std::uint32_t i : data.indices) frame.mesh().indices.push_back(base + i);
            }
            {
                MeshData data = wheels.take();
                MeshBuilder::computeTangents(data);
                const std::uint32_t base = static_cast<std::uint32_t>(tyres.mesh().vertices.size());
                for (const auto& v : data.vertices) tyres.mesh().vertices.push_back(v);
                for (std::uint32_t i : data.indices) tyres.mesh().indices.push_back(base + i);
            }
        }
        // Parked cars along the kerbs: a body and a cabin of tinted glass on
        // four wheels, bumpers and lamps; the near one shows its roof over
        // the hedge. A hatchback from boxes and quads reads as a car at the
        // road's distance; the paint's gloss and the glass do the rest.
        ChunkSet paintBlue, paintSilver, paintRed, glass, chrome;
        const auto addCar = [&](float cx, float cz, float heading, ChunkSet& paint) {
            // Local: x along the car (nose at +x), z across, y up; heading turns it about Y.
            MeshBuilder body, cabin, trim, wheels, chromeParts;
            const float L = 4.25f, W = 1.78f;
            body.addBox(Vector3(-L * 0.5f, 0.32f, -W * 0.5f), Vector3(L * 0.5f, 0.78f, W * 0.5f), 1.0f, kAllFaces & ~kFaceNegY);
            body.addBox(Vector3(-L * 0.5f + 0.15f, 0.78f, -W * 0.5f + 0.04f), Vector3(L * 0.5f - 0.25f, 0.92f, W * 0.5f - 0.04f), 1.0f, kAllFaces & ~kFaceNegY);   // the waistline
            // The cabin: a trapezoid of glass over a painted roof.
            const float hw = W * 0.5f - 0.06f, rw = W * 0.5f - 0.18f;
            const Vector3 fb(0.95f, 0.92f, 0.0f), rb(-1.55f, 0.92f, 0.0f);      // front and rear at the waist
            const Vector3 fr(0.30f, 1.42f, 0.0f), rr(-1.15f, 1.42f, 0.0f);      // front and rear of the roof
            body.addQuad(Vector3(rr.X, rr.Y, -rw), Vector3(rr.X, rr.Y, rw), Vector3(fr.X, fr.Y, rw), Vector3(fr.X, fr.Y, -rw), 1.0f);   // roof
            cabin.addQuad(Vector3(fb.X, fb.Y, -hw), Vector3(fb.X, fb.Y, hw), Vector3(fr.X, fr.Y, rw), Vector3(fr.X, fr.Y, -rw), 1.0f);   // windscreen
            cabin.addQuad(Vector3(rb.X, rb.Y, hw), Vector3(rb.X, rb.Y, -hw), Vector3(rr.X, rr.Y, -rw), Vector3(rr.X, rr.Y, rw), 1.0f);   // rear glass
            cabin.addQuad(Vector3(rb.X, rb.Y, hw), Vector3(rr.X, rr.Y, rw), Vector3(fr.X, fr.Y, rw), Vector3(fb.X, fb.Y, hw), 1.0f);     // +z side
            cabin.addQuad(Vector3(fb.X, fb.Y, -hw), Vector3(fr.X, fr.Y, -rw), Vector3(rr.X, rr.Y, -rw), Vector3(rb.X, rb.Y, -hw), 1.0f); // -z side
            // Bumpers, lamps, mirrors, a grille.
            trim.addBox(Vector3(L * 0.5f - 0.02f, 0.36f, -W * 0.5f - 0.02f), Vector3(L * 0.5f + 0.08f, 0.56f, W * 0.5f + 0.02f), 0.5f, kAllFaces);
            trim.addBox(Vector3(-L * 0.5f - 0.08f, 0.36f, -W * 0.5f - 0.02f), Vector3(-L * 0.5f + 0.02f, 0.56f, W * 0.5f + 0.02f), 0.5f, kAllFaces);
            trim.addBox(Vector3(L * 0.5f - 0.02f, 0.58f, -0.35f), Vector3(L * 0.5f + 0.01f, 0.72f, 0.35f), 0.5f, kAllFaces);   // grille
            for (float side : {-1.0f, 1.0f})
            {
                chromeParts.addBox(Vector3(L * 0.5f - 0.02f, 0.60f, side * (W * 0.5f - 0.36f) - 0.16f), Vector3(L * 0.5f + 0.015f, 0.74f, side * (W * 0.5f - 0.36f) + 0.16f), 0.5f, kAllFaces);   // headlamp
                trim.addBox(Vector3(-L * 0.5f - 0.015f, 0.60f, side * (W * 0.5f - 0.30f) - 0.14f), Vector3(-L * 0.5f + 0.02f, 0.74f, side * (W * 0.5f - 0.30f) + 0.14f), 0.5f, kAllFaces);   // tail lamp
                trim.addBox(Vector3(0.75f, 1.02f, side * (W * 0.5f + 0.02f) - 0.04f), Vector3(0.95f, 1.12f, side * (W * 0.5f + 0.16f)), 0.5f, kAllFaces);   // mirror
                for (float ax : {-1.35f, 1.35f})
                {
                    MeshBuilder wheel;
                    wheel.addCylinder(Vector3::Zero, 0.32f, 0.20f, 18, 0.5f, true);
                    // Turned onto Z, a cylinder spans z0..z0+0.20: inside the arch on either side.
                    wheel.transform(Matrix::CreateRotationX(MathHelper::PiOver2) * Matrix::CreateTranslation(ax, 0.32f, side > 0.0f ? W * 0.5f - 0.22f : -W * 0.5f + 0.02f));
                    MeshData wd = wheel.take();
                    MeshBuilder::computeTangents(wd);
                    const std::uint32_t base = static_cast<std::uint32_t>(wheels.mesh().vertices.size());
                    for (const auto& v : wd.vertices) wheels.mesh().vertices.push_back(v);
                    for (std::uint32_t i : wd.indices) wheels.mesh().indices.push_back(base + i);
                    MeshBuilder cap;
                    cap.addCylinder(Vector3::Zero, 0.19f, 0.03f, 14, 0.5f, true);
                    cap.transform(Matrix::CreateRotationX(MathHelper::PiOver2) * Matrix::CreateTranslation(ax, 0.32f, side > 0.0f ? W * 0.5f - 0.02f : -W * 0.5f - 0.01f));
                    MeshData cd = cap.take();
                    MeshBuilder::computeTangents(cd);
                    const std::uint32_t cbase = static_cast<std::uint32_t>(chromeParts.mesh().vertices.size());
                    for (const auto& v : cd.vertices) chromeParts.mesh().vertices.push_back(v);
                    for (std::uint32_t i : cd.indices) chromeParts.mesh().indices.push_back(cbase + i);
                }
            }
            const Matrix pose = Matrix::CreateRotationY(heading) * Matrix::CreateTranslation(cx, ground, cz);
            const auto pour = [&](MeshBuilder& from, ChunkSet& set) {
                from.transform(pose);
                MeshData data = from.take();
                MeshBuilder::computeTangents(data);
                MeshBuilder& into = set.at(cx);
                const std::uint32_t base = static_cast<std::uint32_t>(into.mesh().vertices.size());
                for (const auto& v : data.vertices) into.mesh().vertices.push_back(v);
                for (std::uint32_t i : data.indices) into.mesh().indices.push_back(base + i);
            };
            pour(body, paint);
            pour(cabin, glass);
            pour(trim, rubber);
            pour(wheels, rubber);
            pour(chromeParts, chrome);
        };
        addCar(-11.5f, farKerbZ + 1.15f, 0.0f, paintBlue);                   // across the road, nose along +x
        addCar(15.0f, farKerbZ + 1.15f, MathHelper::Pi, paintSilver);       // across the road, nose along -x
        addCar(5.2f, kerbZ - 1.15f, MathHelper::Pi, paintRed);              // at our kerb, its roof over the hedge
        placeChunks(paintBlue, "car_paint_blue", "exterior_cars_blue", true);
        placeChunks(paintSilver, "car_paint_silver", "exterior_cars_silver", true);
        placeChunks(paintRed, "car_paint_red", "exterior_cars_red", true);
        placeChunks(glass, "car_glass", "exterior_car_glass", true);
        placeChunks(chrome, "car_chrome", "exterior_car_chrome", true);
        placeChunks(metal, "gutter_metal", "exterior_furniture_metal", true);
        placeChunks(wood, "door_dark", "exterior_furniture_wood", true);
        placeChunks(rubber, "clock_black", "exterior_furniture_rubber", true);
        placeChunks(stone, "concrete", "exterior_furniture_stone", true);
    }
    // Distant blocks behind the terrace and a taller skyline further out.
    {
        MeshBuilder skyline;
        for (int i = 0; i < 26; ++i)
        {
            const float w = dice.range(14.0f, 30.0f);
            const float x = -190.0f + static_cast<float>(i) * 15.0f + dice.range(-4.0f, 4.0f);
            const float z = -75.0f - dice.range(0.0f, 70.0f);
            const float h = dice.range(14.0f, 48.0f);
            skyline.addBoxWorldUv(Vector3(x, ground, z - dice.range(12.0f, 25.0f)), Vector3(x + w, h, z), 4.0f, kAllFaces & ~kFaceNegY);
        }
        // A second street row behind the terrace (roofs visible above it).
        for (float x = -streetHalf; x < streetHalf; x += 9.0f)
        {
            Building b{x, x + 8.5f, 10.0f, 2 + dice.pick(3), 3.0f, dice.pick(5), true, false, 0.0f};
            addBuilding(b, farPavementZ - 26.0f, 1.0f);
        }
        place(skyline.take(), "facade_distant", "exterior_skyline", Matrix::getIdentityProperty(), false, true);
    }
    for (int i = 0; i < 5; ++i)
        placeChunks(facades[i], facadeMaterials[i], std::string("exterior_facades_") + facadeMaterials[i], true);
    placeChunks(frames, "paint_white_trim", "exterior_window_frames", false);
    placeChunks(glassDark, "facade_window", "exterior_windows_dark", false);
    placeChunks(glassLit, "facade_window_lit", "exterior_windows_lit", false);
    placeChunks(glassLitDim, "facade_window_lit_dim", "exterior_windows_lit_dim", false);
    placeChunks(glassLitCool, "facade_window_lit_cool", "exterior_windows_lit_cool", false);
    placeChunks(roofsTile, "roof_tiles", "exterior_roofs_tile", true);
    placeChunks(roofsSlate, "roof_slate", "exterior_roofs_slate", true);
    placeChunks(doors, "door_dark", "exterior_doors", false);
    placeChunks(chimneys, "brick_red", "exterior_chimneys", true);
    placeChunks(sills, "concrete", "exterior_sills", true);
    placeChunks(plinths, "concrete", "exterior_plinths", true);
    placeChunks(gutters, "gutter_metal", "exterior_gutters", true);

    // ---- trees ----
    {
        ChunkSet trunkSet, canopySet, canopyShadowSet;
        struct TreeSpot { float x, z, height, spread; };
        const TreeSpot spots[] = {
            {-4.6f, wallFace - 2.2f, 6.5f, 2.6f},   // neighbour's front garden, left
            {0.6f, kerbZ + 1.0f, 7.5f, 3.0f},       // street tree between the two windows
            {-11.0f, kerbZ + 1.0f, 7.0f, 2.8f},
            {12.5f, kerbZ + 1.0f, 8.0f, 3.1f},
            {-7.0f, farKerbZ - 1.2f, 6.5f, 2.6f},
            {6.5f, farKerbZ - 1.2f, 7.0f, 2.9f},
            {21.0f, farKerbZ - 1.2f, 7.5f, 3.0f},
            {-22.0f, farKerbZ - 1.2f, 7.0f, 2.8f},
            {24.0f, kerbZ + 1.0f, 7.0f, 2.8f},
            {-24.0f, kerbZ + 1.0f, 6.0f, 2.4f},
        };
        int index = 0;
        for (const TreeSpot& t : spots)
        {
            Dice local(1000u + static_cast<std::uint32_t>(index++));
            MeshBuilder& trunks = trunkSet.at(t.x);
            MeshBuilder& canopies = canopySet.at(t.x);
            MeshBuilder& canopyShadows = canopyShadowSet.at(t.x);
            const float trunkH = t.height * 0.42f;
            trunks.addCylinder(Vector3(t.x, ground - 0.05f, t.z), 0.20f, trunkH * 0.55f, 10, 1.0f, false);
            trunks.addCylinder(Vector3(t.x, ground + trunkH * 0.55f - 0.02f, t.z), 0.15f, trunkH * 0.5f, 10, 1.0f, true);
            // Boughs: three leaning cylinders into the canopy.
            for (int b = 0; b < 3; ++b)
            {
                const float angle = local.range(0.0f, MathHelper::TwoPi);
                MeshBuilder bough;
                bough.addCylinder(Vector3::Zero, 0.09f, t.spread * 0.9f, 7, 1.0f, true);
                const Matrix m = Matrix::CreateRotationX(MathHelper::ToRadians(38.0f)) * Matrix::CreateRotationY(angle)
                                 * Matrix::CreateTranslation(t.x, ground + trunkH * 0.85f, t.z);
                bough.transform(m);
                MeshData data = bough.take();
                MeshBuilder::computeTangents(data);
                for (const auto& v : data.vertices) trunks.mesh().vertices.push_back(v);
                const std::uint32_t base = static_cast<std::uint32_t>(trunks.mesh().vertices.size() - data.vertices.size());
                for (std::uint32_t i : data.indices) trunks.mesh().indices.push_back(base + i);
            }
            // Canopy: a cluster of leaf spheres, masked by the foliage alpha.
            const Vector3 centre(t.x, ground + trunkH + t.spread * 0.55f, t.z);
            const int blobs = 9;
            const std::size_t canopyStart = canopies.mesh().vertices.size();
            for (int b = 0; b < blobs; ++b)
            {
                const float a = local.range(0.0f, MathHelper::TwoPi);
                const float r = b == 0 ? 0.0f : local.range(0.35f, 0.75f) * t.spread;
                const float y = b == 0 ? 0.2f : local.range(-0.35f, 0.45f);
                const Vector3 c = centre + Vector3(std::cos(a) * r, y * t.spread, std::sin(a) * r);
                const float radius = (b == 0 ? 0.62f : local.range(0.42f, 0.6f)) * t.spread;
                canopies.addSphere(c, radius, 14, 9, 1.0f);
            }
            // The blobs' normals bend toward the crown's own (from its centre,
            // squashed so the underside reads as beneath): the canopy then
            // shades as one volume, lit on top and dark below, instead of nine
            // balls each with its own highlight.
            for (std::size_t i = canopyStart; i < canopies.mesh().vertices.size(); ++i)
            {
                auto& v = canopies.mesh().vertices[i];
                Vector3 crown = v.Position - centre;
                crown.Y *= 1.4f;
                if (crown.LengthSquared() > 1e-6f) crown.Normalize();
                Vector3 n = v.Normal * 0.3f + crown * 0.7f;
                if (n.LengthSquared() > 1e-6f) n.Normalize();
                v.Normal = n;
            }
            // The shadow is cast by a few leaf clusters instead of the blobs
            // (the caster effect has no alpha test, R-11): about half the
            // canopy's silhouette, so sun through it reaches the ground and
            // the room dappled rather than not at all.
            const int clusters = 7;
            for (int k = 0; k < clusters; ++k)
            {
                const float ca = local.range(0.0f, MathHelper::TwoPi);
                const float cr = local.range(0.1f, 0.6f) * t.spread;
                const float cy = local.range(-0.3f, 0.4f) * t.spread;
                const Vector3 cc = centre + Vector3(std::cos(ca) * cr, cy, std::sin(ca) * cr);
                canopyShadows.addSphere(cc, local.range(0.17f, 0.23f) * t.spread, 8, 5, 1.0f);
            }
        }
        placeChunks(trunkSet, "bark", "exterior_tree_trunks", true);
        placeChunks(canopySet, "foliage", "exterior_tree_canopies", false);
        // CNA_ROOM_NO_TREE_SHADOWS=1 leaves the crowns' shadow proxies out (a
        // diagnostic: clear sun through the windows to test what depends on it).
        if (std::getenv("CNA_ROOM_NO_TREE_SHADOWS") == nullptr)
            placeChunks(canopyShadowSet, "bark", "exterior_tree_canopy_shadows", true, true);
        else
            canopyShadowSet.chunks.clear();
    }

    // ---- street lights ----
    {
        ChunkSet postSet, headSet;
        streetLampPositions_.clear();
        const float lampX[] = {-15.5f, -3.4f, 8.7f, 20.8f};
        for (float x : lampX)
        {
            // Near side: on the pavement 0.5 m from the kerb, arm reaching over the road.
            const float z = kerbZ + 0.5f;
            MeshBuilder& posts = postSet.at(x);
            MeshBuilder& heads = headSet.at(x);
            posts.addCylinder(Vector3(x, ground, z), 0.09f, 6.2f, 10, 1.0f, false);
            posts.addCylinder(Vector3(x, ground, z), 0.16f, 0.9f, 10, 1.0f, true);
            MeshBuilder arm;
            arm.addCylinder(Vector3::Zero, 0.05f, 1.6f, 8, 1.0f, true);
            arm.transform(Matrix::CreateRotationX(MathHelper::ToRadians(-105.0f)) * Matrix::CreateTranslation(x, ground + 6.0f, z));
            MeshData data = arm.take();
            const std::uint32_t base = static_cast<std::uint32_t>(posts.mesh().vertices.size());
            for (const auto& v : data.vertices) posts.mesh().vertices.push_back(v);
            for (std::uint32_t i : data.indices) posts.mesh().indices.push_back(base + i);
            const Vector3 head(x, ground + 6.35f, z - 1.45f);
            posts.addBox(head + Vector3(-0.32f, -0.02f, -0.22f), head + Vector3(0.32f, 0.16f, 0.22f), 1.0f);
            heads.addBox(head + Vector3(-0.26f, -0.10f, -0.17f), head + Vector3(0.26f, -0.02f, 0.17f), 1.0f);
            streetLampPositions_.push_back(head + Vector3(0.0f, -0.15f, 0.0f));
        }
        const float farX[] = {-9.5f, 2.6f, 14.7f};
        for (float x : farX)
        {
            const float z = farKerbZ - 0.5f;
            MeshBuilder& posts = postSet.at(x);
            MeshBuilder& heads = headSet.at(x);
            posts.addCylinder(Vector3(x, ground, z), 0.09f, 6.2f, 10, 1.0f, false);
            posts.addCylinder(Vector3(x, ground, z), 0.16f, 0.9f, 10, 1.0f, true);
            MeshBuilder arm;
            arm.addCylinder(Vector3::Zero, 0.05f, 1.6f, 8, 1.0f, true);
            arm.transform(Matrix::CreateRotationX(MathHelper::ToRadians(105.0f)) * Matrix::CreateTranslation(x, ground + 6.0f, z));
            MeshData data = arm.take();
            const std::uint32_t base = static_cast<std::uint32_t>(posts.mesh().vertices.size());
            for (const auto& v : data.vertices) posts.mesh().vertices.push_back(v);
            for (std::uint32_t i : data.indices) posts.mesh().indices.push_back(base + i);
            const Vector3 head(x, ground + 6.35f, z + 1.45f);
            posts.addBox(head + Vector3(-0.32f, -0.02f, -0.22f), head + Vector3(0.32f, 0.16f, 0.22f), 1.0f);
            heads.addBox(head + Vector3(-0.26f, -0.10f, -0.17f), head + Vector3(0.26f, -0.02f, 0.17f), 1.0f);
            streetLampPositions_.push_back(head + Vector3(0.0f, -0.15f, 0.0f));
        }
        placeChunks(postSet, "metal_paint_dark", "exterior_lamp_posts", true);
        placeChunks(headSet, "street_lamp_head", "exterior_lamp_heads", false);
    }

    // Slab under the room so the floor has an exterior face.
    MeshBuilder slab;
    slab.addBoxWorldUv(Vector3(-L.halfWidth - L.exteriorWallThickness, -L.floorThickness, -L.halfDepth - L.exteriorWallThickness),
                       Vector3(L.halfWidth + L.interiorWallThickness, 0.0f, L.halfDepth + L.interiorWallThickness), 3.0f, kAllFaces & ~kFacePosY);
    place(slab.take(), "concrete", "slab", Matrix::getIdentityProperty(), true, true);
}

}  // namespace CnaRoom
