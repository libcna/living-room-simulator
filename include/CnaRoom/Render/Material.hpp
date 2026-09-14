// SPDX-License-Identifier: MIT
#pragma once

#include "Microsoft/Xna/Framework/Graphics/AlphaModeEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureTransformEXT.hpp"

#include <array>
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <string>

namespace Microsoft::Xna::Framework::Graphics {
    class Texture2D;
}

namespace CnaRoom {

/**
 * @brief A metallic-roughness material as PbrEffect consumes it.
 *
 * Textures are borrowed (the library or the imported model owns them). The
 * ORM map packs occlusion (R), roughness (G) and metallic (B), the glTF
 * layout, and is bound to both the metallic-roughness and occlusion slots.
 */
struct Material
{
    std::string name;
    Microsoft::Xna::Framework::Graphics::Texture2D* albedo = nullptr;
    Microsoft::Xna::Framework::Graphics::Texture2D* normal = nullptr;
    Microsoft::Xna::Framework::Graphics::Texture2D* orm = nullptr;
    Microsoft::Xna::Framework::Graphics::Texture2D* emissive = nullptr;
    /// Separate occlusion map (glTF occlusionTexture). When null, the ORM map's
    /// red channel is used only if ormHasOcclusion says it carries occlusion:
    /// many packed metallic-roughness maps leave red empty, which would black
    /// out every ambient term.
    Microsoft::Xna::Framework::Graphics::Texture2D* occlusion = nullptr;
    bool ormHasOcclusion = false;

    Microsoft::Xna::Framework::Vector3 baseColour{1.0f, 1.0f, 1.0f};
    Microsoft::Xna::Framework::Vector3 emissiveFactor{0.0f, 0.0f, 0.0f};
    float metallic = 0.0f;
    float roughness = 1.0f;
    float roughnessMapMean = 1.0f;   ///< mean of the ORM map's roughness where this material samples it (1 = no map)
    float alpha = 1.0f;
    float normalScale = 1.0f;
    float occlusionStrength = 1.0f;
    float ior = 1.5f;
    float specular = 1.0f;

    Microsoft::Xna::Framework::Graphics::AlphaModeEXT alphaMode =
        Microsoft::Xna::Framework::Graphics::AlphaModeEXT::Opaque;
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
    /// Imported glTF geometry winds counter-clockwise; CNA's default cull keeps
    /// clockwise faces, so such parts are drawn with the opposite cull mode.
    bool frontFaceCounterClockwise = false;

    Microsoft::Xna::Framework::Vector2 uvScale{1.0f, 1.0f};
    Microsoft::Xna::Framework::Vector2 uvOffset{0.0f, 0.0f};
    /// Imported materials carry a transform and a texture-coordinate set per
    /// slot (albedo, normal, metallic-roughness, emissive, occlusion); when
    /// set, these win over uvScale/uvOffset.
    bool perSlotTransforms = false;
    std::array<Microsoft::Xna::Framework::Graphics::TextureTransformEXT, 5> slotTransforms{};
    std::array<int, 5> slotTexCoords{0, 0, 0, 0, 0};

    bool castsShadow = true;
    bool writesDepth = true;
    bool sunlit = true;
    /// Blend mode for glass: the lit colour is added over (1 - alpha) of the
    /// background instead of being weighted by alpha, so reflections survive.
    bool reflectiveBlend = false;
    /// Planar reflection (index into the renderer's planes, -1 none): the
    /// surface shows the mirrored scene render instead of the probe cube.
    int reflectionPlane = -1;
    float reflectionF0 = 0.04f;              ///< reflectance at normal incidence (0.92 silvered glass)
    bool reflectionFresnel = true;           ///< Schlick ramp from reflectionF0 (a dielectric), else constant
    bool reflectionOverlay = false;          ///< draw the plain material first, then add the reflection (a wet road)
    float reflectionPuddles = 0.0f;          ///< 0..1: how much of the overlay gathers into puddles (low spots mirror, the rest reflects a quarter as much, broken up)
    float reflectionPuddleScale = 1.5f;      ///< metres per puddle cell
    Microsoft::Xna::Framework::Vector3 reflectionTint{1.0f, 1.0f, 1.0f};
    bool reflectionFlipV = false;            ///< the capture target's row order (R-8): none needed for a ShaderEffect sampling it
    bool reflectionEmissiveFlipV = true;     ///< the emissive render target's row order

    [[nodiscard]] bool isBlended() const
    {
        return alphaMode == Microsoft::Xna::Framework::Graphics::AlphaModeEXT::Blend;
    }
};

}  // namespace CnaRoom
