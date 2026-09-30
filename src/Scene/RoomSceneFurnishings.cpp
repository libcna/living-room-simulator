// SPDX-License-Identifier: MIT
// Furniture, props and lamp placement for RoomScene.
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
constexpr unsigned kFacePosY = 1u << 2;
constexpr unsigned kFaceNegY = 1u << 3;
constexpr unsigned kAllFaces = 0x3F;
}  // namespace

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
            CNA::Logger::Info("living-room-simulator: sofa front z " + std::to_string(front) + ", slippers at z " + std::to_string(front + 0.16f));
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
            CNA::Logger::Info("living-room-simulator: armchair seat top " + std::to_string(seatTop) + " at " + std::to_string(seatCentre.X) + ", " + std::to_string(seatCentre.Z)
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
            CNA::Logger::Info("living-room-simulator: steam over the cup at " + std::to_string(centre.X) + "," + std::to_string(cup.Max.Y) + "," + std::to_string(centre.Z));
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
        // No reflectionPlane on "tv_content": PlanarReflection::drawSurface() adds the mirrored
        // room in unconditionally before the picture's own emissive term (reflection * uTint * f,
        // Fresnel-weighted by reflectionF0), and the captured room routinely holds a lamp or window
        // at a radiance high enough that even a bare-glass-low F0 (0.01) still outshines the
        // picture's deliberately dim 0.017 emissive (a dark-room picture level) once tonemapped --
        // confirmed by lowering reflectionF0 5x and separately raising the material's roughness
        // with no visible change either way. This never showed up before the "tv" furniture model
        // existed to actually populate assets/external/extracted/tv.glb: with no model, nothing
        // sat behind this quad to be missing, and the room had never rendered "tv_content" against
        // a real, occupied reflection plane. A real panel's anti-glare coating suppresses this kind
        // of mirror-bright glare far more aggressively than any F0/roughness tweak here reproduced;
        // dropping the reflection is the simple fix until that shader gains its own tone response.
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
            CNA::Logger::Info("living-room-simulator: chest top " + std::to_string(top) + " x " + std::to_string(chest.Min.X) + ".." + std::to_string(chest.Max.X) + " z "
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
            CNA::Logger::Warn("living-room-simulator: glow material '" + glow.material + "' not found");
    setLampsOn(false);
    setStreetLightsOn(false);
    setTelevisionOn(false);
    applyLampLevels();
}

}  // namespace CnaRoom
