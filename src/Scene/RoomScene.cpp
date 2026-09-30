// SPDX-License-Identifier: MIT
#include "CnaRoom/Scene/RoomScene.hpp"

#include "CnaRoom/Assets/ModelLibrary.hpp"
#include "CnaRoom/Geometry/MeshBuilder.hpp"
#include "CnaRoom/Render/GpuMesh.hpp"
#include "CnaRoom/Render/Material.hpp"
#include "CnaRoom/Render/MaterialLibrary.hpp"
#include "CnaRoom/Render/PlanarReflection.hpp"
#include "CnaRoom/Render/SceneRenderer.hpp"
#include "CnaRoom/Sim/WeatherSystem.hpp"

#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include "CNA/Logger.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <utility>

using namespace Microsoft::Xna::Framework;
using CnaRoom::Geometry::MeshBuilder;
using CnaRoom::Geometry::MeshData;
using Microsoft::Xna::Framework::Graphics::Texture2D;

namespace CnaRoom {

namespace {

constexpr unsigned kFacePosX = 1u << 0, kFaceNegX = 1u << 1, kFacePosY = 1u << 2,
                   kFaceNegY = 1u << 3, kFacePosZ = 1u << 4, kFaceNegZ = 1u << 5;
constexpr unsigned kAllFaces = 0x3F;

}  // namespace

RoomScene::RoomScene(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
                     MaterialLibrary& materials, ModelLibrary& models, SceneRenderer& renderer,
                     std::string extractedAssetDirectory)
    : device_(device), materials_(materials), models_(models), renderer_(renderer),
      extractedDirectory_(std::move(extractedAssetDirectory))
{
}

const ImportedModel* RoomScene::placeModel(const std::string& model, const Vector3& position,
                                           float yawDegrees, float fitHeight, bool castsShadow,
                                           const ModelLibrary::MaterialTweak& tweak)
{
    const ImportedModel* imported = models_.load(model, extractedDirectory_ + "/" + model + ".glb", tweak);
    if (imported == nullptr) return nullptr;
    models_.place(renderer_, *imported,
                  ModelLibrary::placement(*imported, position, MathHelper::ToRadians(yawDegrees), fitHeight),
                  model, castsShadow);
    ++placedModels_;
    return imported;
}

RoomScene::~RoomScene() = default;

const Viewpoint* RoomScene::viewpoint(const std::string& name) const
{
    for (const Viewpoint& v : viewpoints_)
        if (v.name == name) return &v;
    return nullptr;
}

std::size_t RoomScene::geometryBytes() const
{
    std::size_t total = 0;
    for (const auto& mesh : meshes_) total += mesh->gpuBytes();
    return total;
}

std::vector<Vector3> RoomScene::probePositions() const
{
    // Three rows of three: toward the windows, the middle, toward the
    // television wall, so the sections of the floor and ceiling nearest the
    // glass take a probe that sees the windows large and the far ones a dimmer
    // one, in steps small enough for the seams to pass.
    std::vector<Vector3> probes;
    for (float z : {-1.5f, 0.0f, 1.5f})
        for (float x : {-1.9f, 0.0f, 1.9f})
            probes.emplace_back(x, x == 0.0f ? 1.4f : 1.3f, z);
    return probes;
}

const GpuMesh* RoomScene::place(MeshData mesh, const std::string& materialName,
                                const std::string& name, const Matrix& world, bool castsShadow,
                                bool exterior, bool shadowOnly)
{
    if (mesh.empty()) return nullptr;
    meshes_.push_back(std::make_unique<GpuMesh>(device_, mesh, name));
    const GpuMesh* gpu = meshes_.back().get();
    SceneItem item;
    item.mesh = gpu;
    item.material = &materials_.get(materialName);
    item.world = world;
    item.castsShadow = castsShadow;
    item.shadowOnly = shadowOnly;
    item.exterior = exterior;
    item.name = name;
    renderer_.addItem(std::move(item));
    return gpu;
}

void RoomScene::build()
{
    buildFloorAndCeiling();
    buildWalls();
    buildWindows();
    buildDoor();
    buildHallway();
    buildWear();
    buildTrim();
    buildRadiator();
    buildFixtures();
    buildExterior();
    buildBookcase();
    buildFurniture();
    buildLamps();
    buildViewpoints();
    for (const std::string& failure : models_.failures())
        CNA::Logger::Warn("living-room-simulator: model -- " + failure);
    CNA::Logger::Info("living-room-simulator: room built -- " + std::to_string(meshes_.size()) + " procedural meshes, "
                      + std::to_string(geometryBytes() / 1024) + " KB of geometry, " + std::to_string(placedModels_)
                      + " imported placements");
}

void RoomScene::buildFloorAndCeiling()
{
    const RoomLayout& L = layout_;
    const Material& oak = materials_.get("oak_floor");
    // The floor in three by three sections, so each takes the probe nearest
    // to it (one probe lights one item) and the floor's light falls off from
    // the windows inward instead of sitting at one level wall to wall; the
    // planks' grain and the furniture take the seams. The UVs come from world
    // position, so the planks run on across them. The ceiling stays one
    // piece: a step on plain plaster reads as a rendering edge.
    (void)oak;
    for (int ix = 0; ix < 3; ++ix)
        for (int iz = 0; iz < 3; ++iz)
        {
            const float x0 = -L.halfWidth + L.halfWidth * 2.0f * static_cast<float>(ix) / 3.0f, x1 = x0 + L.halfWidth * 2.0f / 3.0f;
            const float z0 = -L.halfDepth + L.halfDepth * 2.0f * static_cast<float>(iz) / 3.0f, z1 = z0 + L.halfDepth * 2.0f / 3.0f;
            const std::string suffix = " " + std::to_string(ix) + "," + std::to_string(iz);
            MeshBuilder floor;
            // Planks run along X (the long axis): rotate the UV by transforming a floor built along Z.
            floor.addFloor(x0, z0, x1, z1, 0.0f, 2.4f, true);
            // Swap UV axes so the plank length runs along X.
            for (auto& v : floor.mesh().vertices) v.TextureCoordinate = Vector2(v.TextureCoordinate.Y, v.TextureCoordinate.X);
            MeshBuilder::computeTangents(floor.mesh());
            place(floor.take(), "oak_floor", "floor" + suffix);
        }
    MeshBuilder ceiling;
    ceiling.addFloor(-L.halfWidth, -L.halfDepth, L.halfWidth, L.halfDepth, L.ceilingHeight, 2.0f, false);
    place(ceiling.take(), "plaster_ceiling", "ceiling");
}

void RoomScene::buildWalls()
{
    const RoomLayout& L = layout_;
    const float H = L.ceilingHeight;
    const float te = L.exteriorWallThickness;
    const float ti = L.interiorWallThickness;
    const float uv = 2.0f;

    // Window wall (-Z): exterior wall with two openings. Built as the pieces
    // around the openings so the reveals are real geometry.
    {
        MeshBuilder wall;
        const float z0 = -L.halfDepth - te, z1 = -L.halfDepth;
        float cursor = -L.halfWidth - te;  // wall runs the full width including corner
        for (int w = 0; w < L.windowCount; ++w)
        {
            const float x0 = L.windowCentreX[w] - L.windowWidth * 0.5f;
            const float x1 = L.windowCentreX[w] + L.windowWidth * 0.5f;
            // Pier left of the opening.
            wall.addBoxWorldUv(Vector3(cursor, 0.0f, z0), Vector3(x0, H, z1), uv, kAllFaces & ~kFaceNegY);
            // Spandrel under the window and lintel above.
            wall.addBoxWorldUv(Vector3(x0, 0.0f, z0), Vector3(x1, L.windowSillHeight, z1), uv, kAllFaces & ~kFaceNegY);
            wall.addBoxWorldUv(Vector3(x0, L.windowTop(), z0), Vector3(x1, H, z1), uv, kAllFaces);
            cursor = x1;
        }
        wall.addBoxWorldUv(Vector3(cursor, 0.0f, z0), Vector3(L.halfWidth + te, H, z1), uv, kAllFaces & ~kFaceNegY);
        place(wall.take(), "plaster_warm_white", "wall_window");
    }
    // TV wall (+Z): interior wall.
    {
        MeshBuilder wall;
        wall.addBoxWorldUv(Vector3(-L.halfWidth - te, 0.0f, L.halfDepth), Vector3(L.halfWidth + te, H, L.halfDepth + ti), uv, kAllFaces & ~kFaceNegY);
        place(wall.take(), "plaster_accent", "wall_tv");
    }
    // Bookcase wall (-X): exterior corner wall.
    {
        MeshBuilder wall;
        wall.addBoxWorldUv(Vector3(-L.halfWidth - te, 0.0f, -L.halfDepth), Vector3(-L.halfWidth, H, L.halfDepth), uv, kAllFaces & ~kFaceNegY);
        place(wall.take(), "plaster_warm_white", "wall_west");
    }
    // Door wall (+X): interior wall with the door opening.
    {
        MeshBuilder wall;
        const float x0 = L.halfWidth, x1 = L.halfWidth + ti;
        const float dz0 = L.doorCentreZ - L.doorWidth * 0.5f, dz1 = L.doorCentreZ + L.doorWidth * 0.5f;
        wall.addBoxWorldUv(Vector3(x0, 0.0f, -L.halfDepth), Vector3(x1, H, dz0), uv, kAllFaces & ~kFaceNegY);
        wall.addBoxWorldUv(Vector3(x0, L.doorHeight, dz0), Vector3(x1, H, dz1), uv, kAllFaces);
        wall.addBoxWorldUv(Vector3(x0, 0.0f, dz1), Vector3(x1, H, L.halfDepth), uv, kAllFaces & ~kFaceNegY);
        place(wall.take(), "plaster_warm_white", "wall_door");
    }
}

void RoomScene::buildWindows()
{
    const RoomLayout& L = layout_;
    const float zInside = -L.halfDepth;
    const float zFrameIn = zInside - L.windowRecessFromInside;
    const float zFrameOut = zFrameIn - L.windowFrameDepth;
    const float fw = L.windowFrameWidth;

    for (int w = 0; w < L.windowCount; ++w)
    {
        const float x0 = L.windowCentreX[w] - L.windowWidth * 0.5f;
        const float x1 = L.windowCentreX[w] + L.windowWidth * 0.5f;
        const float y0 = L.windowSillHeight, y1 = L.windowTop();
        const std::string id = "window" + std::to_string(w);

        MeshBuilder frame;
        // Outer frame: four members.
        frame.addBox(Vector3(x0, y0, zFrameOut), Vector3(x0 + fw, y1, zFrameIn), 0.5f);
        frame.addBox(Vector3(x1 - fw, y0, zFrameOut), Vector3(x1, y1, zFrameIn), 0.5f);
        frame.addBox(Vector3(x0 + fw, y1 - fw, zFrameOut), Vector3(x1 - fw, y1, zFrameIn), 0.5f);
        frame.addBox(Vector3(x0 + fw, y0, zFrameOut), Vector3(x1 - fw, y0 + fw, zFrameIn), 0.5f);
        // Two casements: each with its own thinner frame and a central mullion.
        const float mid = L.windowCentreX[w];
        const float cf = 0.045f;
        const float zc0 = zFrameOut + 0.012f, zc1 = zFrameIn - 0.012f;
        for (int c = 0; c < 2; ++c)
        {
            const float cx0 = c == 0 ? x0 + fw : mid + 0.004f;
            const float cx1 = c == 0 ? mid - 0.004f : x1 - fw;
            frame.addBox(Vector3(cx0, y0 + fw, zc0), Vector3(cx0 + cf, y1 - fw, zc1), 0.5f);
            frame.addBox(Vector3(cx1 - cf, y0 + fw, zc0), Vector3(cx1, y1 - fw, zc1), 0.5f);
            frame.addBox(Vector3(cx0 + cf, y1 - fw - cf, zc0), Vector3(cx1 - cf, y1 - fw, zc1), 0.5f);
            frame.addBox(Vector3(cx0 + cf, y0 + fw, zc0), Vector3(cx1 - cf, y0 + fw + cf, zc1), 0.5f);
            // Horizontal glazing bar at a third of the height.
            const float barY = y0 + (y1 - y0) * 0.62f;
            frame.addBox(Vector3(cx0 + cf, barY - 0.012f, zc0 + 0.01f), Vector3(cx1 - cf, barY + 0.012f, zc1 - 0.01f), 0.5f);
        }
        // Interior sill (board) and exterior sill (sloped stone approximated by a box).
        frame.addBox(Vector3(x0 - 0.04f, y0 - 0.03f, zFrameIn), Vector3(x1 + 0.04f, y0, zInside + 0.04f), 0.5f);
        place(frame.take(), "paint_white_trim", id + "_frame");
        // Handles: a square rose, a neck and a lever hanging closed, on each
        // casement's stile by the mullion, on the room side.
        MeshBuilder handles;
        for (int c = 0; c < 2; ++c)
        {
            const float hx = c == 0 ? mid - 0.004f - cf * 0.5f : mid + 0.004f + cf * 0.5f;
            const float hy = y0 + (y1 - y0) * 0.45f;
            handles.addBox(Vector3(hx - 0.016f, hy - 0.016f, zc1), Vector3(hx + 0.016f, hy + 0.016f, zc1 + 0.006f), 0.3f);
            handles.addBox(Vector3(hx - 0.007f, hy - 0.007f, zc1 + 0.006f), Vector3(hx + 0.007f, hy + 0.007f, zc1 + 0.026f), 0.3f);
            handles.addBox(Vector3(hx - 0.007f, hy - 0.105f, zc1 + 0.018f), Vector3(hx + 0.007f, hy + 0.007f, zc1 + 0.032f), 0.3f);
        }
        place(handles.take(), "sconce_brass", id + "_handles", Matrix::getIdentityProperty(), true);

        MeshBuilder exteriorSill;
        exteriorSill.addBox(Vector3(x0 - 0.03f, y0 - 0.05f, zInside - L.exteriorWallThickness - 0.03f),
                            Vector3(x1 + 0.03f, y0, zFrameOut), 0.5f);
        place(exteriorSill.take(), "concrete", id + "_exterior_sill", Matrix::getIdentityProperty(), true, true);

        // Glass: one pane per casement, centred in the casement frame depth.
        MeshBuilder glass;
        const float zg = (zc0 + zc1) * 0.5f;
        for (int c = 0; c < 2; ++c)
        {
            const float cx0 = (c == 0 ? x0 + fw : mid + 0.004f) + cf;
            const float cx1 = (c == 0 ? mid - 0.004f : x1 - fw) - cf;
            glass.addQuad(Vector3(cx0, y0 + fw + cf, zg), Vector3(cx1, y0 + fw + cf, zg),
                          Vector3(cx1, y1 - fw - cf, zg), Vector3(cx0, y1 - fw - cf, zg), 1.0f);
        }
        place(glass.take(), "window_glass", id + "_glass", Matrix::getIdentityProperty(), false);
        windowGlassZ_ = zg;
        windowGlassX0_ = std::min(windowGlassX0_, x0);
        windowGlassX1_ = std::max(windowGlassX1_, x1);
        windowGlassY0_ = y0;
        windowGlassY1_ = y1;
    }
}

void RoomScene::buildDoor()
{
    const RoomLayout& L = layout_;
    const float x0 = L.halfWidth, x1 = L.halfWidth + L.interiorWallThickness;
    const float z0 = L.doorCentreZ - L.doorWidth * 0.5f, z1 = L.doorCentreZ + L.doorWidth * 0.5f;
    const float fw = L.doorFrameWidth;
    MeshBuilder frame;
    // Architrave on both faces and the lining inside the opening.
    for (int side = 0; side < 2; ++side)
    {
        const float xa = side == 0 ? x0 - 0.018f : x1;
        const float xb = side == 0 ? x0 : x1 + 0.018f;
        frame.addBox(Vector3(xa, 0.0f, z0 - fw), Vector3(xb, L.doorHeight + fw, z0), 0.5f);
        frame.addBox(Vector3(xa, 0.0f, z1), Vector3(xb, L.doorHeight + fw, z1 + fw), 0.5f);
        frame.addBox(Vector3(xa, L.doorHeight, z0), Vector3(xb, L.doorHeight + fw, z1), 0.5f);
    }
    frame.addBox(Vector3(x0, 0.0f, z0), Vector3(x1, L.doorHeight, z0 + 0.03f), 0.5f);
    frame.addBox(Vector3(x0, 0.0f, z1 - 0.03f), Vector3(x1, L.doorHeight, z1), 0.5f);
    frame.addBox(Vector3(x0, L.doorHeight - 0.03f, z0), Vector3(x1, L.doorHeight, z1), 0.5f);
    place(frame.take(), "paint_white_trim", "door_frame");

    // The door leaf: closed, 40 mm thick, with four recessed panels.
    MeshBuilder leaf;
    const float lx0 = x0 + 0.04f, lx1 = lx0 + 0.04f;
    leaf.addBox(Vector3(lx0, 0.01f, z0 + 0.03f), Vector3(lx1, L.doorHeight - 0.03f, z1 - 0.03f), 0.5f);
    for (int row = 0; row < 2; ++row)
        for (int col = 0; col < 2; ++col)
        {
            const float pz0 = z0 + 0.12f + static_cast<float>(col) * 0.42f, pz1 = pz0 + 0.30f;
            const float py0 = 0.15f + static_cast<float>(row) * 1.0f, py1 = py0 + 0.80f;
            // Panel moulding as a raised rim on the room side.
            leaf.addBox(Vector3(lx0 - 0.008f, py0, pz0), Vector3(lx0, py1, pz0 + 0.03f), 0.5f);
            leaf.addBox(Vector3(lx0 - 0.008f, py0, pz1 - 0.03f), Vector3(lx0, py1, pz1), 0.5f);
            leaf.addBox(Vector3(lx0 - 0.008f, py0, pz0 + 0.03f), Vector3(lx0, py0 + 0.03f, pz1 - 0.03f), 0.5f);
            leaf.addBox(Vector3(lx0 - 0.008f, py1 - 0.03f, pz0 + 0.03f), Vector3(lx0, py1, pz1 - 0.03f), 0.5f);
        }
    // Ajar: the leaf and its handle swing 32 degrees into the hall about the
    // hinge at the far jamb, so the views past the door show the hall.
    const Matrix swing = Matrix::CreateTranslation(-(lx0 + 0.02f), 0.0f, -(z1 - 0.03f)) * Matrix::CreateRotationY(MathHelper::ToRadians(-32.0f))
                         * Matrix::CreateTranslation(lx0 + 0.02f, 0.0f, z1 - 0.03f);
    place(leaf.take(), "paint_door", "door_leaf", swing);

    // Handle: a lever on a rose, room side, at 1.05 m.
    MeshBuilder handle;
    const float hz = z0 + 0.09f, hy = 1.05f;
    handle.addCylinder(Vector3(lx0 - 0.004f, hy - 0.028f, hz), 0.028f, 0.004f, 24, 0.2f);
    Matrix lay = Matrix::CreateRotationZ(MathHelper::PiOver2) * Matrix::CreateTranslation(lx0 - 0.004f, hy, hz);
    MeshBuilder spindle;
    spindle.addCylinder(Vector3(0.0f, 0.0f, 0.0f), 0.009f, 0.065f, 16, 0.2f);
    spindle.transform(lay);
    handle.mesh().append(spindle.mesh());
    MeshBuilder lever;
    lever.addCylinder(Vector3(0.0f, 0.0f, 0.0f), 0.009f, 0.12f, 16, 0.2f);
    lever.transform(Matrix::CreateRotationX(-MathHelper::PiOver2) * Matrix::CreateTranslation(lx0 - 0.065f, hy, hz));
    handle.mesh().append(lever.mesh());
    place(handle.take(), "metal_brushed", "door_handle", swing);
}

void RoomScene::buildHallway()
{
    // A hall beyond the door wall: 1.2 m wide along the wall, its own floor,
    // ceiling and far wall, a flush dome light that stays on (no window of
    // its own), a coat on a hook and a pair of shoes by the wall.
    const RoomLayout& L = layout_;
    const float x0 = L.halfWidth + L.interiorWallThickness, x1 = x0 + 1.20f;
    const float z0 = -0.20f, z1 = 2.80f, H = L.ceilingHeight;
    MeshBuilder floor, ceiling, walls;
    floor.addFloor(x0, z0, x1, z1, 0.0f, 1.0f, true);
    ceiling.addFloor(x0, z0, x1, z1, H, 1.0f, false);
    walls.addBox(Vector3(x1, 0.0f, z0), Vector3(x1 + 0.12f, H, z1), 1.0f, kFaceNegX);           // the far wall, facing the door
    walls.addBox(Vector3(x0, 0.0f, z0 - 0.12f), Vector3(x1, H, z0), 1.0f, kFacePosZ);           // the end walls
    walls.addBox(Vector3(x0, 0.0f, z1), Vector3(x1, H, z1 + 0.12f), 1.0f, kFaceNegZ);
    walls.addBox(Vector3(x0 - 0.01f, 0.0f, z0), Vector3(x0, H, z1), 1.0f, kFacePosX);            // the door wall's other face
    place(floor.take(), "oak_floor", "hall_floor");
    place(ceiling.take(), "plaster_ceiling", "hall_ceiling");
    place(walls.take(), "plaster_warm_white", "hall_walls");
    MeshBuilder skirting;
    skirting.addBox(Vector3(x1 - 0.015f, 0.0f, z0), Vector3(x1, 0.10f, z1), 0.5f, kFaceNegX | kFacePosY);
    place(skirting.take(), "paint_white_trim", "hall_skirting");
    // The dome light: a frosted half sphere on the ceiling, always lit.
    MeshBuilder dome;
    dome.addSphere(Vector3((x0 + x1) * 0.5f, H + 0.02f, L.doorCentreZ), 0.13f, 20, 12, 0.3f);
    place(dome.take(), "hall_dome", "hall_dome", Matrix::getIdentityProperty(), false);
    // A coat on a hook: shoulders, body and a collar in navy wool, the hook a brass peg.
    {
        const float cx = x1 - 0.07f, cz = L.doorCentreZ - 0.62f;   // on the side the gap looks at
        MeshBuilder coat;
        coat.addBox(Vector3(cx - 0.05f, 1.42f, cz - 0.21f), Vector3(cx + 0.05f, 1.52f, cz + 0.21f), 0.3f);   // shoulders
        coat.addBox(Vector3(cx - 0.045f, 0.55f, cz - 0.18f), Vector3(cx + 0.045f, 1.44f, cz + 0.18f), 0.3f);   // body
        coat.addBox(Vector3(cx - 0.06f, 1.46f, cz - 0.09f), Vector3(cx + 0.06f, 1.58f, cz + 0.09f), 0.3f);   // collar
        coat.addBox(Vector3(cx - 0.03f, 0.80f, cz - 0.28f), Vector3(cx + 0.035f, 1.40f, cz - 0.20f), 0.3f);   // sleeves
        coat.addBox(Vector3(cx - 0.03f, 0.80f, cz + 0.20f), Vector3(cx + 0.035f, 1.40f, cz + 0.28f), 0.3f);
        place(coat.take(), "coat_wool", "hall_coat");
        MeshBuilder peg;
        peg.addBox(Vector3(x1 - 0.10f, 1.58f, cz - 0.012f), Vector3(x1, 1.604f, cz + 0.012f), 0.2f);
        peg.addBox(Vector3(x1 - 0.02f, 1.50f, cz - 0.03f), Vector3(x1, 1.66f, cz + 0.03f), 0.2f);
        place(peg.take(), "sconce_brass", "hall_peg");
    }
    // Shoes by the far wall, one a little askew.
    {
        MeshBuilder shoes;
        shoes.addBox(Vector3(x1 - 0.30f, 0.0f, L.doorCentreZ - 0.12f), Vector3(x1 - 0.04f, 0.07f, L.doorCentreZ - 0.02f), 0.2f);
        shoes.addBox(Vector3(x1 - 0.30f, 0.0f, L.doorCentreZ - 0.12f), Vector3(x1 - 0.18f, 0.11f, L.doorCentreZ - 0.02f), 0.2f);   // the heel
        place(shoes.take(), "rubber_black", "hall_shoe_a");
        MeshBuilder shoe;
        shoe.addBox(Vector3(-0.13f, 0.0f, -0.05f), Vector3(0.13f, 0.07f, 0.05f), 0.2f);
        shoe.addBox(Vector3(-0.13f, 0.0f, -0.05f), Vector3(-0.01f, 0.11f, 0.05f), 0.2f);
        place(shoe.take(), "rubber_black", "hall_shoe_b", Matrix::CreateRotationY(MathHelper::ToRadians(14.0f)) * Matrix::CreateTranslation(x1 - 0.17f, 0.0f, L.doorCentreZ + 0.08f));
    }
}

void RoomScene::buildWear()
{
    // Wear and stains as decals: scuffs on the boards inside the door, a
    // worn path from the door toward the seating, hand marks on the wall by
    // the door where the hand goes for the switch. Each is a unit box placed
    // over the surface (local +Z projects down into it), black at low opacity.
    const RoomLayout& L = layout_;
    const Matrix onFloor = Matrix::CreateRotationX(MathHelper::PiOver2);   // local +Z down onto the floor
    if (Texture2D* scuffs = materials_.decal("scuffs"))
        renderer_.addDecal(scuffs, Matrix::CreateScale(0.85f, 0.65f, 0.06f) * onFloor * Matrix::CreateRotationY(MathHelper::ToRadians(20.0f))
                                       * Matrix::CreateTranslation(L.halfWidth - 0.55f, 0.0f, L.doorCentreZ + 0.05f), 0.35f);
    if (Texture2D* wear = materials_.decal("wear"))
    {
        const Vector3 from(L.halfWidth - 0.45f, 0.0f, L.doorCentreZ), to(0.95f, 0.0f, 0.10f);
        const Vector3 d = to - from;
        const float yaw = std::atan2(-d.Z, d.X);   // local +X along the path
        renderer_.addDecal(wear, Matrix::CreateScale(d.Length() + 0.6f, 0.95f, 0.06f) * onFloor * Matrix::CreateRotationY(yaw)
                                     * Matrix::CreateTranslation((from + to) * 0.5f), 0.14f);
    }
    if (Texture2D* marks = materials_.decal("marks"))
        renderer_.addDecal(marks, Matrix::CreateScale(0.24f, 0.28f, 0.05f) * Matrix::CreateRotationY(-MathHelper::PiOver2)
                                      * Matrix::CreateTranslation(L.halfWidth, 1.08f, L.doorCentreZ - L.doorWidth * 0.5f - L.doorFrameWidth - 0.15f), 0.18f);
    CNA::Logger::Info("living-room-simulator: " + std::to_string(renderer_.decalCount()) + " wear decals");
}

void RoomScene::buildTrim()
{
    const RoomLayout& L = layout_;
    const float h = L.baseboardHeight, d = L.baseboardDepth;
    MeshBuilder base;
    // -Z wall
    base.addBox(Vector3(-L.halfWidth, 0.0f, -L.halfDepth), Vector3(L.halfWidth, h, -L.halfDepth + d), 0.5f, kAllFaces & ~kFaceNegY & ~kFaceNegZ);
    // +Z wall
    base.addBox(Vector3(-L.halfWidth, 0.0f, L.halfDepth - d), Vector3(L.halfWidth, h, L.halfDepth), 0.5f, kAllFaces & ~kFaceNegY & ~kFacePosZ);
    // -X wall
    base.addBox(Vector3(-L.halfWidth, 0.0f, -L.halfDepth + d), Vector3(-L.halfWidth + d, h, L.halfDepth - d), 0.5f, kAllFaces & ~kFaceNegY & ~kFaceNegX);
    // +X wall, around the door.
    const float z0 = L.doorCentreZ - L.doorWidth * 0.5f - L.doorFrameWidth;
    const float z1 = L.doorCentreZ + L.doorWidth * 0.5f + L.doorFrameWidth;
    base.addBox(Vector3(L.halfWidth - d, 0.0f, -L.halfDepth + d), Vector3(L.halfWidth, h, z0), 0.5f, kAllFaces & ~kFaceNegY & ~kFacePosX);
    base.addBox(Vector3(L.halfWidth - d, 0.0f, z1), Vector3(L.halfWidth, h, L.halfDepth - d), 0.5f, kAllFaces & ~kFaceNegY & ~kFacePosX);
    place(base.take(), "paint_white_trim", "baseboards");

    // A simple cove cornice at the ceiling.
    const float c = L.corniceSize;
    MeshBuilder cornice;
    const float H = L.ceilingHeight;
    cornice.addBox(Vector3(-L.halfWidth, H - c, -L.halfDepth), Vector3(L.halfWidth, H, -L.halfDepth + c), 0.5f, kFacePosZ | kFaceNegY);
    cornice.addBox(Vector3(-L.halfWidth, H - c, L.halfDepth - c), Vector3(L.halfWidth, H, L.halfDepth), 0.5f, kFaceNegZ | kFaceNegY);
    cornice.addBox(Vector3(-L.halfWidth, H - c, -L.halfDepth), Vector3(-L.halfWidth + c, H, L.halfDepth), 0.5f, kFacePosX | kFaceNegY);
    cornice.addBox(Vector3(L.halfWidth - c, H - c, -L.halfDepth), Vector3(L.halfWidth, H, L.halfDepth), 0.5f, kFaceNegX | kFaceNegY);
    place(cornice.take(), "plaster_ceiling", "cornice", Matrix::getIdentityProperty(), false);
}

void RoomScene::buildRadiator()
{
    const RoomLayout& L = layout_;
    // Panel radiator under the right-hand window: 1.2 m x 0.6 m x 0.1 m, 0.12 m off the floor.
    const float cx = L.windowCentreX[1];
    const float x0 = cx - 0.60f, x1 = cx + 0.60f;
    const float y0 = 0.14f, y1 = 0.74f;
    const float z0 = -L.halfDepth + 0.035f, z1 = z0 + 0.10f;
    MeshBuilder rad;
    rad.addBox(Vector3(x0, y0, z0), Vector3(x1, y1, z1), 0.5f);
    // Convector fins on top (grille) and the ripple of the front panel.
    for (int i = 0; i < 36; ++i)
    {
        const float fx = x0 + 0.02f + static_cast<float>(i) * (1.16f / 36.0f);
        rad.addBox(Vector3(fx, y1, z0 + 0.01f), Vector3(fx + 0.012f, y1 + 0.012f, z1 - 0.01f), 0.5f);
        rad.addBox(Vector3(fx, y0 + 0.02f, z1), Vector3(fx + 0.016f, y1 - 0.02f, z1 + 0.006f), 0.5f);
    }
    // Wall brackets and the valve.
    rad.addBox(Vector3(x0 + 0.15f, y0 + 0.1f, -L.halfDepth), Vector3(x0 + 0.19f, y1 - 0.1f, z0), 0.5f);
    rad.addBox(Vector3(x1 - 0.19f, y0 + 0.1f, -L.halfDepth), Vector3(x1 - 0.15f, y1 - 0.1f, z0), 0.5f);
    place(rad.take(), "radiator_enamel", "radiator");
    MeshBuilder valve;
    valve.addCylinder(Vector3(x1 + 0.03f, y0 - 0.02f, (z0 + z1) * 0.5f), 0.012f, 0.16f, 16, 0.2f);
    valve.addCylinder(Vector3(x1 + 0.03f, 0.0f, (z0 + z1) * 0.5f), 0.008f, y0 - 0.02f, 12, 0.2f);
    valve.addCylinder(Vector3(x0 - 0.03f, 0.0f, (z0 + z1) * 0.5f), 0.008f, y0 + 0.02f, 12, 0.2f);
    place(valve.take(), "metal_brushed", "radiator_valve");
}

void RoomScene::buildFixtures()
{
    const RoomLayout& L = layout_;
    // Light switch by the door and sockets along the walls.
    MeshBuilder plates;
    const float sx = L.halfWidth - 0.004f;
    const float sz = L.doorCentreZ - L.doorWidth * 0.5f - L.doorFrameWidth - 0.12f;
    plates.addBox(Vector3(sx - 0.006f, 1.10f, sz - 0.04f), Vector3(sx, 1.18f, sz + 0.04f), 0.2f);
    plates.addBox(Vector3(sx - 0.009f, 1.125f, sz - 0.02f), Vector3(sx - 0.006f, 1.155f, sz + 0.02f), 0.2f);
    // Double socket on the TV wall and one on the window wall.
    plates.addBox(Vector3(0.6f, 0.30f, L.halfDepth - 0.006f), Vector3(0.75f, 0.38f, L.halfDepth), 0.2f);
    plates.addBox(Vector3(-2.2f, 0.30f, -L.halfDepth), Vector3(-2.05f, 0.38f, -L.halfDepth + 0.006f), 0.2f);
    place(plates.take(), "plastic_white", "wall_plates", Matrix::getIdentityProperty(), false);

    // Ceiling rose and a pendant: cable, then a shallow drum shade.
    MeshBuilder fixture;
    fixture.addCylinder(Vector3(0.0f, L.ceilingHeight - 0.02f, 0.0f), 0.05f, 0.02f, 24, 0.2f);
    place(fixture.take(), "plastic_white", "pendant_rose", Matrix::getIdentityProperty(), false);
}

void RoomScene::applyWeather(const WeatherState& weather, float seconds)
{
    if (weatherMaterials_.empty())
    {
        const auto track = [&](const char* name, float darken, float rough, float snow) {
            if (const Material* m = materials_.find(name))
                weatherMaterials_.push_back(WeatherMaterial{name, darken, rough, snow, m->baseColour, m->roughness,
                                                            m->albedo, m->normal, m->orm, m->uvScale, false});
        };
        track("asphalt", 0.45f, 0.30f, 0.45f);
        track("paving", 0.55f, 0.35f, 0.9f);
        track("concrete", 0.6f, 0.4f, 0.8f);
        track("road_paint", 0.7f, 0.35f, 0.5f);
        track("grass", 0.7f, 0.6f, 1.0f);
        track("hedge", 0.75f, 0.6f, 0.7f);
        track("hedge_fringe", 0.75f, 0.6f, 0.7f);
        track("foliage", 0.8f, 0.7f, 0.7f);
        track("roof_tiles", 0.65f, 0.35f, 0.95f);
        track("roof_slate", 0.6f, 0.25f, 0.95f);
        track("bark", 0.6f, 0.5f, 0.2f);
        track("brick_red", 0.7f, 0.5f, 0.15f);
        track("facade_render", 0.85f, 0.7f, 0.05f);
        track("facade_render_2", 0.85f, 0.7f, 0.05f);
        track("facade_render_3", 0.85f, 0.7f, 0.05f);
        track("facade_render_4", 0.85f, 0.7f, 0.05f);
        track("metal_paint_dark", 0.8f, 0.3f, 0.3f);
        // The street's furniture and the parked cars: their tops take the snow.
        track("car_paint_blue", 0.9f, 0.5f, 0.85f);
        track("car_paint_silver", 0.9f, 0.5f, 0.85f);
        track("car_paint_red", 0.9f, 0.5f, 0.85f);
        track("car_glass", 0.95f, 0.8f, 0.75f);
        track("car_chrome", 0.95f, 0.8f, 0.3f);
        track("rubber_black", 0.9f, 0.7f, 0.35f);
        track("bench_wood", 0.75f, 0.45f, 0.85f);
        track("gutter_metal", 0.85f, 0.5f, 0.45f);
    }
    const float wet = std::clamp(weather.wetness, 0.0f, 1.0f);
    const float snow = std::clamp(weather.snowCover, 0.0f, 1.0f);
    const Vector3 snowColour(0.86f, 0.88f, 0.93f);
    const MaterialLibrary::SurfaceTextures& snowTextures = materials_.snowSurface();
    for (WeatherMaterial& wm : weatherMaterials_)
    {
        Material* m = materials_.edit(wm.name);
        if (m == nullptr) continue;
        // Wet: darker and glossier. Snow: past a third of cover the surface
        // swaps to the snow textures (base colour is only a multiplier, so it
        // cannot whiten a dark texture); below that it lightens a little.
        Vector3 colour = wm.dryColour * (1.0f - wet * (1.0f - wm.wetDarken));
        float roughness = wm.dryRoughness * (1.0f - wet * (1.0f - wm.wetRoughness));
        const float cover = snow * wm.snowBlend;
        const bool snowed = cover > 0.35f && snowTextures.albedo != nullptr;
        // Leaves keep their own masks and get a snow-dusted variant instead of the ground's snow.
        const bool leafy = wm.name == "foliage" || wm.name == "hedge" || wm.name == "hedge_fringe";
        const MaterialLibrary::SurfaceTextures& swap = wm.name == "foliage" ? materials_.foliageSnowSurface()
                                                       : wm.name == "hedge" ? materials_.hedgeSnowSurface()
                                                       : wm.name == "hedge_fringe" ? materials_.hedgeFringeSnowSurface() : snowTextures;
        if (snowed != wm.snowed)
        {
            wm.snowed = snowed;
            m->albedo = snowed ? swap.albedo : wm.dryAlbedo;
            m->normal = snowed ? swap.normal : wm.dryNormal;
            m->orm = snowed ? swap.orm : wm.dryOrm;
            m->uvScale = snowed && !leafy ? wm.dryUvScale * (1.0f / 1.5f) : wm.dryUvScale;
        }
        if (snowed && leafy)
        {
            colour = wm.dryColour;   // the dusting is in the texture
            roughness = wm.dryRoughness;
        }
        else if (snowed)
        {
            const float t = std::clamp((cover - 0.35f) / 0.3f, 0.0f, 1.0f);
            colour = Vector3(0.75f, 0.77f, 0.82f) * (1.0f - t) + Vector3(1.0f, 1.0f, 1.0f) * t;
            roughness = 0.95f;
        }
        else
        {
            colour = colour * (1.0f - cover) + snowColour * cover;
            roughness = roughness * (1.0f - cover) + 0.95f * cover;
        }
        m->baseColour = colour;
        m->roughness = roughness;
        // The road and pavement mirror the street in proportion to the film
        // of water on them (none under snow).
        if (wm.name == "asphalt" || wm.name == "paving")
            m->reflectionTint = Vector3(1.0f, 1.0f, 1.0f) * (snowed ? 0.0f : wet);
    }
    renderer_.setReflectionPlaneEnabled(streetReflection_, wet > 0.03f && snow < 0.35f);
    // Droplets on the panes while it rains, drying with the wetness.
    if (Material* glass = materials_.edit("window_glass"))
    {
        const bool raining = weather.type == PrecipitationType::Rain || weather.type == PrecipitationType::Hail;
        const float drops = std::clamp(raining ? std::max(wet, 0.4f) : wet * 0.7f, 0.0f, 1.0f);
        // The runners slide a tile (0.4 m) in about 1.3 s: six frames a second through the loop.
        const int frame = static_cast<int>(seconds * 6.0f);
        glass->normal = drops > 0.02f ? materials_.raindropsNormal(frame) : nullptr;
        glass->normalScale = drops;
        glass->uvScale = Vector2(3.5f, 3.5f);   // ~0.4 m tiles: drops of a centimetre or two
    }
    // Chimney smoke across the street on cold days (the stoves lit as ours
    // is), fully below 8 C and gone above 14, carried by the wind.
    {
        const float smoke = chimneySmokeLevel(weather.temperatureC);
        std::vector<SceneRenderer::SmokePlume> plumes;
        if (smoke > 0.0f)
        {
            // Leaning with the wind at half its speed: the plume's young, dense part stays over the pot.
            const Vector3 drift(std::sin(weather.windDirection) * weather.windSpeed * 0.45f, 0.0f, std::cos(weather.windDirection) * weather.windSpeed * 0.45f);
            std::size_t index = 0;
            for (const Vector3& top : chimneyTops_)
            {
                if (index++ % 3 == 2) continue;   // not every hearth is lit
                plumes.push_back({top, drift, smoke});
            }
        }
        if (plumes.size() != smokePlumeCount_)
        {
            smokePlumeCount_ = plumes.size();
            CNA::Logger::Info("living-room-simulator: chimney smoke: " + std::to_string(plumes.size()) + " plumes of " + std::to_string(chimneyTops_.size()) + " chimneys at "
                              + std::to_string(weather.temperatureC) + " C, strength " + std::to_string(smoke));
        }
        renderer_.setSmokePlumes(std::move(plumes));
    }
    // Curtains: a draught proportional to the wind rocks each panel about its rod.
    const float wind = std::clamp(weather.windSpeed / 12.0f, 0.0f, 1.0f);
    // The street trees: each crown sways about the top of its trunk, a slow
    // lean downwind with gusts on top, up to a few degrees in a storm; the
    // shadow proxies follow, so the dapples in the room drift too.
    {
        const Vector3 downwind(std::sin(weather.windDirection), 0.0f, std::cos(weather.windDirection));
        // CNA_ROOM_TREE_SWAY=N scales the amplitude (a check that the crowns turn about their trunks).
        static const float swayScale = [] {
            const char* value = std::getenv("CNA_ROOM_TREE_SWAY");
            return value != nullptr ? std::max(0.0f, static_cast<float>(std::atof(value))) : 1.0f;
        }();
        for (const Tree& tree : trees_)
        {
            const float gust = std::sin(seconds * 0.6f + tree.phase) * 0.55f + std::sin(seconds * 1.7f + tree.phase * 1.9f) * 0.3f + std::sin(seconds * 3.1f + tree.phase * 0.7f) * 0.15f;
            const float lean = MathHelper::ToRadians(7.0f) * swayScale * wind * (0.6f + 0.4f * gust);
            const float across = MathHelper::ToRadians(2.5f) * swayScale * wind * std::sin(seconds * 1.1f + tree.phase * 2.3f);
            // Lean downwind: a rotation about the axis across the wind; the wobble about the downwind axis.
            const Vector3 axisAcross = Vector3::Cross(Vector3::Up, downwind);
            const Matrix sway = Matrix::CreateTranslation(-tree.pivot) * Matrix::CreateFromAxisAngle(axisAcross, lean) * Matrix::CreateFromAxisAngle(downwind, across)
                                * Matrix::CreateTranslation(tree.pivot);
            for (std::size_t index : {tree.canopy, tree.shadow})
            {
                if (index >= renderer_.itemCount()) continue;
                SceneItem& item = renderer_.item(index);
                item.world = sway;
                SceneRenderer::updateBounds(item);
            }
        }
    }
    for (const Curtain& c : curtains_)
    {
        const float gust = std::sin(seconds * 0.9f + c.phase) * 0.6f + std::sin(seconds * 2.3f + c.phase * 1.7f) * 0.4f;
        const float angle = MathHelper::ToRadians(2.5f) * wind * gust;
        SceneItem& item = renderer_.item(c.item);
        item.world = c.baseWorld * Matrix::CreateTranslation(-c.pivot) * Matrix::CreateRotationX(angle)
                     * Matrix::CreateTranslation(c.pivot);
        SceneRenderer::updateBounds(item);
    }
}

void RoomScene::setClockTime(float hours)
{
    // Clockwise as seen from the room: a turn about the dial's normal (+X)
    // by the negative angle takes 12 (+Y) toward 3 (-Z, the viewer's right).
    const float minutes = std::fmod(std::max(hours, 0.0f) * 60.0f, 60.0f);
    const float minuteAngle = minutes / 60.0f * MathHelper::TwoPi;
    const float hourAngle = std::fmod(std::max(hours, 0.0f), 12.0f) / 12.0f * MathHelper::TwoPi;
    const auto turn = [&](std::size_t index, const Vector3& pivot, float angle) {
        if (index >= renderer_.itemCount()) return;
        SceneItem& item = renderer_.item(index);
        item.world = Matrix::CreateRotationX(-angle) * Matrix::CreateTranslation(pivot);
        SceneRenderer::updateBounds(item);
    };
    turn(clockHourHand_, clockHourPivot_, hourAngle);
    turn(clockMinuteHand_, clockMinutePivot_, minuteAngle);
}

void RoomScene::setLampsOn(bool on) { lampsOn_ = on; }

void RoomScene::setFireOn(bool on) { fireOn_ = on; }

void RoomScene::setStreetLightsOn(bool on) { streetLightsOn_ = on; }

void RoomScene::update(float dt)
{
    // Incandescent-like: a second to full brightness, faster off.
    const auto ramp = [dt](float level, bool on, float up, float down) {
        const float target = on ? 1.0f : 0.0f;
        const float rate = on ? up : down;
        if (level < target) return std::min(target, level + rate * dt);
        return std::max(target, level - rate * dt);
    };
    const float lamp = ramp(lampLevel_, lampsOn_, 1.2f, 2.5f);
    const float street = ramp(streetLevel_, streetLightsOn_, 1.2f, 2.5f);
    // The fire catches and dies slowly, and flickers while it burns.
    const float fire = ramp(fireLevel_, fireOn_, 0.25f, 0.12f);
    fireSeconds_ += dt;
    if (televisionOn_) updateTelevisionGlow();
    updateWindowLights();
    const bool fireLive = fire > 0.0f || fireLevel_ > 0.0f || lamp > 0.0f;   // the candle burns with the lamps
    if (lamp == lampLevel_ && street == streetLevel_ && !fireLive) return;
    lampLevel_ = lamp;
    streetLevel_ = street;
    fireLevel_ = fire;
    if (fireLive) updateFire();
    applyLampLevels();
}

void RoomScene::updateWindowLights()
{
    // Each window's spot: the sky's hemisphere-average radiance over the
    // opening's area gives the irradiance it delivers at a metre (a point
    // source stands in for the opening; the effect's 1 / (1 + d^2) keeps the
    // near field finite). The probes carry the window's light too, at their
    // own distance, so this over-counts a little near the glass: the price of
    // a gradient the three probes cannot hold.
    const SkyLighting& lighting = renderer_.sky().lighting();
    const Vector3 sky = lighting.ambientColour;
    const float top = std::max({sky.X, sky.Y, sky.Z, 1e-6f});
    const float area = layout_.windowWidth * layout_.windowHeight;
    // CNA_ROOM_WINDOW_LIGHT scales the windows' light (0 turns it off).
    static const float scale = std::getenv("CNA_ROOM_WINDOW_LIGHT") != nullptr ? std::strtof(std::getenv("CNA_ROOM_WINDOW_LIGHT"), nullptr) : 1.0f;
    // Scaled by the daylight: the night sky's glow through the glass is real
    // but the probes carry it, and the lamp-lit calibration stays untouched.
    const float intensity = top * area * scale * std::clamp(lighting.daylight, 0.0f, 1.0f);
    bool changed = false;
    for (const Lamp& lamp : std::as_const(renderer_).lamps())
        if (lamp.daylightPortal && std::abs(lamp.intensity - intensity) > intensity * 0.02f + 1e-6f) changed = true;
    if (!changed) return;
    for (Lamp& lamp : renderer_.lamps())
    {
        if (!lamp.daylightPortal) continue;
        lamp.colour = sky / top;
        lamp.intensity = intensity;
        lamp.fullIntensity = intensity;
        lamp.on = intensity > 1e-4f;
    }
    if (!loggedWindowLights_)
    {
        loggedWindowLights_ = true;
        CNA::Logger::Info("living-room-simulator: window lights " + std::to_string(intensity) + " (sky " + std::to_string(sky.X) + "," + std::to_string(sky.Y) + "," + std::to_string(sky.Z) + ")");
    }
}

void RoomScene::updateTelevisionGlow()
{
    // The set's light on the room follows its picture: the lamp takes the
    // picture's mean colour at the luminance of its calibrated cool white,
    // and its level the mean's luminance against the programme's own mean
    // (kTelevisionMeanLuminance, measured over the 70 s cycle), so the
    // 250 lm stay the long-run level and the cuts and pans move around it.
    const TelevisionContent* content = renderer_.television();
    if (content == nullptr || !content->hasMean()) return;
    constexpr float kTelevisionMeanLuminance = 0.23f;
    const Vector3 mean = content->meanColour();
    const auto luminance = [](const Vector3& v) { return 0.2126f * v.X + 0.7152f * v.Y + 0.0722f * v.Z; };
    const float lum = luminance(mean);
    const float level = std::clamp(lum / kTelevisionMeanLuminance, 0.1f, 2.5f);
    for (Lamp& lamp : renderer_.lamps())
    {
        if (lamp.name != "television") continue;
        static const Vector3 white(0.75f, 0.85f, 1.0f);
        static const float whiteLuminance = luminance(white);
        const Vector3 colour = lum > 1e-4f ? mean * (whiteLuminance / lum) : white;
        const float top = std::max({colour.X, colour.Y, colour.Z});
        lamp.colour = top > 2.0f ? colour * (2.0f / top) : colour;
        lamp.intensity = lamp.fullIntensity * level;
        if (!loggedTelevisionGlow_ || std::getenv("CNA_ROOM_DEBUG_TV") != nullptr)
        {
            loggedTelevisionGlow_ = true;
            CNA::Logger::Info("living-room-simulator: television picture mean " + std::to_string(mean.X) + " " + std::to_string(mean.Y) + " " + std::to_string(mean.Z)
                              + " luminance " + std::to_string(lum) + " level " + std::to_string(level));
        }
    }
}

void RoomScene::updateFire()
{
    // Three sines at unrelated rates, per card and for the light; the embers
    // breathe more slowly.
    const auto flicker = [&](float phase, float depth) {
        const float t = fireSeconds_;
        const float f = 0.5f * std::sin(t * 9.1f + phase) + 0.3f * std::sin(t * 13.7f + phase * 1.7f + 1.0f) + 0.2f * std::sin(t * 23.3f + phase * 0.6f + 2.0f);
        return 1.0f - depth + depth * (0.5f + 0.5f * f);
    };
    fireFlicker_ = flicker(0.0f, 0.35f);
    candleFlicker_ = flicker(7.0f, 0.2f);
    for (const FireCard& card : fireCards_)
    {
        if (card.item >= renderer_.itemCount()) continue;
        SceneItem& item = renderer_.item(card.item);
        const float level = card.group == 0 ? fireLevel_ : lampLevel_;
        const float lick = flicker(card.phase, card.group == 0 ? 0.5f : 0.25f);
        // Height breathes, the card leans a touch in the draught.
        item.world = Matrix::CreateScale(1.0f, 0.75f + 0.5f * lick, 1.0f) * Matrix::CreateRotationZ(0.06f * (lick - 0.7f)) * card.baseWorld;
        SceneRenderer::updateBounds(item);
        if (Material* m = materials_.edit(item.material != nullptr ? item.material->name : ""))
        {
            // A wood flame is a few thousand cd/m^2 (0.9 in scene units at the
            // card's core), a candle's brighter and smaller.
            const float strength = level * (0.55f + 0.45f * lick) * card.strength;
            m->emissiveFactor = Vector3(strength, strength, strength);
            m->alpha = level;
        }
    }
    if (Material* m = materials_.edit("ember_bed"))
    {
        const float strength = fireLevel_ * (0.8f + 0.2f * fireFlicker_) * 0.28f;
        m->emissiveFactor = Vector3(strength, strength, strength);
    }
}

void RoomScene::applyLampLevels()
{
    // A warm filament dims red first: the colour is scaled with a slight bias.
    const auto tint = [](float level) { return Vector3(level, std::pow(level, 1.15f), std::pow(level, 1.3f)); };
    for (Lamp& lamp : renderer_.lamps())
    {
        if (lamp.name == "television" || lamp.name == "hall" || lamp.daylightPortal) continue;
        if (lamp.name == "stove" || lamp.name == "candle")
        {
            const float level = lamp.name == "stove" ? fireLevel_ * (0.7f + 0.3f * fireFlicker_) : lampLevel_ * (0.8f + 0.2f * candleFlicker_);
            lamp.intensity = lamp.fullIntensity * level;
            lamp.on = level > 0.002f;
            continue;
        }
        const bool isStreet = lamp.name.rfind("street", 0) == 0;
        const float level = isStreet ? streetLevel_ : lampLevel_;
        lamp.intensity = lamp.fullIntensity * level;
        lamp.on = level > 0.002f;
    }
    for (const Glow& glow : glows_)
    {
        if (glow.television) continue;
        const float level = glow.street ? streetLevel_ : lampLevel_;
        if (Material* m = materials_.edit(glow.material))
        {
            const Vector3 t = tint(level);
            m->emissiveFactor = Vector3(glow.colour.X * t.X, glow.colour.Y * t.Y, glow.colour.Z * t.Z) * glow.strength;
        }
    }
}

void RoomScene::setTelevisionOn(bool on)
{
    televisionOn_ = on;
    renderer_.setTelevisionPlaying(on);
    if (Material* picture = materials_.edit("tv_content"))
    {
        TelevisionContent* content = renderer_.television();
        const bool playing = on && content != nullptr && content->supported();
        picture->emissive = playing ? content->texture() : nullptr;
        // ~120 cd/m^2, a set's dark-room picture mode (0.017 in scene units, on
        // top of the glow's own level): at 200 the picture paled under the
        // tonemapper next to the lamp-lit walls.
        picture->emissiveFactor = playing ? Vector3(0.017f, 0.017f, 0.017f) : Vector3::Zero;
    }
    for (Lamp& lamp : renderer_.lamps())
        if (lamp.name == "television") lamp.on = on;
    for (const Glow& glow : glows_)
    {
        if (!glow.television) continue;
        if (Material* m = materials_.edit(glow.material))
            m->emissiveFactor = on ? glow.colour * glow.strength : Vector3::Zero;
    }
}

void RoomScene::buildViewpoints()
{
    viewpoints_ = {
        {"entrance", Vector3(2.6f, 1.6f, 1.3f), 70.0f, -6.0f},
        {"sofa-to-tv", Vector3(0.0f, 1.2f, -0.9f), 180.0f, -4.0f},
        {"tv-to-sofa", Vector3(0.3f, 1.4f, 2.0f), 5.0f, -8.0f},
        {"bookshelf", Vector3(-0.8f, 1.5f, 0.4f), -80.0f, -4.0f},
        {"window", Vector3(0.3f, 1.35f, 1.05f), 12.0f, 3.0f},
        {"window-close", Vector3(0.75f, 1.5f, -1.55f), -32.0f, 3.0f},   // the right window, the floor lamp just out of frame
        {"material", Vector3(0.95f, 1.15f, 0.25f), 150.0f, -28.0f},
        {"corner", Vector3(-2.6f, 2.3f, 1.9f), -50.0f, -22.0f},
        {"tv-close", Vector3(0.0f, 1.5f, 0.9f), 180.0f, -5.0f},
        {"street", Vector3(1.45f, 1.55f, -1.75f), 0.0f, 3.0f},
        {"street-left", Vector3(-1.2f, 1.6f, -1.5f), -18.0f, 6.0f},
        {"lamp", Vector3(0.2f, 1.3f, -0.3f), 130.0f, 5.0f},
    };
}

}  // namespace CnaRoom
