// SPDX-License-Identifier: MIT
#pragma once

#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <memory>
#include <vector>
#include <string>

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDevice;
    class ShaderEffect;
    class Texture2D;
    class TextureCube;
}

namespace CNA::Graphics {
    class FullscreenPass;
}

namespace CnaRoom {

/// Everything the sky needs to know about the moment being rendered.
struct SkyState
{
    Microsoft::Xna::Framework::Vector3 sunDirection{0.0f, 1.0f, 0.0f};  ///< towards the sun
    Microsoft::Xna::Framework::Vector3 moonDirection{0.0f, -1.0f, 0.0f};
    float turbidity = 2.5f;
    float intensity = 1.0f;       ///< scene-referred scale of the sky radiance
    float cloudCoverage = 0.3f;   ///< 0 clear .. 1 overcast
    float cloudDensity = 0.5f;    ///< how dark the cloud bases are
    float haze = 0.0f;            ///< fog/haze towards the horizon 0..1
    float moonPhase = 0.7f;       ///< 0 new .. 1 full
    float starVisibility = 1.0f;  ///< 0..1, after weather
    float timeSeconds = 0.0f;     ///< drives cloud drift
    float windX = 1.0f, windZ = 0.35f;
    float lightning = 0.0f;       ///< flash 0..1
};

/// Sun/sky lighting derived from the state, in scene-referred units.
struct SkyLighting
{
    Microsoft::Xna::Framework::Vector3 sunColour{1.0f, 1.0f, 1.0f};   ///< sun radiance × intensity
    Microsoft::Xna::Framework::Vector3 ambientColour{0.2f, 0.24f, 0.3f};///< hemisphere average
    Microsoft::Xna::Framework::Vector3 lightDirection{0.0f, -1.0f, 0.0f};///< direction sunlight travels
    Microsoft::Xna::Framework::Vector3 moonColour{0.0f, 0.0f, 0.0f};    ///< moon radiance × intensity
    Microsoft::Xna::Framework::Vector3 moonLightDirection{0.0f, -1.0f, 0.0f};
    float sunElevation = 0.0f;   ///< radians above the horizon
    float moonElevation = 0.0f;
    float daylight = 1.0f;       ///< 0 night .. 1 full day, smooth
    float shadowSoftness = 0.0f; ///< 0 a crisp sun disc .. 1 the disc smeared over the cloud deck
    bool sunUp = true;
    bool moonUp = false;
};

/**
 * @brief Atmospheric sky with clouds, stars and a moon; IBL products from the same sky.
 *
 * The fragment program is CNA's own Rayleigh/Mie model
 * (AtmosphericSky::getModelGlsl) extended with two cloud decks, a star field
 * and a moon disc, drawn as one fullscreen pass. The same model evaluated on
 * the CPU (AtmosphericSky::radiance) produces the sun/ambient colours and the
 * environment cube from which EnvironmentProcessor derives irradiance,
 * prefiltered specular and the BRDF table.
 */
class FloatCubeUploader;

class SkySystem
{
public:
    explicit SkySystem(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);
    ~SkySystem();
    SkySystem(const SkySystem&) = delete;
    SkySystem& operator=(const SkySystem&) = delete;

    void build();
    /// Updates lighting and, when the sun moved enough, the IBL products.
    void update(const SkyState& state, bool forceRebake = false);
    /// Half-float irradiance cubes when available (see FloatCubeUploader); null keeps CNA's 8-bit product.
    void setFloatCubeUploader(FloatCubeUploader* uploader) { floatCubes_ = uploader; }
    void draw(const Microsoft::Xna::Framework::Matrix& view,
              const Microsoft::Xna::Framework::Matrix& projection, int width, int height,
              bool encodeSrgb = false, float intensityScale = 1.0f);

    [[nodiscard]] bool isSupported() const { return supported_; }
    [[nodiscard]] const std::string& unsupportedReason() const { return reason_; }
    [[nodiscard]] const SkyLighting& lighting() const { return lighting_; }
    [[nodiscard]] const SkyState& state() const { return state_; }

    [[nodiscard]] Microsoft::Xna::Framework::Graphics::TextureCube* irradiance() const { return irradiance_.get(); }
    [[nodiscard]] Microsoft::Xna::Framework::Graphics::TextureCube* prefiltered() const { return prefiltered_.get(); }
    /// Half-float prefiltered cube for a roughness class (kSpecularClasses), or null.
    [[nodiscard]] Microsoft::Xna::Framework::Graphics::TextureCube* prefilteredClass(int index) const
    {
        return index >= 0 && static_cast<std::size_t>(index) < prefilteredClasses_.size() ? prefilteredClasses_[static_cast<std::size_t>(index)].get()
                                                                                            : nullptr;
    }
    [[nodiscard]] Microsoft::Xna::Framework::Graphics::Texture2D* brdfLut() const { return brdfLut_.get(); }
    [[nodiscard]] int prefilteredMipCount() const { return prefilteredMips_; }
    [[nodiscard]] float environmentScale() const { return environmentScale_; }
    [[nodiscard]] bool hasImageBasedLighting() const;
    [[nodiscard]] int bakeCount() const { return bakeCount_; }
    [[nodiscard]] float lastBakeMs() const { return lastBakeMs_; }

    [[nodiscard]] static Microsoft::Xna::Framework::Vector3 cubeDirection(
        Microsoft::Xna::Framework::Graphics::CubeMapFace face, float u, float v);
    /// Sky radiance for a direction on the CPU, including the night floor and clouds' average effect.
    [[nodiscard]] Microsoft::Xna::Framework::Vector3 radiance(
        const Microsoft::Xna::Framework::Vector3& direction) const;

private:
    void computeLighting();
    void bakeEnvironment();

    Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect> effect_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> white_;
    std::unique_ptr<CNA::Graphics::FullscreenPass> fullscreen_;
    FloatCubeUploader* floatCubes_ = nullptr;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::TextureCube> environment_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::TextureCube> irradiance_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::TextureCube> prefiltered_;
    std::vector<std::unique_ptr<Microsoft::Xna::Framework::Graphics::TextureCube>> prefilteredClasses_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> brdfLut_;

    SkyState state_;
    SkyLighting lighting_;
    Microsoft::Xna::Framework::Vector3 bakedSun_{0.0f, 0.0f, 0.0f};
    float bakedCoverage_ = -1.0f;
    float lastBakeMs_ = 0.0f;
    float environmentScale_ = 1.0f;
    float modelScale_ = 1.0f;
    int prefilteredMips_ = 5;
    int bakeCount_ = 0;
    bool supported_ = false;
    std::string reason_;
};

}  // namespace CnaRoom
