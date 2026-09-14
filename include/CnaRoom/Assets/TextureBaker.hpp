// SPDX-License-Identifier: MIT
#pragma once

#include "CnaRoom/Assets/Image.hpp"

#include <cstdint>
#include <memory>

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDevice;
    class Texture2D;
}

namespace CnaRoom::Assets {

/// Albedo + normal + ORM images describing one surface, before upload.
struct SurfaceImages
{
    Image albedo;
    Image normal;
    Image orm;
    float tileMetres = 1.0f;  ///< physical size of one texture repeat
};

/**
 * @brief Procedural surface synthesis for the architectural materials.
 *
 * Nothing here is photographic; the textures are built from tileable noise so
 * the room builds from a clean checkout with no downloads. Every generator is
 * deterministic in its seed.
 */
class TextureBaker
{
public:
    [[nodiscard]] static SurfaceImages plaster(int size, std::uint32_t seed, float r, float g,
                                               float b);
    [[nodiscard]] static SurfaceImages oakPlanks(int size, std::uint32_t seed);
    [[nodiscard]] static SurfaceImages carpet(int size, std::uint32_t seed, float r, float g,
                                              float b);
    [[nodiscard]] static SurfaceImages fabricWeave(int size, std::uint32_t seed, float r, float g,
                                                   float b);
    /// A chunky rib knit: beaded ribs a centimetre apart, for throws and jumpers.
    [[nodiscard]] static SurfaceImages knit(int size, std::uint32_t seed, float r, float g, float b);
    [[nodiscard]] static SurfaceImages paintedWood(int size, std::uint32_t seed, float r, float g,
                                                   float b, float roughness);
    /// Furniture-grade wood: continuous grain (no planks), stained to the given colour.
    [[nodiscard]] static SurfaceImages woodGrain(int size, std::uint32_t seed, float r, float g, float b,
                                                 float roughness);
    [[nodiscard]] static SurfaceImages concrete(int size, std::uint32_t seed);
    [[nodiscard]] static SurfaceImages brick(int size, std::uint32_t seed);
    [[nodiscard]] static SurfaceImages asphalt(int size, std::uint32_t seed);
    [[nodiscard]] static SurfaceImages grass(int size, std::uint32_t seed);
    /// Leaf clusters with gaps in the alpha channel (mask at 0.5); coverage 0..1.
    [[nodiscard]] static SurfaceImages foliage(int size, std::uint32_t seed, float coverage, float snow = 0.0f);
    [[nodiscard]] static SurfaceImages bark(int size, std::uint32_t seed);
    /// 0.6 m concrete paving slabs with joints (1.2 m tile).
    [[nodiscard]] static SurfaceImages pavingSlabs(int size, std::uint32_t seed);
    /// Barrel roof tiles in the given colour (1 m tile, 3 courses).
    [[nodiscard]] static SurfaceImages roofTiles(int size, std::uint32_t seed, float r, float g, float b);
    /// Settled snow (1.5 m tile).
    [[nodiscard]] static SurfaceImages snow(int size, std::uint32_t seed);
    /// What a facade window shows, spanning the window once: kind 0 a lamp-lit
    /// room with curtains at the sides, 1 drawn curtains lit from behind,
    /// 2 a television's glow, 3 a dark room. `glow` bakes the night emissive
    /// (the room lit by its lamp) instead of the daytime albedo (the same
    /// room, dark behind its curtains).
    [[nodiscard]] static SurfaceImages windowInterior(int size, std::uint32_t seed, int kind, bool glow = false);
    /// A flame card, spanning its quad once: a teardrop with noise-torn edges, a
    /// pale core through orange to a red rim, transparent around it; the tip at the top.
    [[nodiscard]] static SurfaceImages flame(int size, std::uint32_t seed);
    /// A bed of embers: charcoal lumps with glowing cracks between them (the
    /// image doubles as the emissive map, so only the cracks glow).
    [[nodiscard]] static SurfaceImages embers(int size, std::uint32_t seed);
    /// A wall clock's dial, spanning its quad once: a cream face inside a dark rim
    /// (alpha 0 outside the disc, for a masked round quad), twelve bars and the
    /// minute ticks; 12 at the top.
    [[nodiscard]] static SurfaceImages clockDial(int size, std::uint32_t seed);
    /// An abstract canvas, spanning it once: muted colour fields brushed over a
    /// ground (kind 0 ochre and rust on blue-grey, 1 sage on cream, 2 plum and
    /// charcoal on warm grey), the cloth's weave under the paint.
    [[nodiscard]] static SurfaceImages canvas(int size, std::uint32_t seed, int kind);
    /// Water droplets on glass as a normal map (for the window panes when it rains).
    [[nodiscard]] static Image raindrops(int size, std::uint32_t seed);
    /// A flat surface with a stated colour/roughness/metallic and faint variation.
    [[nodiscard]] static SurfaceImages flat(int size, std::uint32_t seed, float r, float g, float b,
                                            float roughness, float metallic, float variation);

    /// Uploads with a full mip chain; sRGB images are averaged in linear light.
    /// With alphaCutoff >= 0 every level's alpha is rescaled so the fraction of
    /// texels above the cutoff matches level 0 (masked foliage keeps its
    /// density at a distance instead of turning solid or vanishing).
    [[nodiscard]] static std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> upload(
        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, const Image& image, bool srgb,
        float alphaCutoff = -1.0f);
};

}  // namespace CnaRoom::Assets
