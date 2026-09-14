// SPDX-License-Identifier: MIT
#include "CnaRoom/Render/MaterialLibrary.hpp"

#include "CnaRoom/Assets/TextureBaker.hpp"

#include "CNA/Logger.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "System/IO/FileAccess.hpp"
#include "System/IO/FileMode.hpp"
#include "System/IO/FileStream.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CnaRoom::Assets::SurfaceImages;
using CnaRoom::Assets::TextureBaker;

namespace CnaRoom {

struct SurfaceImagesRef
{
    const SurfaceImages& images;
};

MaterialLibrary::MaterialLibrary(GraphicsDevice& device) : device_(device) {}

MaterialLibrary::~MaterialLibrary() = default;

Texture2D* MaterialLibrary::own(std::unique_ptr<Texture2D> texture)
{
    Texture2D* raw = texture.get();
    if (raw != nullptr)
        textureBytes_ += static_cast<std::size_t>(raw->getWidthProperty())
                         * static_cast<std::size_t>(raw->getHeightProperty()) * 4u * 4u / 3u;
    textures_.push_back(std::move(texture));
    return raw;
}

MaterialLibrary::Surface MaterialLibrary::uploadSurface(const SurfaceImagesRef& ref)
{
    Surface s;
    // Albedo alpha below 128 anywhere means a masked surface: keep its coverage per mip.
    bool masked = false;
    for (std::size_t i = 3; i < ref.images.albedo.rgba.size() && !masked; i += 4) masked = ref.images.albedo.rgba[i] < 128;
    s.albedo = own(TextureBaker::upload(device_, ref.images.albedo, true, masked ? 0.5f : -1.0f));
    s.normal = own(TextureBaker::upload(device_, ref.images.normal, false));
    s.orm = own(TextureBaker::upload(device_, ref.images.orm, false));
    s.tileMetres = ref.images.tileMetres;
    return s;
}

const Material* MaterialLibrary::add(Material material)
{
    auto owned = std::make_unique<Material>(std::move(material));
    Material* raw = owned.get();
    materials_[raw->name] = std::move(owned);
    return raw;
}

const Material& MaterialLibrary::get(const std::string& name) const
{
    const Material* found = find(name);
    if (found == nullptr) throw std::out_of_range("MaterialLibrary: no material named '" + name + "'");
    return *found;
}

Material* MaterialLibrary::edit(const std::string& name)
{
    const auto it = materials_.find(name);
    return it == materials_.end() ? nullptr : it->second.get();
}

const Material* MaterialLibrary::find(const std::string& name) const
{
    const auto it = materials_.find(name);
    return it == materials_.end() ? nullptr : it->second.get();
}

namespace {

constexpr int kCacheVersion = 7;   // bump when any baker's output changes

bool readImage(GraphicsDevice& device, const std::string& path, CnaRoom::Assets::Image& out)
{
    if (!std::filesystem::exists(path)) return false;
    System::IO::FileStream stream(path, System::IO::FileMode::Open, System::IO::FileAccess::Read);
    Texture2D texture = Texture2D::FromStream(device, stream);
    const int w = texture.getWidthProperty(), h = texture.getHeightProperty();
    if (w <= 0 || h <= 0) return false;
    std::vector<Color> pixels(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    texture.GetData(pixels.data(), static_cast<int>(pixels.size()));
    out = CnaRoom::Assets::Image(w, h);
    for (std::size_t i = 0; i < pixels.size(); ++i)
    {
        out.rgba[i * 4] = pixels[i].getRProperty();
        out.rgba[i * 4 + 1] = pixels[i].getGProperty();
        out.rgba[i * 4 + 2] = pixels[i].getBProperty();
        out.rgba[i * 4 + 3] = pixels[i].getAProperty();
    }
    return true;
}

void writeImage(GraphicsDevice& device, const std::string& path, const CnaRoom::Assets::Image& image)
{
    Texture2D texture = Texture2D::CreateFromPixels(device, image.width, image.height, image.rgba);
    texture.SaveAsPng(path);
}

}  // namespace

SurfaceImages MaterialLibrary::cachedBake(const std::string& name, std::uint32_t seed, int size,
                                          const std::function<SurfaceImages()>& bake)
{
    if (cacheDirectory_.empty()) return bake();
    const std::string stem = cacheDirectory_ + "/" + name + "-" + std::to_string(seed) + "-" + std::to_string(size)
                             + "-v" + std::to_string(kCacheVersion);
    const std::string header = stem + ".txt";
    try
    {
        if (std::filesystem::exists(header))
        {
            std::ifstream in(header);
            float tile = 1.0f;
            in >> tile;
            SurfaceImages images;
            if (in && readImage(device_, stem + "-albedo.png", images.albedo) && readImage(device_, stem + "-normal.png", images.normal)
                && readImage(device_, stem + "-orm.png", images.orm))
            {
                images.tileMetres = tile;
                ++cacheHits_;
                return images;
            }
        }
    }
    catch (const std::exception& failure)
    {
        CNA::Logger::Warn("cna-room: texture cache read failed for " + name + ": " + failure.what());
    }
    SurfaceImages images = bake();
    try
    {
        std::filesystem::create_directories(cacheDirectory_);
        writeImage(device_, stem + "-albedo.png", images.albedo);
        writeImage(device_, stem + "-normal.png", images.normal);
        writeImage(device_, stem + "-orm.png", images.orm);
        std::ofstream out(header);
        out << images.tileMetres << "\n";
    }
    catch (const std::exception& failure)
    {
        CNA::Logger::Warn("cna-room: texture cache write failed for " + name + ": " + failure.what());
    }
    return images;
}

void MaterialLibrary::buildProcedural(std::uint32_t seed, int size)
{
    const auto surfaced = [&](const std::string& name, const std::function<SurfaceImages()>& bake, float roughness,
                              float metallic, Vector3 base = Vector3(1.0f, 1.0f, 1.0f)) {
        const SurfaceImages images = cachedBake(name, seed, size, bake);
        const Surface s = uploadSurface(SurfaceImagesRef{images});
        Material m;
        m.name = name;
        m.albedo = s.albedo;
        m.normal = s.normal;
        m.orm = s.orm;
        m.ormHasOcclusion = true;
        m.baseColour = base;
        // The ORM map carries the per-texel roughness; the factor multiplies it.
        m.roughness = roughness;
        {
            double sum = 0.0;
            std::size_t n = 0;
            for (std::size_t i = 1; i < images.orm.rgba.size(); i += 4, ++n) sum += images.orm.rgba[i];
            m.roughnessMapMean = n > 0 ? static_cast<float>(sum / static_cast<double>(n) / 255.0) : 1.0f;
        }
        m.metallic = metallic;
        m.uvScale = Vector2(1.0f, 1.0f);
        return add(std::move(m));
    };
    // uvMetres in the mesh builder equals tileMetres here, so one repeat is
    // one physical tile and the factor stays 1.

    surfaced("plaster_warm_white", [&] { return TextureBaker::plaster(size, seed + 1u, 0.93f, 0.90f, 0.85f); }, 1.0f, 0.0f);
    surfaced("plaster_ceiling", [&] { return TextureBaker::plaster(size, seed + 2u, 0.96f, 0.955f, 0.94f); }, 1.0f, 0.0f);
    surfaced("plaster_accent", [&] { return TextureBaker::plaster(size, seed + 3u, 0.46f, 0.52f, 0.50f); }, 1.0f, 0.0f);
    surfaced("oak_floor", [&] { return TextureBaker::oakPlanks(size, seed + 4u); }, 1.0f, 0.0f);
    surfaced("rug_wool", [&] { return TextureBaker::carpet(size, seed + 5u, 0.62f, 0.55f, 0.47f); }, 1.0f, 0.0f);
    surfaced("fabric_linen", [&] { return TextureBaker::fabricWeave(size / 2, seed + 6u, 0.72f, 0.68f, 0.60f); }, 1.0f, 0.0f);
    surfaced("fabric_curtain", [&] { return TextureBaker::fabricWeave(size / 2, seed + 7u, 0.82f, 0.80f, 0.74f); }, 1.0f, 0.0f);
    surfaced("paint_white_trim", [&] { return TextureBaker::paintedWood(size / 2, seed + 8u, 0.95f, 0.95f, 0.93f, 0.42f); }, 1.0f, 0.0f);
    surfaced("paint_door", [&] { return TextureBaker::paintedWood(size / 2, seed + 9u, 0.93f, 0.93f, 0.91f, 0.38f); }, 1.0f, 0.0f);
    surfaced("concrete", [&] { return TextureBaker::concrete(size, seed + 10u); }, 1.0f, 0.0f);
    surfaced("wood_walnut", [&] { return TextureBaker::woodGrain(size / 2, seed + 23u, 0.30f, 0.19f, 0.12f, 0.42f); }, 1.0f, 0.0f);
    surfaced("wood_light", [&] { return TextureBaker::woodGrain(size / 2, seed + 24u, 0.66f, 0.52f, 0.36f, 0.45f); }, 1.0f, 0.0f);
    surfaced("brick_red", [&] { return TextureBaker::brick(size, seed + 11u); }, 1.0f, 0.0f);
    surfaced("asphalt", [&] { return TextureBaker::asphalt(size, seed + 12u); }, 1.0f, 0.0f);
    surfaced("grass", [&] { return TextureBaker::grass(size, seed + 13u); }, 1.0f, 0.0f);
    surfaced("plastic_white", [&] { return TextureBaker::flat(size / 4, seed + 14u, 0.92f, 0.92f, 0.90f, 0.45f, 0.0f, 0.04f); }, 1.0f, 0.0f);
    surfaced("plastic_black", [&] { return TextureBaker::flat(size / 4, seed + 15u, 0.03f, 0.03f, 0.03f, 0.55f, 0.0f, 0.06f); }, 1.0f, 0.0f);
    surfaced("metal_brushed", [&] { return TextureBaker::flat(size / 4, seed + 16u, 0.83f, 0.82f, 0.80f, 0.40f, 1.0f, 0.10f); }, 1.0f, 1.0f);
    surfaced("metal_polished", [&] { return TextureBaker::flat(size / 4, seed + 17u, 0.90f, 0.89f, 0.87f, 0.12f, 1.0f, 0.04f); }, 1.0f, 1.0f);
    surfaced("radiator_enamel", [&] { return TextureBaker::flat(size / 4, seed + 18u, 0.94f, 0.94f, 0.92f, 0.35f, 0.0f, 0.05f); }, 1.0f, 0.0f);
    surfaced("ceramic_white", [&] { return TextureBaker::flat(size / 4, seed + 19u, 0.95f, 0.95f, 0.94f, 0.15f, 0.0f, 0.02f); }, 1.0f, 0.0f);
    surfaced("paper", [&] { return TextureBaker::flat(size / 4, seed + 20u, 0.90f, 0.88f, 0.82f, 0.90f, 0.0f, 0.08f); }, 1.0f, 0.0f);
    // Lived-in props: a knitted throw in ochre wool, a paperback's cloth cover.
    surfaced("throw_knit", [&] { return TextureBaker::knit(size / 2, seed + 21u, 0.74f, 0.68f, 0.58f); }, 1.0f, 0.0f);
    if (Material* m = edit("throw_knit")) m->doubleSided = true;
    surfaced("book_cover", [&] { return TextureBaker::flat(size / 8, seed + 22u, 0.16f, 0.30f, 0.34f, 0.75f, 0.0f, 0.05f); }, 1.0f, 0.0f);
    // The hall: a navy wool coat, a frosted dome that stays lit (~1000 cd/m^2 over its face).
    surfaced("coat_wool", [&] { return TextureBaker::fabricWeave(size / 2, seed + 23u, 0.13f, 0.15f, 0.24f); }, 1.0f, 0.0f);
    surfaced("hall_dome", [&] { return TextureBaker::flat(size / 8, seed + 24u, 0.95f, 0.93f, 0.88f, 0.45f, 0.0f, 0.0f); }, 1.0f, 0.0f);
    if (Material* m = edit("hall_dome")) m->emissiveFactor = Vector3(1.0f, 0.86f, 0.68f) * 0.13f;
    surfaced("facade_render", [&] { return TextureBaker::plaster(size, seed + 21u, 0.80f, 0.74f, 0.62f); }, 1.0f, 0.0f);
    surfaced("facade_render_2", [&] { return TextureBaker::plaster(size, seed + 22u, 0.70f, 0.72f, 0.70f); }, 1.0f, 0.0f);
    surfaced("facade_render_3", [&] { return TextureBaker::plaster(size, seed + 25u, 0.78f, 0.62f, 0.42f); }, 1.0f, 0.0f);
    surfaced("facade_render_4", [&] { return TextureBaker::plaster(size, seed + 26u, 0.86f, 0.86f, 0.84f); }, 1.0f, 0.0f);
    surfaced("paving", [&] { return TextureBaker::pavingSlabs(size, seed + 27u); }, 1.0f, 0.0f);
    surfaced("roof_tiles", [&] { return TextureBaker::roofTiles(size, seed + 28u, 0.55f, 0.30f, 0.20f); }, 1.0f, 0.0f);
    surfaced("roof_slate", [&] { return TextureBaker::roofTiles(size, seed + 29u, 0.30f, 0.31f, 0.34f); }, 1.0f, 0.0f);
    surfaced("bark", [&] { return TextureBaker::bark(size / 2, seed + 30u); }, 1.0f, 0.0f);
    surfaced("foliage", [&] { return TextureBaker::foliage(size / 2, seed + 31u, 0.75f); }, 1.0f, 0.0f);
    if (Material* m = edit("foliage"))
    {
        m->alphaMode = AlphaModeEXT::Mask;
        m->alphaCutoff = 0.5f;
        m->doubleSided = false;   // the far side of a canopy blob is the next blob, not this one's back faces
    }
    surfaced("hedge", [&] { return TextureBaker::foliage(size / 2, seed + 32u, 1.0f); }, 1.0f, 0.0f, Vector3(0.7f, 0.8f, 0.6f));
    // The hedge's fringe: masked leaf clumps along the box's edges, so the
    // clipped block reads as a bush.
    surfaced("hedge_fringe", [&] { return TextureBaker::foliage(size / 2, seed + 43u, 0.7f); }, 1.0f, 0.0f, Vector3(0.7f, 0.8f, 0.6f));
    if (Material* m = edit("hedge_fringe"))
    {
        m->alphaMode = AlphaModeEXT::Mask;
        m->alphaCutoff = 0.5f;
        m->doubleSided = true;
    }
    surfaced("road_paint", [&] { return TextureBaker::flat(size / 4, seed + 33u, 0.85f, 0.85f, 0.80f, 0.7f, 0.0f, 0.10f); }, 1.0f, 0.0f);
    surfaced("metal_paint_dark", [&] { return TextureBaker::flat(size / 4, seed + 34u, 0.12f, 0.13f, 0.14f, 0.45f, 0.0f, 0.05f); }, 1.0f, 0.0f);
    // The facade windows show a room behind their glass (curtains, a lamp,
    // a television, or dark), one bake per kind spanning each window once;
    // the lit kinds glow through the same image at night.
    surfaced("facade_window", [&] { return TextureBaker::windowInterior(size / 4, seed + 135u, 3); }, 1.0f, 0.0f);
    // The lit kinds carry two bakes: the daytime albedo (a dark room behind its
    // curtains) and the night emissive (the same room lit by its lamp).
    struct LitWindow { const char* name; std::uint32_t seed; int kind; };
    for (const LitWindow& w : {LitWindow{"facade_window_lit", 136u, 0}, LitWindow{"facade_window_lit_dim", 142u, 1},
                               LitWindow{"facade_window_lit_cool", 143u, 2}})
    {
        surfaced(w.name, [&] { return TextureBaker::windowInterior(size / 4, seed + w.seed, w.kind, false); }, 1.0f, 0.0f);
        const SurfaceImages glow = cachedBake(std::string(w.name) + "_glow", seed, size,
                                              [&] { return TextureBaker::windowInterior(size / 4, seed + w.seed, w.kind, true); });
        if (Material* m = edit(w.name)) m->emissive = own(TextureBaker::upload(device_, glow.albedo, true));
    }
    // Three abstract canvases by the chest of drawers, and the raw cloth that
    // wraps their stretchers.
    for (int i = 0; i < 3; ++i)
        surfaced("canvas_" + std::to_string(i), [&] { return TextureBaker::canvas(size / 2, seed + 150u + static_cast<std::uint32_t>(i), i); }, 1.0f, 0.0f);
    surfaced("canvas_cloth", [&] { return TextureBaker::fabricWeave(size / 4, seed + 153u, 0.80f, 0.76f, 0.66f); }, 1.0f, 0.0f);
    // The stove's fire: the ember bed and three flame cards, each its own image
    // as the emissive map; the flames blend by their alpha and light nothing else.
    surfaced("ember_bed", [&] { return TextureBaker::embers(size / 2, seed + 170u); }, 1.0f, 0.0f);
    if (Material* m = edit("ember_bed")) m->emissive = m->albedo;
    for (int i = 0; i < 3; ++i)
    {
        const std::string name = "flame_" + std::to_string(i);
        surfaced(name, [&] { return TextureBaker::flame(size / 2, seed + 171u + static_cast<std::uint32_t>(i)); }, 1.0f, 0.0f);
        if (Material* m = edit(name))
        {
            m->emissive = m->albedo;
            m->baseColour = Vector3::Zero;
            m->alphaMode = AlphaModeEXT::Blend;
            m->doubleSided = true;
            m->castsShadow = false;
            m->writesDepth = false;
            m->sunlit = false;
        }
    }
    // The candle: its wax, and a flame of its own (a smaller card, brighter).
    surfaced("candle_wax", [&] { return TextureBaker::flat(size / 4, seed + 175u, 0.92f, 0.90f, 0.84f, 0.55f, 0.0f, 0.03f); }, 1.0f, 0.0f);
    // The wall lights: brushed brass, a linen drum (lit from both sides), a frosted bulb.
    surfaced("sconce_brass", [&] { return TextureBaker::flat(size / 4, seed + 180u, 0.86f, 0.68f, 0.40f, 0.35f, 1.0f, 0.02f); }, 1.0f, 1.0f);
    surfaced("sconce_linen", [&] { return TextureBaker::flat(size / 4, seed + 181u, 0.90f, 0.85f, 0.74f, 0.85f, 0.0f, 0.08f); }, 1.0f, 0.0f);
    if (Material* m = edit("sconce_linen")) m->doubleSided = true;
    surfaced("sconce_bulb", [&] { return TextureBaker::flat(size / 8, seed + 182u, 0.95f, 0.92f, 0.85f, 0.40f, 0.0f, 0.0f); }, 1.0f, 0.0f);
    surfaced("flame_candle", [&] { return TextureBaker::flame(size / 2, seed + 174u); }, 1.0f, 0.0f);
    if (Material* m = edit("flame_candle"))
    {
        m->emissive = m->albedo;
        m->baseColour = Vector3::Zero;
        m->alphaMode = AlphaModeEXT::Blend;
        m->doubleSided = true;
        m->castsShadow = false;
        m->writesDepth = false;
        m->sunlit = false;
    }
    // The wall clock: a round dial (masked quad) in a black case with black hands.
    surfaced("clock_dial", [&] { return TextureBaker::clockDial(size / 2, seed + 160u); }, 1.0f, 0.0f);
    if (Material* m = edit("clock_dial"))
    {
        m->alphaMode = AlphaModeEXT::Mask;
        m->alphaCutoff = 0.5f;
    }
    surfaced("clock_black", [&] { return TextureBaker::flat(size / 4, seed + 161u, 0.05f, 0.05f, 0.055f, 0.45f, 0.0f, 0.02f); }, 1.0f, 0.0f);
    surfaced("street_lamp_head", [&] { return TextureBaker::flat(size / 4, seed + 37u, 0.85f, 0.85f, 0.82f, 0.30f, 0.0f, 0.02f); }, 1.0f, 0.0f);
    surfaced("facade_distant", [&] { return TextureBaker::flat(size / 4, seed + 38u, 0.52f, 0.54f, 0.58f, 0.85f, 0.0f, 0.06f); }, 1.0f, 0.0f);
    surfaced("door_dark", [&] { return TextureBaker::paintedWood(size / 2, seed + 39u, 0.16f, 0.18f, 0.22f, 0.35f); }, 1.0f, 0.0f);
    surfaced("gutter_metal", [&] { return TextureBaker::flat(size / 4, seed + 176u, 0.30f, 0.31f, 0.32f, 0.45f, 0.0f, 0.04f); }, 1.0f, 0.0f);
    // Parked cars: two paints (a metallic clear coat's gloss), tinted glass, chrome trim.
    // The paints keep some roughness: a bonnet seen from above at a grazing angle
    // mirrors the whole sky otherwise and reads as a white panel under cloud.
    surfaced("car_paint_blue", [&] { return TextureBaker::flat(size / 4, seed + 180u, 0.09f, 0.13f, 0.30f, 0.40f, 0.5f, 0.02f); }, 1.0f, 0.5f);
    surfaced("car_paint_silver", [&] { return TextureBaker::flat(size / 4, seed + 181u, 0.60f, 0.61f, 0.63f, 0.38f, 0.6f, 0.02f); }, 1.0f, 0.6f);
    surfaced("car_paint_red", [&] { return TextureBaker::flat(size / 4, seed + 182u, 0.42f, 0.06f, 0.05f, 0.40f, 0.5f, 0.02f); }, 1.0f, 0.5f);
    surfaced("car_glass", [&] { return TextureBaker::flat(size / 4, seed + 183u, 0.05f, 0.06f, 0.08f, 0.12f, 0.0f, 0.01f); }, 1.0f, 0.0f);
    surfaced("car_chrome", [&] { return TextureBaker::flat(size / 4, seed + 184u, 0.75f, 0.76f, 0.78f, 0.22f, 0.9f, 0.02f); }, 1.0f, 0.9f);
    // Exterior black rubber and painted bench wood of their own, so the weather can
    // snow them over without touching the wall clock's case or the terrace's doors.
    surfaced("rubber_black", [&] { return TextureBaker::flat(size / 4, seed + 185u, 0.03f, 0.03f, 0.035f, 0.62f, 0.0f, 0.03f); }, 1.0f, 0.0f);
    surfaced("bench_wood", [&] { return TextureBaker::paintedWood(size / 2, seed + 186u, 0.17f, 0.22f, 0.19f, 0.5f); }, 1.0f, 0.0f);

    raindrops_ = own(TextureBaker::upload(device_, TextureBaker::raindrops(std::max(64, size / 2), seed + 40u), false));
    {
        const Surface s = uploadSurface(SurfaceImagesRef{cachedBake("snow", seed, size, [&] { return TextureBaker::snow(size / 2, seed + 41u); })});
        snow_.albedo = s.albedo;
        snow_.normal = s.normal;
        snow_.orm = s.orm;
        const Surface f = uploadSurface(SurfaceImagesRef{cachedBake("foliage_snow", seed, size, [&] { return TextureBaker::foliage(size / 2, seed + 31u, 0.75f, 1.0f); })});
        foliageSnow_.albedo = f.albedo;
        foliageSnow_.normal = f.normal;
        foliageSnow_.orm = f.orm;
        const Surface h = uploadSurface(SurfaceImagesRef{cachedBake("hedge_snow", seed, size, [&] { return TextureBaker::foliage(size / 2, seed + 32u, 1.0f, 1.0f); })});
        hedgeSnow_.albedo = h.albedo;
        hedgeSnow_.normal = h.normal;
        hedgeSnow_.orm = h.orm;
        const Surface hf = uploadSurface(SurfaceImagesRef{cachedBake("hedge_fringe_snow", seed, size, [&] { return TextureBaker::foliage(size / 2, seed + 43u, 0.7f, 1.0f); })});
        hedgeFringeSnow_.albedo = hf.albedo;
        hedgeFringeSnow_.normal = hf.normal;
        hedgeFringeSnow_.orm = hf.orm;
    }
    {
        Material glass;
        glass.name = "window_glass";
        // Drawn premultiplied: the pane adds its Fresnel reflection to what is
        // seen through it and lets (1 - alpha) of the background through; a
        // black base colour leaves no diffuse term, so alpha is only the
        // slight tint of the glass.
        glass.baseColour = Vector3(0.0f, 0.0f, 0.0f);
        glass.alpha = 0.04f;
        glass.roughness = 0.03f;
        glass.reflectiveBlend = true;
        glass.metallic = 0.0f;
        glass.alphaMode = AlphaModeEXT::Blend;
        glass.doubleSided = true;
        glass.castsShadow = false;
        glass.writesDepth = false;
        add(std::move(glass));
    }
    {
        Material screen;
        screen.name = "tv_screen_off";
        screen.baseColour = Vector3(0.02f, 0.02f, 0.025f);
        screen.roughness = 0.08f;
        screen.metallic = 0.0f;
        add(std::move(screen));
    }
    {
        // The picture: a black glossy pane whose emissive map is the television's render target.
        Material content;
        content.name = "tv_content";
        content.baseColour = Vector3(0.02f, 0.02f, 0.025f);
        content.roughness = 0.08f;
        content.metallic = 0.0f;
        content.castsShadow = false;
        add(std::move(content));
    }
    {
        Material sky;
        sky.name = "unlit_black";
        sky.baseColour = Vector3(0.0f, 0.0f, 0.0f);
        sky.roughness = 1.0f;
        sky.sunlit = false;
        add(std::move(sky));
    }

    CNA::Logger::Info("cna-room: material library -- " + std::to_string(materials_.size())
                      + " materials, " + std::to_string(textures_.size()) + " textures, ~"
                      + std::to_string(textureBytes_ / (1024u * 1024u)) + " MB");
}

}  // namespace CnaRoom
