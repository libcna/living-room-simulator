// SPDX-License-Identifier: MIT
#pragma once

#include "CnaRoom/Assets/TextureBaker.hpp"
#include "CnaRoom/Render/Material.hpp"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDevice;
    class Texture2D;
}

namespace CnaRoom {

/**
 * @brief Owns the room's materials and the textures they refer to.
 *
 * Architectural surfaces are baked procedurally at start-up (see
 * Assets::TextureBaker); materials for imported models are registered by the
 * model library with textures the model owns.
 */
class MaterialLibrary
{
public:
    explicit MaterialLibrary(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);
    /// Directory for cached bakes (PNG per channel + a header); empty disables the cache.
    void setCacheDirectory(std::string directory) { cacheDirectory_ = std::move(directory); }
    [[nodiscard]] int cacheHits() const { return cacheHits_; }
    ~MaterialLibrary();
    MaterialLibrary(const MaterialLibrary&) = delete;
    MaterialLibrary& operator=(const MaterialLibrary&) = delete;

    /// Bakes and uploads the built-in surfaces. Slow (seconds); called once.
    void buildProcedural(std::uint32_t seed, int textureSize);

    [[nodiscard]] const Material& get(const std::string& name) const;
    [[nodiscard]] const Material* find(const std::string& name) const;
    /// Mutable access for runtime state changes (a lamp shade's emission).
    [[nodiscard]] Material* edit(const std::string& name);
    /// Registers a material; the returned pointer is stable for the library's lifetime.
    const Material* add(Material material);
    /// Takes ownership of a texture the library must keep alive.
    Microsoft::Xna::Framework::Graphics::Texture2D* own(
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> texture);

    /// The snow surface's textures (albedo, normal, ORM) for swapping onto covered surfaces.
    struct SurfaceTextures
    {
        Microsoft::Xna::Framework::Graphics::Texture2D* albedo = nullptr;
        Microsoft::Xna::Framework::Graphics::Texture2D* normal = nullptr;
        Microsoft::Xna::Framework::Graphics::Texture2D* orm = nullptr;
    };
    [[nodiscard]] const SurfaceTextures& snowSurface() const { return snow_; }
    /// Snow-dusted leaf textures (same masks as `foliage` / `hedge`).
    [[nodiscard]] const SurfaceTextures& foliageSnowSurface() const { return foliageSnow_; }
    [[nodiscard]] const SurfaceTextures& hedgeSnowSurface() const { return hedgeSnow_; }
    [[nodiscard]] const SurfaceTextures& hedgeFringeSnowSurface() const { return hedgeFringeSnow_; }
    /// Droplet normal map for wet window panes (null before buildProcedural).
    /// The droplets' normal map; the frames loop as the running drops slide down the glass.
    [[nodiscard]] Microsoft::Xna::Framework::Graphics::Texture2D* raindropsNormal(int frame = 0) const
    {
        if (raindropFrames_.empty()) return nullptr;
        return raindropFrames_[static_cast<std::size_t>(((frame % static_cast<int>(raindropFrames_.size())) + static_cast<int>(raindropFrames_.size())) % static_cast<int>(raindropFrames_.size()))];
    }
    [[nodiscard]] int raindropFrameCount() const { return static_cast<int>(raindropFrames_.size()); }
    [[nodiscard]] std::size_t textureCount() const { return textures_.size(); }
    [[nodiscard]] std::size_t textureBytes() const { return textureBytes_; }

private:
    std::vector<Microsoft::Xna::Framework::Graphics::Texture2D*> raindropFrames_;
    SurfaceTextures snow_, foliageSnow_, hedgeSnow_, hedgeFringeSnow_;
    struct Surface
    {
        Microsoft::Xna::Framework::Graphics::Texture2D* albedo = nullptr;
        Microsoft::Xna::Framework::Graphics::Texture2D* normal = nullptr;
        Microsoft::Xna::Framework::Graphics::Texture2D* orm = nullptr;
        float tileMetres = 1.0f;
    };
    Surface uploadSurface(const struct SurfaceImagesRef& images);
    /// Bakes (or loads from the cache, or bakes and stores) one surface.
    Assets::SurfaceImages cachedBake(const std::string& name, std::uint32_t seed, int size,
                                     const std::function<Assets::SurfaceImages()>& bake);
    std::string cacheDirectory_;
    int cacheHits_ = 0;

    Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
    std::vector<std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D>> textures_;
    std::map<std::string, std::unique_ptr<Material>> materials_;
    std::size_t textureBytes_ = 0;
};

}  // namespace CnaRoom
