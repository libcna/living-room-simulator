// SPDX-License-Identifier: MIT
#pragma once

#include "CnaRoom/Assets/Image.hpp"
#include "CnaRoom/Render/Material.hpp"

#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Microsoft::Xna::Framework::Content {
    class ContentManager;
}
namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDevice;
    class Model;
    class Texture2D;
}

namespace CnaRoom {

class GpuMesh;
class MaterialLibrary;
class SceneRenderer;

/// One imported model: its parts as GPU meshes with translated materials.
struct ImportedModel
{
    std::string name;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::Model> model;
    struct Part
    {
        std::unique_ptr<GpuMesh> mesh;
        const Material* material = nullptr;
        Microsoft::Xna::Framework::Matrix local = Microsoft::Xna::Framework::Matrix::getIdentityProperty();
        int triangles = 0;
    };
    std::vector<Part> parts;
    Microsoft::Xna::Framework::BoundingBox bounds;   ///< in the model's own space, all parts
    int triangles = 0;
    std::size_t textureCount = 0;

    [[nodiscard]] Microsoft::Xna::Framework::Vector3 size() const { return bounds.Max - bounds.Min; }
};

/**
 * @brief Loads glTF/GLB models through CNA's ContentManager and translates
 * their materials into the room's own Material so every imported object is
 * lit and shadowed exactly like the procedural architecture.
 *
 * Imported textures arrive with one mip level (CNA GLTF-206); each unique
 * image is read back once and re-uploaded with a linear-light mip chain.
 */
class ModelLibrary
{
public:
    ModelLibrary(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
                 Microsoft::Xna::Framework::Content::ContentManager& content,
                 MaterialLibrary& materials,
                 Microsoft::Xna::Framework::Content::ContentManager* compiled = nullptr);
    ~ModelLibrary();
    ModelLibrary(const ModelLibrary&) = delete;
    ModelLibrary& operator=(const ModelLibrary&) = delete;

    /// Adjusts a translated material before it is stored (part index given).
    using MaterialTweak = std::function<void(Material&, int)>;
    /// How many models loaded from compiled .cnb files instead of the glTF import.
    [[nodiscard]] int compiledLoads() const { return compiledLoads_; }
    /// Largest edge an imported texture keeps (halved in linear light above it; 0 keeps all).
    void setTextureCap(int cap) { textureCap_ = cap; }

    /// Loads (or returns the cached) model at `path` (absolute or content-relative).
    /// Returns null when the file is missing or refused; the reason is logged once.
    const ImportedModel* load(const std::string& name, const std::string& path,
                              const MaterialTweak& tweak = {});

    /// Adds every part of the model to the renderer under `world`.
    void place(SceneRenderer& renderer, const ImportedModel& model,
               const Microsoft::Xna::Framework::Matrix& world, const std::string& name,
               bool castsShadow = true) const;

    /// A world matrix that puts the model's footprint centre at `position`,
    /// rotated by `yawRadians`, optionally scaled uniformly so its height is `fitHeight`.
    [[nodiscard]] static Microsoft::Xna::Framework::Matrix placement(
        const ImportedModel& model, const Microsoft::Xna::Framework::Vector3& position,
        float yawRadians, float fitHeight = 0.0f);

    [[nodiscard]] const std::vector<std::string>& failures() const { return failures_; }
    [[nodiscard]] std::size_t loadedCount() const { return cache_.size(); }

private:
    Microsoft::Xna::Framework::Graphics::Texture2D* remip(
        Microsoft::Xna::Framework::Graphics::Texture2D* source, bool srgb);

    Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
    Microsoft::Xna::Framework::Content::ContentManager& content_;
    MaterialLibrary& materials_;
    Microsoft::Xna::Framework::Content::ContentManager* compiled_ = nullptr;   ///< rooted at assets/cnb: a .cnb's textures resolve beside it
    int compiledLoads_ = 0;   ///< models that came from a .cnb
    std::map<std::uint64_t, Microsoft::Xna::Framework::Graphics::Texture2D*> signatures_;   ///< rebuilt textures by content signature
    std::uint64_t pendingSignature_ = 0;
    int dedupedTextures_ = 0;
    int textureCap_ = 1024;          ///< imported textures larger than this are halved on load (0: keep)
    double readbackMs_ = 0.0, buildMs_ = 0.0, signatureMs_ = 0.0;
    std::map<Microsoft::Xna::Framework::Graphics::Texture2D*, Assets::Image> images_;   ///< linear maps kept after the mip rebuild
    std::map<std::string, std::unique_ptr<ImportedModel>> cache_;
    std::map<Microsoft::Xna::Framework::Graphics::Texture2D*,
             Microsoft::Xna::Framework::Graphics::Texture2D*> remipped_;
    std::vector<std::string> failures_;
};

}  // namespace CnaRoom
