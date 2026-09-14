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
        CNA::Logger::Warn("cna-room: model -- " + failure);
    CNA::Logger::Info("cna-room: room built -- " + std::to_string(meshes_.size()) + " procedural meshes, "
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
    CNA::Logger::Info("cna-room: " + std::to_string(renderer_.decalCount()) + " wear decals");
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

void RoomScene::buildBookcase()
{
    // A tall open bookcase against the -X wall: 1.8 m wide, 2.1 m tall, 0.32 m
    // deep, five shelves, in dark stained wood.
    const RoomLayout& L = layout_;
    const float x0 = -L.halfWidth + 0.02f, x1 = x0 + 0.32f;
    const float z0 = -0.30f, z1 = z0 + 1.80f;
    const float top = 2.10f, side = 0.025f, shelfT = 0.022f;
    MeshBuilder b;
    b.addBox(Vector3(x0, 0.0f, z0), Vector3(x1, top, z0 + side), 0.5f);            // left side
    b.addBox(Vector3(x0, 0.0f, z1 - side), Vector3(x1, top, z1), 0.5f);            // right side
    b.addBox(Vector3(x0, top - shelfT, z0 + side), Vector3(x1, top, z1 - side), 0.5f); // top
    b.addBox(Vector3(x0, 0.0f, z0 + side), Vector3(x1, 0.08f, z1 - side), 0.5f);     // plinth
    b.addBox(Vector3(x0, 0.08f, z0 + side), Vector3(x0 + 0.012f, top - shelfT, z1 - side), 0.5f); // back panel
    const float shelfY[] = {0.08f, 0.50f, 0.92f, 1.34f, 1.76f};
    for (float y : shelfY)
        b.addBox(Vector3(x0 + 0.012f, y, z0 + side), Vector3(x1, y + shelfT, z1 - side), 0.5f);
    // Centre divider on the lower two bays.
    b.addBox(Vector3(x0 + 0.012f, 0.08f, (z0 + z1) * 0.5f - 0.012f), Vector3(x1, 0.92f, (z0 + z1) * 0.5f + 0.012f), 0.5f);
    place(b.take(), "wood_walnut", "bookcase");
}

void RoomScene::buildFurniture()
{
    const RoomLayout& L = layout_;
    const float H = L.ceilingHeight;
    // --- the seating group, centred on the rug, facing the television wall (+Z) ---
    placeModel("rug", Vector3(0.0f, 0.0f, 0.15f), 90.0f);
    // The source models' palette materials carry a mirror-like roughness (0.04)
    // that reads as porcelain; real leather and lacquer sit far higher.
    // The White Room's palette whites sit near 1.0; no cloth or lacquer
    // reflects that much (fresh white paint ~0.85, white fabric ~0.75), and
    // at 1.0 the sofa and table clipped to featureless slabs under the
    // window: the palette parts are brought down to 0.84 of themselves.
    const ModelLibrary::MaterialTweak leather = [](Material& m, int) {
        if (m.albedo != nullptr && m.albedo->getHeightProperty() <= 4)  // a palette texel, not a photo
        {
            m.roughness = 0.55f;
            m.specular = 0.7f;
            m.baseColour = m.baseColour * 0.84f;
        }
    };
    const ModelLibrary::MaterialTweak lacquer = [](Material& m, int) {
        if (m.albedo != nullptr && m.albedo->getHeightProperty() <= 4)
        {
            m.roughness = 0.35f;
            m.baseColour = m.baseColour * 0.84f;
        }
    };
    {
        const std::size_t sofaFirst = renderer_.itemCount();
        placeModel("leather-sofa", Vector3(0.0f, 0.0f, -0.75f), 180.0f, 0.0f, true, leather);
        // A knitted throw folded over the left end of the sofa's back (the
        // part of the model furthest back), hanging a little down both faces.
        const BoundingBox* back = nullptr;
        for (std::size_t i = sofaFirst; i < renderer_.itemCount(); ++i)
        {
            const BoundingBox& b = renderer_.item(i).worldBounds;
            if (back == nullptr || (b.Min.Z + b.Max.Z) < (back->Min.Z + back->Max.Z)) back = &renderer_.item(i).worldBounds;
        }
        if (back != nullptr)
        {
            // A strip of cloth draped over the back: down the front, over the
            // top and down the back, with soft folds along its width and a
            // wavy hem, as quads between path samples (double-sided knit).
            const float top = back->Max.Y, z0 = back->Min.Z, z1 = back->Max.Z;
            const float x0 = std::max(back->Min.X + 0.05f, -0.62f), x1 = x0 + 0.42f;
            const float zm = (z0 + z1) * 0.5f;
            const float lift = 0.012f;   // the cloth's thickness over the back
            const Vector2 path[] = {   // (z, y), front hem first
                Vector2(z1 + 0.03f, top - 0.30f), Vector2(z1 + 0.03f, top - 0.16f), Vector2(z1 + 0.028f, top - 0.05f),
                Vector2(z1 + 0.012f, top + lift * 0.4f), Vector2(zm, top + lift), Vector2(z0 - 0.012f, top + lift * 0.4f),
                Vector2(z0 - 0.028f, top - 0.05f), Vector2(z0 - 0.03f, top - 0.20f), Vector2(z0 - 0.03f, top - 0.40f)};
            constexpr int kAcross = 8;
            const int along = static_cast<int>(sizeof(path) / sizeof(path[0]));
            std::vector<Vector3> grid(static_cast<std::size_t>(along) * (kAcross + 1));
            for (int i = 0; i < along; ++i)
                for (int j = 0; j <= kAcross; ++j)
                {
                    const float t = static_cast<float>(j) / kAcross;
                    const float x = x0 + (x1 - x0) * t;
                    // Folds: a ripple across the width that deepens down the hangs.
                    const float hang = std::max(0.0f, top - path[i].Y);
                    const float ripple = (std::sin(t * 6.2831853f * 2.5f + static_cast<float>(i) * 0.7f) + 0.5f * std::sin(t * 6.2831853f * 5.3f + 2.0f))
                                         * (0.006f + 0.06f * hang);
                    const float front = path[i].X > zm ? 1.0f : -1.0f;
                    const float y = path[i].Y - (i == 0 || i == along - 1 ? 0.02f * std::sin(t * 6.2831853f * 1.5f + 1.0f) : 0.0f);
                    grid[static_cast<std::size_t>(i) * (kAcross + 1) + static_cast<std::size_t>(j)] = Vector3(x, y, path[i].X + ripple * front);
                }
            MeshBuilder throwCloth;
            for (int i = 0; i + 1 < along; ++i)
                for (int j = 0; j < kAcross; ++j)
                {
                    const auto at = [&](int a, int b) { return grid[static_cast<std::size_t>(a) * (kAcross + 1) + static_cast<std::size_t>(b)]; };
                    throwCloth.addQuad(at(i, j), at(i, j + 1), at(i + 1, j + 1), at(i + 1, j), 0.12f);
                }
            place(throwCloth.take(), "throw_knit", "throw", Matrix::getIdentityProperty(), true);
        }
        // A pair of slippers kicked off in front of the sofa's right end: a
        // rubber sole, a felt footbed and a rounded felt vamp over the toes.
        if (renderer_.itemCount() > sofaFirst)
        {
            BoundingBox sofa = renderer_.item(sofaFirst).worldBounds;
            for (std::size_t i = sofaFirst + 1; i < renderer_.itemCount(); ++i) sofa = BoundingBox::CreateMerged(sofa, renderer_.item(i).worldBounds);
            const float front = sofa.Max.Z;   // the sofa faces +Z
            const auto slipper = [&](const std::string& name, float x, float z, float yawDegrees) {
                const Matrix world = Matrix::CreateRotationY(MathHelper::ToRadians(yawDegrees)) * Matrix::CreateTranslation(x, 0.0f, z);
                MeshBuilder sole;
                sole.addBox(Vector3(-0.05f, 0.0f, -0.13f), Vector3(0.05f, 0.012f, 0.13f), 0.2f);
                place(sole.take(), "rubber_black", name + " sole", world, true);
                MeshBuilder felt;
                felt.addBox(Vector3(-0.048f, 0.012f, -0.128f), Vector3(0.048f, 0.024f, 0.10f), 0.2f);   // the footbed
                MeshBuilder vamp;   // a squashed sphere over the toes, its bottom in the sole
                vamp.addSphere(Vector3(0.0f, 0.0f, 0.0f), 1.0f, 18, 10, 0.2f);
                vamp.transform(Matrix::CreateScale(0.054f, 0.034f, 0.095f) * Matrix::CreateTranslation(0.0f, 0.034f, 0.04f));
                felt.mesh().append(vamp.mesh());
                place(felt.take(), "slipper_felt", name + " felt", world, true);
            };
            slipper("slipper left", 0.70f, front + 0.19f, 6.0f);
            slipper("slipper right", 0.86f, front + 0.16f, -24.0f);
            CNA::Logger::Info("cna-room: sofa front z " + std::to_string(front) + ", slippers at z " + std::to_string(front + 0.16f));
        }
    }
    placeModel("coffee-table", Vector3(0.0f, 0.0f, 0.45f), 0.0f, 0.0f, true, lacquer);
    {
        // A folded newspaper dropped on the left armchair's seat (the right
        // one holds the folded blankets): the front page on top, masthead
        // toward the chair's back, plain paper below.
        const std::size_t chairFirst = renderer_.itemCount();
        placeModel("leather-armchair-a", Vector3(-2.05f, 0.0f, 0.55f), 55.0f, 0.0f, true, leather);
        if (renderer_.itemCount() > chairFirst)
        {
            BoundingBox chair = renderer_.item(chairFirst).worldBounds;
            const BoundingBox* seat = nullptr;
            float bestArea = 0.0f;
            for (std::size_t i = chairFirst; i < renderer_.itemCount(); ++i)
            {
                const BoundingBox& b = renderer_.item(i).worldBounds;
                chair = BoundingBox::CreateMerged(chair, b);
                // The seat cushion: the widest part whose top lies at sitting height.
                if (b.Max.Y > 0.30f && b.Max.Y < 0.60f)
                {
                    const float area = (b.Max.X - b.Min.X) * (b.Max.Z - b.Min.Z);
                    if (area > bestArea) { bestArea = area; seat = &b; }
                }
            }
            const Vector3 chairCentre = (chair.Min + chair.Max) * 0.5f;
            Vector3 seatCentre = chairCentre;
            float seatTop = 0.42f;
            if (seat != nullptr)
            {
                seatCentre = (seat->Min + seat->Max) * 0.5f;
                seatTop = seat->Max.Y;
            }
            // Forward is from the chair's centre toward the seat's (the back sits behind it).
            Vector3 forward(seatCentre.X - chairCentre.X, 0.0f, seatCentre.Z - chairCentre.Z);
            if (forward.Length() < 0.02f) forward = Vector3(-1.0f, 0.0f, 0.0f);
            forward.Normalize();
            CNA::Logger::Info("cna-room: armchair seat top " + std::to_string(seatTop) + " at " + std::to_string(seatCentre.X) + ", " + std::to_string(seatCentre.Z)
                              + " forward " + std::to_string(forward.X) + ", " + std::to_string(forward.Z) + (seat != nullptr ? "" : " (no seat part found)"));
            // Local -Z is the masthead's edge; RotY(yaw) sends it to -forward.
            const float yaw = std::atan2(forward.X, forward.Z) + MathHelper::ToRadians(9.0f);
            const Matrix world = Matrix::CreateRotationY(yaw)
                                 * Matrix::CreateTranslation(seatCentre.X + forward.X * 0.03f, seatTop + 0.001f, seatCentre.Z + forward.Z * 0.03f);
            const float hw = 0.145f, hd = 0.10f, th = 0.012f;
            MeshBuilder body;
            body.addBox(Vector3(-hw, 0.0f, -hd), Vector3(hw, th, hd), 0.3f, kAllFaces & ~kFacePosY);
            place(body.take(), "paper", "newspaper pages", world, true);
            MeshBuilder page;   // seen from above with +X to the right, +Z is down the page
            page.addQuadUv(Vector3(-hw, th + 0.0003f, hd), Vector3(hw, th + 0.0003f, hd), Vector3(hw, th + 0.0003f, -hd), Vector3(-hw, th + 0.0003f, -hd));
            place(page.take(), "newsprint", "newspaper front", world, false);
        }
    }
    placeModel("leather-armchair-a", Vector3(2.05f, 0.0f, 0.55f), -55.0f, 0.0f, true, leather);
    placeModel("floor-lamp", Vector3(1.35f, 0.0f, -1.55f), 0.0f);
    placeModel("pedestal-table", Vector3(-2.35f, 0.0f, -1.20f), 0.0f);
    placeModel("IridescenceLamp", Vector3(-2.35f, 0.632f, -1.20f), 20.0f);
    placeModel("bottle-and-glasses", Vector3(-2.15f, 0.632f, -1.05f), 0.0f);
    placeModel("DiffuseTransmissionTeacup", Vector3(-2.52f, 0.632f, -1.32f), 25.0f);
    placeModel("SpecularSilkPouf", Vector3(-1.55f, 0.0f, 1.45f), 30.0f);

    // Things on the coffee table (top at 0.419 m).
    placeModel("magazine", Vector3(-0.30f, 0.419f, 0.42f), 12.0f, 0.0f, false);
    {
        // A paperback left open, face up, on the magazine: cloth covers under
        // two blocks of pages, the spine along the book's short axis.
        MeshBuilder covers, pages;
        covers.addBox(Vector3(-0.135f, 0.0f, -0.10f), Vector3(-0.003f, 0.004f, 0.10f), 0.3f);
        covers.addBox(Vector3(0.003f, 0.0f, -0.10f), Vector3(0.135f, 0.004f, 0.10f), 0.3f);
        pages.addBox(Vector3(-0.128f, 0.004f, -0.095f), Vector3(-0.004f, 0.016f, 0.095f), 0.3f);
        pages.addBox(Vector3(0.004f, 0.004f, -0.095f), Vector3(0.128f, 0.016f, 0.095f), 0.3f);
        const Matrix world = Matrix::CreateRotationY(MathHelper::ToRadians(-8.0f)) * Matrix::CreateTranslation(-0.30f, 0.425f, 0.42f);
        place(covers.take(), "book_cover", "open book covers", world, true);
        place(pages.take(), "paper", "open book pages", world, true);
    }
    placeModel("fruit-bowl", Vector3(0.22f, 0.419f, 0.40f), 0.0f);
    {
        const std::size_t cupFirst = renderer_.itemCount();
        placeModel("cup-and-saucer", Vector3(-0.05f, 0.419f, 0.30f), 40.0f);
        if (renderer_.itemCount() > cupFirst)
        {
            // A hot drink: steam born on the cup's rim (the model's top, over its centre).
            BoundingBox cup = renderer_.item(cupFirst).worldBounds;
            for (std::size_t i = cupFirst + 1; i < renderer_.itemCount(); ++i) cup = BoundingBox::CreateMerged(cup, renderer_.item(i).worldBounds);
            const Vector3 centre = (cup.Min + cup.Max) * 0.5f;
            renderer_.setSteam(Vector3(centre.X, cup.Max.Y - 0.004f, centre.Z), 0.032f);
            // The ring an earlier cup left on the table top, just past the saucer.
            if (Texture2D* ring = materials_.decal("ring"))
                renderer_.addDecal(ring, Matrix::CreateScale(0.11f, 0.11f, 0.04f) * Matrix::CreateRotationX(MathHelper::PiOver2)
                                             * Matrix::CreateTranslation(centre.X + 0.13f, 0.419f, centre.Z - 0.09f), 0.65f);
            CNA::Logger::Info("cna-room: steam over the cup at " + std::to_string(centre.X) + "," + std::to_string(cup.Max.Y) + "," + std::to_string(centre.Z));
        }
    }
    placeModel("candle-holders", Vector3(0.45f, 0.419f, 0.60f), 90.0f);
    placeModel("WaterBottle", Vector3(-0.42f, 0.419f, 0.62f), 0.0f);
    placeModel("Avocado", Vector3(0.12f, 0.419f, 0.58f), 35.0f);

    // Cushions and a blanket on the seating.
    placeModel("cushion-b", Vector3(-0.72f, 0.42f, -0.82f), 200.0f);
    placeModel("cushion-a", Vector3(0.70f, 0.40f, -0.90f), 160.0f);
    placeModel("folded-blankets", Vector3(2.15f, 0.42f, 0.62f), -55.0f);

    // --- the television wall (+Z) ---
    placeModel("tv-unit", Vector3(0.0f, 0.0f, L.halfDepth - 0.40f), 90.0f, 0.0f, true, lacquer);
    placeModel("wood-stove", Vector3(0.0f, 0.0f, L.halfDepth - 0.34f), 90.0f);
    // The stove's fire: an ember bed on the grate, three crossed flame cards
    // above it (their emissive and height flicker with the lamp in update),
    // lit on cool evenings.
    {
        const Vector3 grate(0.0f, 0.36f, L.halfDepth - 0.34f + 0.02f);
        MeshBuilder bed;
        bed.addBox(Vector3(grate.X - 0.11f, grate.Y - 0.03f, grate.Z - 0.07f), Vector3(grate.X + 0.11f, grate.Y + 0.006f, grate.Z + 0.07f), 0.25f,
                   kAllFaces & ~kFaceNegY);
        fireEmbers_ = renderer_.itemCount();
        place(bed.take(), "ember_bed", "stove-embers", Matrix::getIdentityProperty(), false);
        for (int i = 0; i < 3; ++i)
        {
            const float w = 0.22f, h = 0.30f;
            MeshBuilder card;   // built upright about the origin, the tip at the top
            card.addQuadUv(Vector3(-w * 0.5f, 0.0f, 0.0f), Vector3(w * 0.5f, 0.0f, 0.0f), Vector3(w * 0.5f, h, 0.0f), Vector3(-w * 0.5f, h, 0.0f));
            const Matrix base = Matrix::CreateRotationY(MathHelper::ToRadians(static_cast<float>(i) * 60.0f + 15.0f))
                                * Matrix::CreateTranslation(grate.X + (i == 1 ? 0.03f : i == 2 ? -0.025f : 0.0f), grate.Y, grate.Z + (i == 0 ? 0.0f : 0.02f));
            fireCards_.push_back(FireCard{renderer_.itemCount(), base, static_cast<float>(i) * 2.1f});
            place(card.take(), "flame_" + std::to_string(i), "stove-flame-" + std::to_string(i), base, false);
        }
    }
    placeModel("tv", Vector3(0.0f, 1.30f, L.halfDepth - 0.03f), 90.0f, 0.0f, true,
               [](Material& m, int) {
                   m.alphaMode = Microsoft::Xna::Framework::Graphics::AlphaModeEXT::Opaque;
                   m.alpha = 1.0f;
                   m.albedo = nullptr;
                   m.orm = nullptr;
                   m.baseColour = Vector3(0.02f, 0.02f, 0.022f);
                   m.roughness = 0.10f;
                   m.metallic = 0.0f;
                   m.emissive = nullptr;              // the screen glow is driven by setTelevisionOn
                   m.emissiveFactor = Vector3::Zero;
                   m.castsShadow = true;
                   m.writesDepth = true;
               });
    {
        // The picture surface: a quad a hair in front of the model's screen
        // (its own UVs are a palette texel and cannot carry an image). Seen
        // from the sofa (looking +Z) world +X is on the left, so the quad's
        // first corner is at +X for u to run left to right.
        const float screenZ = L.halfDepth - 0.03f - 0.008f - 0.003f;
        const float x0 = -0.553f, x1 = 0.553f, y0 = 1.30f + 0.03f, y1 = 1.30f + 0.725f;
        const float w = x1 - x0, h = y1 - y0;
        MeshBuilder picture;
        picture.addQuad(Vector3(x1, y0, screenZ), Vector3(x0, y0, screenZ), Vector3(x0, y1, screenZ), Vector3(x1, y1, screenZ), w,
                        Vector2(0.0f, h / w));
        if (Material* m = materials_.edit("tv_content")) m->uvScale = Vector2(1.0f, w / h);
        place(picture.take(), "tv_content", "tv_picture", Matrix::getIdentityProperty(), false);
        // The television wall is a mirror plane: the screen reflects the
        // room (Fresnel, so mostly at grazing angles) under its picture, and
        // the mirror beside the fireplace shares the capture.
        ReflectionPlane plane;
        plane.name = "tv-wall";
        plane.plane = Plane(Vector3(0.0f, 0.0f, -1.0f), screenZ);   // n.x + d = 0 -> -z + screenZ = 0
        plane.corners = {Vector3(-2.42f, 0.69f, screenZ), Vector3(0.56f, 0.69f, screenZ), Vector3(0.56f, 2.03f, screenZ),
                         Vector3(-2.42f, 2.03f, screenZ)};
        tvWallReflection_ = renderer_.addReflectionPlane(plane);
        // The window panes at night: the lamp-lit room mirrored in the glass
        // (by day the exterior drowns the few percent the Fresnel term keeps,
        // so the capture only runs from exposure 3 up).
        if (windowGlassX1_ > windowGlassX0_)
        {
            ReflectionPlane panes;
            panes.name = "window-panes";
            panes.plane = Plane(Vector3(0.0f, 0.0f, 1.0f), -windowGlassZ_);   // z - zg = 0, normal into the room (+Z)
            panes.corners = {Vector3(windowGlassX0_, windowGlassY0_, windowGlassZ_), Vector3(windowGlassX1_, windowGlassY0_, windowGlassZ_),
                             Vector3(windowGlassX1_, windowGlassY1_, windowGlassZ_), Vector3(windowGlassX0_, windowGlassY1_, windowGlassZ_)};
            panes.minExposure = 3.0f;
            windowReflection_ = renderer_.addReflectionPlane(panes);
            if (Material* m = materials_.edit("window_glass"))
            {
                m->reflectionPlane = windowReflection_;
                m->reflectionF0 = 0.04f;
                m->reflectionFresnel = true;
                m->reflectionTint = Vector3(1.0f, 1.0f, 1.0f);
            }
        }
        if (Material* m = materials_.edit("tv_content"))
        {
            m->reflectionPlane = tvWallReflection_;
            m->reflectionF0 = 0.045f;
            m->reflectionFresnel = true;
            m->reflectionTint = Vector3(0.95f, 0.96f, 1.0f);
        }
    }
    placeModel("potted-plant-b", Vector3(1.80f, 0.0f, L.halfDepth - 0.45f), 0.0f);
    placeModel("DiffuseTransmissionPlant", Vector3(-2.60f, 0.0f, L.halfDepth - 0.50f), 0.0f);
    // Wall lights flanking the television: a linen drum shade on a brass arm
    // and back plate, a bulb inside seen through the drum's open ends (the
    // source's lattice sconce read as a flat white box by day).
    for (const float x : {-1.10f, 1.10f})
    {
        const float wallZ = L.halfDepth - 0.005f, y = 1.85f;
        const std::string side = x < 0.0f ? "left" : "right";
        MeshBuilder metal;
        metal.addBox(Vector3(x - 0.045f, y - 0.08f, wallZ - 0.012f), Vector3(x + 0.045f, y + 0.08f, wallZ), 0.3f);
        metal.addSphere(Vector3(x, y, wallZ - 0.112f), 0.013f, 12, 8, 0.3f);
        place(metal.take(), "sconce_brass", "sconce " + side + " metal", Matrix::getIdentityProperty(), true);
        MeshBuilder arm;   // built along Y, turned to run out from the wall
        arm.addCylinder(Vector3::Zero, 0.007f, 0.105f, 10, 0.3f, true);
        place(arm.take(), "sconce_brass", "sconce " + side + " arm", Matrix::CreateRotationX(-MathHelper::PiOver2) * Matrix::CreateTranslation(x, y, wallZ - 0.012f), true);
        MeshBuilder shade;
        shade.addCylinder(Vector3(x, y - 0.08f, wallZ - 0.10f), 0.075f, 0.16f, 28, 0.3f, false);
        place(shade.take(), "sconce_linen", "sconce " + side + " shade", Matrix::getIdentityProperty(), true);
        MeshBuilder bulb;
        bulb.addSphere(Vector3(x, y - 0.01f, wallZ - 0.10f), 0.02f, 14, 10, 0.3f);
        place(bulb.take(), "sconce_bulb", "sconce " + side + " bulb", Matrix::getIdentityProperty(), false);
    }

    // --- the door wall (+X) ---
    {
        const std::size_t chestFirst = renderer_.itemCount();
        placeModel("chest-of-drawers", Vector3(L.halfWidth - 0.17f, 0.0f, -1.10f), 180.0f);
        if (renderer_.itemCount() > chestFirst)
        {
            // The day's post and a bunch of keys dropped on the chest's near end.
            BoundingBox chest = renderer_.item(chestFirst).worldBounds;
            for (std::size_t i = chestFirst + 1; i < renderer_.itemCount(); ++i) chest = BoundingBox::CreateMerged(chest, renderer_.item(i).worldBounds);
            const float top = chest.Max.Y + 0.001f;
            const float cx = (chest.Min.X + chest.Max.X) * 0.5f, cz = chest.Min.Z + 0.17f;   // the end past the vase
            CNA::Logger::Info("cna-room: chest top " + std::to_string(top) + " x " + std::to_string(chest.Min.X) + ".." + std::to_string(chest.Max.X) + " z "
                              + std::to_string(chest.Min.Z) + ".." + std::to_string(chest.Max.Z));
            MeshBuilder post;
            post.addBox(Vector3(-0.055f, 0.0f, -0.11f), Vector3(0.055f, 0.003f, 0.11f), 0.3f);
            post.addBox(Vector3(-0.05f, 0.003f, -0.10f), Vector3(0.06f, 0.006f, 0.12f), 0.3f);
            post.addBox(Vector3(-0.065f, 0.006f, -0.09f), Vector3(0.045f, 0.009f, 0.115f), 0.3f);
            place(post.take(), "paper", "post", Matrix::CreateRotationY(MathHelper::ToRadians(14.0f)) * Matrix::CreateTranslation(cx, top, cz), true);
            MeshBuilder keys;
            keys.addTorus(Vector3(0.0f, 0.002f, 0.0f), 0.013f, 0.0015f, 20, 6, 0.3f);
            keys.addBox(Vector3(0.010f, 0.0f, -0.004f), Vector3(0.056f, 0.002f, 0.004f), 0.3f);   // a key's blade and bow
            keys.addBox(Vector3(0.006f, 0.0f, -0.008f), Vector3(0.020f, 0.002f, 0.008f), 0.3f);
            keys.addBox(Vector3(-0.004f, 0.002f, 0.010f), Vector3(0.004f, 0.004f, 0.052f), 0.3f);   // a second key across
            keys.addBox(Vector3(-0.008f, 0.002f, 0.006f), Vector3(0.008f, 0.004f, 0.020f), 0.3f);
            place(keys.take(), "metal_polished", "keys",
                  Matrix::CreateRotationY(MathHelper::ToRadians(-30.0f)) * Matrix::CreateTranslation(cx + 0.06f, top, (chest.Min.Z + chest.Max.Z) * 0.5f + 0.05f), true);
        }
    }
    placeModel("GlassVaseFlowers", Vector3(L.halfWidth - 0.17f, 1.177f, -1.20f), 0.0f);
    placeModel("radio", Vector3(L.halfWidth - 0.17f, 1.177f, -0.85f), 195.0f);
    placeModel("small-pictures", Vector3(L.halfWidth - 0.006f, 1.35f, 2.05f), 180.0f, 0.0f, false);
    {
        // A leather shoulder bag dropped by the door, leaning on the wall on
        // the chest's side of it: a soft body, a flap over its front, a brass
        // clasp and the strap slumped over its top.
        // Tilted about its back bottom edge, the body's base sunk 2 cm into
        // the floor so the raised front edge stays hidden, the way a soft bag sags.
        const float lean = MathHelper::ToRadians(9.0f);
        const Matrix world = Matrix::CreateTranslation(-0.055f, 0.0f, 0.0f) * Matrix::CreateRotationZ(-lean)
                             * Matrix::CreateTranslation(L.halfWidth - 0.062f, 0.0f, L.doorCentreZ - L.doorWidth * 0.5f - L.doorFrameWidth - 0.26f);
        MeshBuilder bag;
        bag.addBox(Vector3(-0.055f, -0.02f, -0.17f), Vector3(0.055f, 0.30f, 0.17f), 0.3f);          // the body
        bag.addBox(Vector3(-0.068f, 0.13f, -0.168f), Vector3(-0.054f, 0.31f, 0.168f), 0.3f);      // the flap, on the room side
        bag.addBox(Vector3(-0.045f, 0.30f, -0.165f), Vector3(0.045f, 0.318f, 0.165f), 0.3f);      // the top, under the flap's fold
        bag.addBox(Vector3(-0.03f, 0.318f, -0.20f), Vector3(0.0f, 0.34f, 0.20f), 0.3f);           // the strap lying across the top
        bag.addBox(Vector3(-0.075f, 0.16f, -0.21f), Vector3(-0.045f, 0.34f, -0.19f), 0.3f);      // its ends hanging down the front
        bag.addBox(Vector3(-0.075f, 0.16f, 0.19f), Vector3(-0.045f, 0.34f, 0.21f), 0.3f);
        place(bag.take(), "bag_leather", "door bag", world, true);
        MeshBuilder clasp;
        clasp.addBox(Vector3(-0.072f, 0.145f, -0.02f), Vector3(-0.066f, 0.175f, 0.02f), 0.2f);
        place(clasp.take(), "sconce_brass", "door bag clasp", world, false);
    }
    placeModel("ChairDamaskPurplegold", Vector3(2.55f, 0.0f, 1.85f), -130.0f);

    // --- the window wall (-Z): curtains and the pier between the windows ---
    for (int w = 0; w < L.windowCount; ++w)
    {
        const float cx = L.windowCentreX[w];
        const float rodY = L.windowTop() + 0.12f;
        placeModel("curtain-rod", Vector3(cx, rodY - 0.025f, -L.halfDepth + 0.09f), 0.0f);
        const float outer = w == 0 ? cx - L.windowWidth * 0.5f - 0.30f : cx + L.windowWidth * 0.5f + 0.30f;
        const std::size_t before = renderer_.itemCount();
        // Sheer panels: a thin voile lets the window show through faintly
        // (blended at 0.86). They cast no shadow: the caster pass has no
        // alpha (R-11) and would make each a solid slab against the sun,
        // while a voile passes most of the light.
        const ModelLibrary::MaterialTweak sheer = [](Material& m, int) {
            m.alphaMode = Microsoft::Xna::Framework::Graphics::AlphaModeEXT::Blend;
            m.alpha = 0.86f;
            m.doubleSided = true;
            m.roughness = std::max(m.roughness, 0.8f);
        };
        placeModel(w == 0 ? "curtain-panel-a" : "curtain-panel-b", Vector3(outer, rodY - 2.30f, -L.halfDepth + 0.09f), 0.0f, 0.0f, false,
                   sheer);
        for (std::size_t i = before; i < renderer_.itemCount(); ++i)
            curtains_.push_back(Curtain{i, renderer_.item(i).world, Vector3(outer, rodY - 0.025f, -L.halfDepth + 0.09f),
                                        static_cast<float>(w) * 1.9f});
    }
    placeModel("large-picture", Vector3(0.0f, 1.10f, -L.halfDepth + 0.03f), -90.0f, 0.0f, false);   // faces +X natively, like picture-medium
    // A low cabinet on the pier between the windows, with a candlestick, a
    // dish of olives and a picture leaning against the wall.
    placeModel("low-cabinet", Vector3(0.0f, 0.0f, -L.halfDepth + 0.17f), 90.0f);
    placeModel("candlestick", Vector3(-0.36f, 0.568f, -L.halfDepth + 0.17f), 0.0f);
    // A candle in the holder (the model is the steel alone), lit with the
    // lamps: a wax cylinder and a small flame card that flickers in update.
    {
        const Vector3 top(-0.36f, 0.568f + 0.61f - 0.02f, -L.halfDepth + 0.17f);
        MeshBuilder wax;
        wax.addCylinder(top, 0.012f, 0.10f, 16, 0.2f, true);
        place(wax.take(), "candle_wax", "candle");
        MeshBuilder card;
        const float w = 0.028f, h = 0.05f;
        card.addQuadUv(Vector3(-w * 0.5f, 0.0f, 0.0f), Vector3(w * 0.5f, 0.0f, 0.0f), Vector3(w * 0.5f, h, 0.0f), Vector3(-w * 0.5f, h, 0.0f));
        for (int i = 0; i < 2; ++i)
        {
            const Matrix base = Matrix::CreateRotationY(MathHelper::ToRadians(30.0f + static_cast<float>(i) * 90.0f))
                                * Matrix::CreateTranslation(top.X, top.Y + 0.10f - 0.004f, top.Z);
            fireCards_.push_back(FireCard{renderer_.itemCount(), base, 4.0f + static_cast<float>(i) * 1.3f, 1, 1.4f});
            MeshBuilder copy = card;
            place(copy.take(), "flame_candle", "candle-flame-" + std::to_string(i), base, false);
        }
    }
    placeModel("IridescentDishWithOlives", Vector3(0.12f, 0.568f, -L.halfDepth + 0.19f), 0.0f);
    placeModel("picture-medium", Vector3(0.36f, 0.568f, -L.halfDepth + 0.10f), -82.0f, 0.0f, false);   // its image faces +X natively
    // Art on the side walls: a landscape over the pedestal table, three
    // canvases by the chest of drawers, a mirror beside the fireplace.
    placeModel("painting-landscape", Vector3(-L.halfWidth + 0.012f, 1.55f, -1.35f), 90.0f, 0.0f, false);
    // The canvases (0.40 x 0.55 m on 25 mm stretchers, 5 mm off the wall,
    // a row over the chest of drawers): abstract colour fields baked once
    // each, on a quad a hair in front of the stretcher's face; the raw cloth
    // wraps the edges. (The bedroom's palette paintings they replace had no
    // image to show and read as black holes.)
    for (int i = 0; i < 3; ++i)
    {
        const float cz = -1.10f + static_cast<float>(i - 1) * 0.50f, cy = 1.78f;
        const float back = L.halfWidth - 0.005f, front = back - 0.025f;
        const float z0 = cz - 0.20f, z1 = cz + 0.20f, y0 = cy - 0.275f, y1 = cy + 0.275f;
        MeshBuilder stretcher;
        stretcher.addBox(Vector3(front, y0, z0), Vector3(back, y1, z1), 0.25f, 0x3Fu & ~1u);   // no face against the wall
        place(stretcher.take(), "canvas_cloth", "canvas-stretcher-" + std::to_string(i));
        // Seen from the room (looking +X) world +Z is on the right, so the
        // quad runs z0 -> z1 along its bottom for u to run left to right.
        MeshBuilder face;
        const float fx = front - 0.0005f;
        face.addQuadUv(Vector3(fx, y0, z0), Vector3(fx, y0, z1), Vector3(fx, y1, z1), Vector3(fx, y1, z0));
        place(face.take(), "canvas_" + std::to_string(i), "canvas-" + std::to_string(i), Matrix::getIdentityProperty(), false);
    }
    // A wall clock on the -X wall: a shallow black case, the cream dial (a
    // masked round quad) on its face, and two hands the scene's clock turns.
    {
        const float radius = 0.155f, caseDepth = 0.035f;
        const Vector3 centre(-L.halfWidth, 1.85f, 1.95f);   // beside the bookcase, toward the fireplace corner
        MeshBuilder body;
        body.addCylinder(Vector3::Zero, radius, caseDepth, 40, 0.5f, true);   // along local Y
        // Local +Y to world +X: the case stands off the wall.
        body.transform(Matrix::CreateRotationZ(-MathHelper::PiOver2) * Matrix::CreateTranslation(centre));
        place(body.take(), "clock_black", "clock-case");
        const float fx = centre.X + caseDepth + 0.001f;
        MeshBuilder dial;
        // Seen from the room (looking -X) world +Z is on the left: a runs from
        // the bottom-left (+Z) to the bottom-right (-Z), 12 at the top (+Y).
        dial.addQuadUv(Vector3(fx, centre.Y - radius, centre.Z + radius), Vector3(fx, centre.Y - radius, centre.Z - radius),
                       Vector3(fx, centre.Y + radius, centre.Z - radius), Vector3(fx, centre.Y + radius, centre.Z + radius));
        place(dial.take(), "clock_dial", "clock-dial", Matrix::getIdentityProperty(), false);
        // The hands, built pointing at 12 (+Y) about a pivot at the origin,
        // each on its own plane a few millimetres off the dial.
        const auto hand = [&](float length, float width, float lift, const char* name, std::size_t& index, Vector3& pivot) {
            MeshBuilder h;
            h.addBox(Vector3(0.0f, -0.025f, -width * 0.5f), Vector3(0.0035f, length, width * 0.5f), 0.5f);
            pivot = Vector3(fx + lift, centre.Y, centre.Z);
            index = renderer_.itemCount();
            place(h.take(), "clock_black", name, Matrix::CreateTranslation(pivot), false);
        };
        hand(0.085f, 0.012f, 0.004f, "clock-hour-hand", clockHourHand_, clockHourPivot_);
        hand(0.125f, 0.008f, 0.009f, "clock-minute-hand", clockMinuteHand_, clockMinutePivot_);
        setClockTime(10.0f + 10.0f / 60.0f);
    }
    placeModel("mirror", Vector3(-2.05f, 0.62f, L.halfDepth - 0.012f), 180.0f, 0.0f, false);
    {
        // The mirror's glass: a quad a hair in front of the model's plate
        // (local x +-0.365, y 0.075..1.402 inside the 0.88 x 1.48 m frame),
        // silvered: a constant 0.92 reflectance, no Fresnel ramp.
        Material glass;
        glass.name = "mirror_glass";
        glass.baseColour = Vector3(0.01f, 0.01f, 0.01f);
        glass.roughness = 0.05f;
        glass.metallic = 1.0f;
        glass.castsShadow = false;
        glass.reflectionPlane = tvWallReflection_;
        glass.reflectionF0 = 0.92f;
        glass.reflectionFresnel = false;
        glass.reflectionTint = Vector3(0.97f, 0.98f, 1.0f);
        const Material* mirrorGlass = materials_.add(glass);
        const float z = L.halfDepth - 0.012f - 0.004f;
        const float x0 = -2.05f - 0.355f, x1 = -2.05f + 0.355f, y0 = 0.62f + 0.085f, y1 = 0.62f + 1.392f;
        MeshBuilder quad;
        quad.addQuad(Vector3(x1, y0, z), Vector3(x0, y0, z), Vector3(x0, y1, z), Vector3(x1, y1, z), 1.0f);
        place(quad.take(), mirrorGlass->name, "mirror_glass", Matrix::getIdentityProperty(), false);
    }
    // A lantern on the hearth and a second floor lamp by the door.
    placeModel("Lantern", Vector3(0.78f, 0.0f, L.halfDepth - 0.55f), 20.0f, 0.32f);
    // The Khronos lamp carries a lit bulb in its emissive; it joins the room's
    // lamps instead of burning by day.
    std::vector<std::string> bulbMaterials;
    placeModel("LightsPunctualLamp", Vector3(2.62f, 0.0f, 0.25f), -125.0f, 0.0f, true,
               [&bulbMaterials](Material& m, int) {
                   if (m.emissiveFactor.LengthSquared() > 0.0f || m.emissive != nullptr)
                   {
                       bulbMaterials.push_back(m.name);
                       m.emissiveFactor = Vector3::Zero;
                   }
               });
    // A frosted bulb behind the shade's opening: ~5000 cd/m^2 (0.6), not the
    // 10 000 of a clear one, or its bloom disc swallows the lamp at night.
    for (const std::string& name : bulbMaterials) extraGlows_.push_back(Glow{name, Vector3(1.0f, 0.78f, 0.55f), 0.6f});
    placeModel("potted-plant-a", Vector3(-2.55f, 0.0f, -1.85f), 0.0f);
    placeModel("SheenChair", Vector3(2.35f, 0.0f, -1.65f), -140.0f);

    // --- the bookcase wall (-X): shelves loaded with books and objects ---
    const float bx = -L.halfWidth + 0.02f + 0.16f;  // shelf centre line
    const float shelfY[] = {0.102f, 0.522f, 0.942f, 1.362f, 1.782f};
    placeModel("books-row-a", Vector3(bx, shelfY[4], 0.05f), 0.0f);
    placeModel("books-row-a", Vector3(bx, shelfY[3], 0.95f), 0.0f);
    placeModel("books-row-b", Vector3(bx, shelfY[3], 0.15f), 0.0f);
    placeModel("books-row-a", Vector3(bx, shelfY[2], 1.05f), 0.0f);
    placeModel("books-row-b", Vector3(bx, shelfY[1], 1.10f), 0.0f);
    placeModel("storage-boxes", Vector3(bx, shelfY[0], 0.20f), 0.0f);
    placeModel("storage-boxes", Vector3(bx, shelfY[0], 1.10f), 0.0f);
    placeModel("book-closed", Vector3(bx, shelfY[2], 0.25f), 80.0f);
    placeModel("picture-frame-small", Vector3(bx, shelfY[1], 0.30f), 100.0f);
    placeModel("vase-decor", Vector3(bx, shelfY[4], 1.05f), 90.0f);
    placeModel("ToyCar", Vector3(bx, shelfY[2], 0.55f), 60.0f, 0.12f);
    placeModel("BoomBox", Vector3(bx, shelfY[1], 0.70f), 95.0f, 0.22f);
    placeModel("teapot", Vector3(bx, shelfY[3], 0.55f), 90.0f);
    placeModel("ClearcoatWicker", Vector3(bx, shelfY[2], 1.35f), 0.0f, 0.22f);

    // --- the ceiling ---
    // The paper globe is a blended material in its source scene, which let
    // the wall's picture show through it; to the eye a paper lantern is
    // opaque, lit or not (its bulb shows only as the glow), so it draws opaque.
    placeModel("ceiling-lamp", Vector3(0.0f, H - 1.065f, 0.0f), 0.0f, 0.0f, true, [](Material& m, int) {
        if (m.alphaMode == Microsoft::Xna::Framework::Graphics::AlphaModeEXT::Blend)
        {
            m.alphaMode = Microsoft::Xna::Framework::Graphics::AlphaModeEXT::Opaque;
            m.alpha = 1.0f;
            m.writesDepth = true;      // the import cleared it with the blend
            m.castsShadow = false;     // it wraps the pendant's own bulb
        }
    });
}

void RoomScene::buildLamps()
{
    const RoomLayout& L = layout_;
    const Vector3 warm(1.0f, 0.78f, 0.55f);   // ~2700 K
    std::vector<Lamp> lamps;
    const auto add = [&](const std::string& name, const Vector3& position, float lumens, const Vector3& colour,
                         float range, bool shadow) {
        Lamp lamp;
        lamp.name = name;
        lamp.position = position;
        lamp.colour = colour;
        lamp.intensity = 0.0f;
        lamp.fullIntensity = Lamp::fromLumens(lumens);
        lamp.range = range;
        lamp.castsShadow = shadow;
        lamps.push_back(lamp);
    };
    add("pendant", Vector3(0.0f, L.ceilingHeight - 1.065f + 0.31f, 0.0f), 1600.0f, warm, 9.0f, true);
    add("floor lamp", Vector3(1.35f, 1.40f, -1.55f), 800.0f, warm, 7.0f, false);
    add("table lamp", Vector3(-2.35f, 0.632f + 0.30f, -1.20f), 400.0f, warm, 6.0f, false);
    add("sconce left", Vector3(-1.10f, 1.84f, L.halfDepth - 0.105f), 300.0f, warm, 5.0f, false);
    {
        // The hall's dome light, always on: it has no window of its own.
        Lamp hall;
        hall.name = "hall";
        hall.position = Vector3(L.halfWidth + L.interiorWallThickness + 0.60f, L.ceilingHeight - 0.12f, L.doorCentreZ);
        hall.colour = warm;
        hall.fullIntensity = Lamp::fromLumens(600.0f);
        hall.intensity = hall.fullIntensity;
        hall.range = 5.0f;
        hall.on = true;
        lamps.push_back(hall);
    }
    add("sconce right", Vector3(1.10f, 1.84f, L.halfDepth - 0.105f), 300.0f, warm, 5.0f, false);
    add("reading lamp", Vector3(2.55f, 1.55f, 0.32f), 350.0f, warm, 6.0f, false);
    // The stove's fire: a few hundred lumens of orange, flickering (update).
    add("stove", Vector3(0.0f, 0.50f, L.halfDepth - 0.34f + 0.02f), 260.0f, Vector3(1.0f, 0.48f, 0.14f), 4.5f, false);
    add("candle", Vector3(-0.36f, 0.568f + 0.61f + 0.10f, -L.halfDepth + 0.17f), 12.0f, Vector3(1.0f, 0.62f, 0.28f), 2.0f, false);
    // Street lights: 100 W LED luminaires, 3000 K. A real one is ~12 000 lm;
    // three times that is used so the street reads next to the lamp-lit room
    // under a single global tonemapper (the eye adapts locally, ACES does not).
    // They share the lamp switch until the time-of-day system schedules them (M5).
    const Vector3 streetWhite(1.0f, 0.86f, 0.68f);
    int streetIndex = 0;
    for (const Vector3& p : streetLampPositions_)
        add("street " + std::to_string(streetIndex++), p, 36000.0f, streetWhite, 26.0f, false);
    // The windows as light sources: the probes hold the sky's light as one
    // level across the room, so a surface by the window was lit no more than
    // the far wall. A wide spot at each window's centre carries the sky's
    // diffuse light in with the inverse-square fall-off (its intensity from
    // the sky each frame, updateWindowLights), pointing a little downward.
    for (int w = 0; w < L.windowCount; ++w)
    {
        Lamp window;
        window.name = "window " + std::to_string(w);
        window.position = Vector3(L.windowCentreX[w], L.windowSillHeight + L.windowHeight * 0.5f, -L.halfDepth + 0.08f);
        window.colour = Vector3(0.8f, 0.9f, 1.0f);
        window.intensity = 0.0f;
        window.fullIntensity = 0.0f;
        window.range = 9.0f;
        window.spot = true;
        window.direction = Vector3::Normalize(Vector3(0.0f, -0.18f, 1.0f));
        // Near the hemisphere a Lambertian opening emits into: full to 52
        // degrees off the axis, gone at 89 (a cosine-like roll-off between).
        window.innerAngle = 0.90f;
        window.outerAngle = 1.55f;
        window.daylightPortal = true;
        lamps.push_back(window);
    }
    {
        Lamp tv;
        tv.name = "television";
        tv.position = Vector3(0.0f, 1.30f + 0.37f, L.halfDepth - 0.06f);
        tv.colour = Vector3(0.75f, 0.85f, 1.0f);
        tv.intensity = Lamp::fromLumens(150.0f);   // the dark-room picture level (120 cd/m^2 over the screen)
        tv.fullIntensity = tv.intensity;
        tv.range = 5.0f;
        tv.spot = true;
        tv.direction = Vector3(0.0f, -0.25f, -1.0f);
        tv.innerAngle = 0.9f;
        tv.outerAngle = 1.35f;
        lamps.push_back(tv);
    }
    renderer_.setLamps(std::move(lamps));
    // The sunbeams' medium: the room's air, wall to wall and floor to ceiling.
    renderer_.setSunbeamVolume(Vector3(-L.halfWidth, 0.0f, -L.halfDepth), Vector3(L.halfWidth, L.ceilingHeight, L.halfDepth));

    // Shades and bulbs that glow with their lamp (material names are
    // "<model>.<part index>", parts in the extracted node order).
    // Radiance in scene units (1.0 ~ 8000 cd/m^2), from each shade's lumens
    // over its area: the 1600 lm paper globe (1.4 m^2) ~400 cd/m^2 = 0.05,
    // the 800 lm fabric drum (0.4 m^2) ~640 = 0.08, the 400 lm table shade
    // (0.15 m^2) ~850 = 0.1, the small 300 lm sconces ~1900 = 0.24, a bare
    // bulb ~10 000 = 1.2, a television ~200 cd/m^2.
    glows_ = {
        {"floor-lamp.1", warm, 0.10f}, {"floor-lamp.2", warm, 0.05f},
        {"ceiling-lamp.4", warm, 0.06f},
        {"sconce_linen", warm, 0.16f}, {"sconce_bulb", warm, 1.2f},
        {"IridescenceLamp.1", warm, 0.12f}, {"IridescenceLamp.2", warm, 0.04f},
        {"tv.1", Vector3(0.6f, 0.7f, 0.9f), 0.004f, true},
        // Outside: luminaire diffusers (~10 000 cd/m^2) and lit windows (~80 cd/m^2).
        {"street_lamp_head", Vector3(1.0f, 0.88f, 0.70f), 1.3f, false, true},
        {"shop_sign", Vector3(1.0f, 0.97f, 0.90f), 0.25f, false, true},      // a lit sign box, ~2000 cd/m^2
        {"timetable", Vector3(1.0f, 1.0f, 1.0f), 0.12f, false, true},        // the bus stop's lit case
        {"facade_window_lit", Vector3(1.0f, 0.80f, 0.55f), 0.008f, false, true},
        {"facade_window_lit_dim", Vector3(1.0f, 0.72f, 0.45f), 0.0025f, false, true},    // behind drawn curtains
        {"facade_window_lit_cool", Vector3(0.65f, 0.78f, 1.0f), 0.006f, false, true},   // a television's flicker-blue
    };
    glows_.insert(glows_.end(), extraGlows_.begin(), extraGlows_.end());
    for (const Glow& glow : glows_)
        if (materials_.find(glow.material) == nullptr)
            CNA::Logger::Warn("cna-room: glow material '" + glow.material + "' not found");
    setLampsOn(false);
    setStreetLightsOn(false);
    setTelevisionOn(false);
    applyLampLevels();
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
            CNA::Logger::Info("cna-room: chimney smoke: " + std::to_string(plumes.size()) + " plumes of " + std::to_string(chimneyTops_.size()) + " chimneys at "
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
        CNA::Logger::Info("cna-room: window lights " + std::to_string(intensity) + " (sky " + std::to_string(sky.X) + "," + std::to_string(sky.Y) + "," + std::to_string(sky.Z) + ")");
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
            CNA::Logger::Info("cna-room: television picture mean " + std::to_string(mean.X) + " " + std::to_string(mean.Y) + " " + std::to_string(mean.Z)
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
