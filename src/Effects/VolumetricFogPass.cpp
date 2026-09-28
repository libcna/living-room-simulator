// SPDX-License-Identifier: MS-PL
#include "CnaRoom/Effects/VolumetricFogPass.hpp"
#include "CnaRoom/Effects/ShaderDiagnostics.hpp"


#include "CnaRoom/Effects/DepthNormalPrepass.hpp"
#include "CnaRoom/Effects/RenderPipelineSettings.hpp"
#include "CNA/Graphics/ShaderCodeEXT.hpp"
#include "CNA/Graphics/ShaderPackageEXT.hpp"
#include "CnaRoom/Effects/ShadowMap.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "shaders/volumetric_fog/VolumetricFogShaderPackage.generated.hpp"
#include "shaders/SceneHlsl.generated.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace CnaRoom::Effects {

    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Graphics::DepthFormat;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

    namespace {

        [[nodiscard]] std::vector<std::uint8_t> ToBytes(
            const std::uint32_t* words, const std::size_t byteSize)
        {
            const auto* begin = reinterpret_cast<const std::uint8_t*>(words);
            return std::vector<std::uint8_t>(begin, begin + byteSize);
        }

        struct TextStage
        {
            std::string_view source;
            const char* label;
        };

        struct SpirVStage
        {
            const std::uint32_t* words;
            std::size_t byteSize;
            const char* label;
        };

        [[nodiscard]] CNA::Graphics::ShaderPackageEXT MakeVolumetricFogPackage(
            const TextStage& esFragment, const TextStage& desktopFragment,
            const SpirVStage& vulkanFragment, const TextStage& wgslFragment,
            std::vector<CNA::Graphics::ShaderBindingRequirementEXT> additionalRequirements)
        {
            using namespace CnaRoom::Effects::detail::VolumetricFogGenerated;
            std::vector<CNA::Graphics::ShaderBindingRequirementEXT> requirements;
            requirements.reserve(additionalRequirements.size() + 1);
            requirements.emplace_back(
                "texture1", 0, CNA::Graphics::ShaderBindingTypeEXT::SampledTexture2D,
                CNA::ShaderStageEXT::Fragment);
            for (CNA::Graphics::ShaderBindingRequirementEXT& requirement : additionalRequirements)
                requirements.push_back(std::move(requirement));
            return CNA::Graphics::ShaderPackageEXT(
                {
                    CNA::Graphics::ShaderCodeEXT(CNA::ShaderLanguageEXT::GlslEs,
                                  CNA::ShaderStageEXT::Vertex, "main",
                                  "volumetric_fog/fullscreen.es.vert.glsl",
                                  std::string(kFullscreenEsVertexSource)),
                    CNA::Graphics::ShaderCodeEXT(CNA::ShaderLanguageEXT::GlslEs,
                                  CNA::ShaderStageEXT::Fragment, "main",
                                  esFragment.label, std::string(esFragment.source)),
                    CNA::Graphics::ShaderCodeEXT(CNA::ShaderLanguageEXT::GlslDesktop,
                                  CNA::ShaderStageEXT::Vertex, "main",
                                  "volumetric_fog/fullscreen.desktop.vert.glsl",
                                  std::string(kFullscreenDesktopVertexSource)),
                    CNA::Graphics::ShaderCodeEXT(CNA::ShaderLanguageEXT::GlslDesktop,
                                  CNA::ShaderStageEXT::Fragment, "main",
                                  desktopFragment.label, std::string(desktopFragment.source)),
                    CNA::Graphics::ShaderCodeEXT(CNA::ShaderLanguageEXT::SpirV,
                                  CNA::ShaderStageEXT::Vertex, "main",
                                  "volumetric_fog/fullscreen.vulkan.vert.spv",
                                  ToBytes(kFullscreenVulkanVertexSpirV,
                                          kFullscreenVulkanVertexSpirVByteSize)),
                    CNA::Graphics::ShaderCodeEXT(CNA::ShaderLanguageEXT::Wgsl,
                                  CNA::ShaderStageEXT::Vertex, "main",
                                  "volumetric_fog/fullscreen.vulkan.vert.wgsl",
                                  std::string(kFullscreenVulkanVertexWgsl)),
                    CNA::Graphics::ShaderCodeEXT(CNA::ShaderLanguageEXT::SpirV,
                                  CNA::ShaderStageEXT::Fragment, "main",
                                  vulkanFragment.label,
                                  ToBytes(vulkanFragment.words, vulkanFragment.byteSize)),
                    CNA::Graphics::ShaderCodeEXT(CNA::ShaderLanguageEXT::Wgsl,
                                  CNA::ShaderStageEXT::Fragment, "main",
                                  wgslFragment.label, std::string(wgslFragment.source)),
                    CNA::Graphics::ShaderCodeEXT(CNA::ShaderLanguageEXT::Hlsl,
                                  CNA::ShaderStageEXT::Vertex, "main",
                                  "volumetric_fog/fullscreen.vulkan.vert.spv -> hlsl",
                                  std::string(CnaRoom::Effects::detail::SceneHlslGenerated::FindStage(
                                      "volumetric_fog/fullscreen.vulkan.vert.spv"))),
                    CNA::Graphics::ShaderCodeEXT(CNA::ShaderLanguageEXT::Hlsl,
                                  CNA::ShaderStageEXT::Fragment, "main",
                                  std::string(vulkanFragment.label) + " -> hlsl",
                                  std::string(CnaRoom::Effects::detail::SceneHlslGenerated::FindStage(
                                      vulkanFragment.label))),
                },
                {CNA::ShaderStageEXT::Vertex, CNA::ShaderStageEXT::Fragment},
                std::move(requirements));
        }

        [[nodiscard]] CNA::Graphics::ShaderPackageEXT CreateBuildPackage()
        {
            using namespace CnaRoom::Effects::detail::VolumetricFogGenerated;
            return MakeVolumetricFogPackage(
                {kBuildEsFragmentSource, "volumetric_fog/build.es.frag.glsl"},
                {kBuildDesktopFragmentSource, "volumetric_fog/build.desktop.frag.glsl"},
                {kBuildVulkanFragmentSpirV, kBuildVulkanFragmentSpirVByteSize,
                 "volumetric_fog/build.vulkan.frag.spv"},
                {kBuildVulkanFragmentWgsl, "volumetric_fog/build.vulkan.frag.wgsl"},
                {CNA::Graphics::ShaderBindingRequirementEXT(
                    "uShadowSampler", 1, CNA::Graphics::ShaderBindingTypeEXT::SampledTexture2D,
                    CNA::ShaderStageEXT::Fragment)});
        }

        [[nodiscard]] CNA::Graphics::ShaderPackageEXT CreateResolvePackage()
        {
            using namespace CnaRoom::Effects::detail::VolumetricFogGenerated;
            return MakeVolumetricFogPackage(
                {kResolveEsFragmentSource, "volumetric_fog/resolve.es.frag.glsl"},
                {kResolveDesktopFragmentSource,
                 "volumetric_fog/resolve.desktop.frag.glsl"},
                {kResolveVulkanFragmentSpirV, kResolveVulkanFragmentSpirVByteSize,
                 "volumetric_fog/resolve.vulkan.frag.spv"},
                {kResolveVulkanFragmentWgsl, "volumetric_fog/resolve.vulkan.frag.wgsl"},
                {
                    CNA::Graphics::ShaderBindingRequirementEXT(
                        "uDepthSampler", 1, CNA::Graphics::ShaderBindingTypeEXT::SampledTexture2D,
                        CNA::ShaderStageEXT::Fragment),
                    CNA::Graphics::ShaderBindingRequirementEXT(
                        "uVolumeSampler", 2, CNA::Graphics::ShaderBindingTypeEXT::SampledTexture2D,
                        CNA::ShaderStageEXT::Fragment),
                });
        }

    } // namespace

    VolumetricFogPass::VolumetricFogPass(GraphicsDevice& device)
        : fullscreen_(std::make_unique<FullscreenPass>(device))
        , pool_(device)
        , packedDepth_(DepthNormalPrepass::usesPackedDepthEXT(device))
    {
        const CNA::Graphics::ShaderPackageEXT buildPackage = CreateBuildPackage();
        const CNA::Graphics::ShaderPackageEXT resolvePackage = CreateResolvePackage();
        if (buildPackage.selectFor(device).isUsable())
            buildEffect_ = std::make_unique<ShaderEffect>(device, buildPackage);
        if (resolvePackage.selectFor(device).isUsable())
            resolveEffect_ = std::make_unique<ShaderEffect>(device, resolvePackage);
        bool logged = false;
        detail::reportShaderCompileFailure(device, "VolumetricFogPass (build)", buildEffect_.get(),
                                           logged);
        detail::reportShaderCompileFailure(device, "VolumetricFogPass (resolve)",
                                           resolveEffect_.get(), logged);
    }

    VolumetricFogPass::~VolumetricFogPass() = default;

    void VolumetricFogPass::setLight(ShadowMap* shadowMap, const Vector3& lightDirection,
                                     const Vector3& lightColor)
    {
        shadowMap_      = shadowMap;
        lightDirection_ = lightDirection;
        lightColor_     = lightColor;
    }

    void VolumetricFogPass::apply(const PostProcessContext& context)
    {
        const RenderPipelineSettings* settings = context.settings;
        const float density = settings != nullptr ? settings->getVolumetricFogDensity() : density_;

        const bool ready = buildEffect_ && buildEffect_->IsEffectValid()
                        && resolveEffect_ && resolveEffect_->IsEffectValid()
                        && context.sourceDepth != nullptr && context.farPlane > 0.0f;
        if (!ready || density <= 0.0f)
        {
            fullscreen_->draw(context.source, context.destination, nullptr,
                              context.width, context.height);
            return;
        }

        RenderTarget2D* volume = pool_.acquire(kSliceCount * kSliceResolution, kSliceResolution,
                                               SurfaceFormat::Color, DepthFormat::None, 0);

        const bool haveShadow = shadowMap_ != nullptr && shadowMap_->getShadowTexture() != nullptr;
        Matrix lightViewProjection = Matrix::getIdentityProperty();
        if (haveShadow)
        {
            lightViewProjection = shadowMap_->getLightViewProjection();
            buildEffect_->SetUniformInt("uShadowSampler", 1);
            buildEffect_->SetTexture(1, *shadowMap_->getShadowTexture());
        }
        std::array<float, 48> buildMatrices{};
        context.inverseProjection.ToColumnMajor(buildMatrices.data());
        context.inverseView.ToColumnMajor(buildMatrices.data() + 16);
        lightViewProjection.ToColumnMajor(buildMatrices.data() + 32);
        const std::array buildVectors{
            lightDirection_.X, lightDirection_.Y, lightDirection_.Z,
            lightColor_.X, lightColor_.Y, lightColor_.Z,
        };
        const std::array buildScalars{
            static_cast<float>(kSliceCount),
            static_cast<float>(kSliceResolution),
            density,
            anisotropy_,
            range_,
            haveShadow ? 1.0f : 0.0f,
        };
        buildEffect_->SetUniformMat4Array("uVolumetricBuildMatrices",
                                          buildMatrices.data(), 3);
        buildEffect_->SetUniformVec3Array("uVolumetricBuildVectors",
                                          buildVectors.data(), 2);
        buildEffect_->SetUniformFloatArray("uVolumetricBuildScalars",
                                           buildScalars.data(),
                                           static_cast<int>(buildScalars.size()));
        buildEffect_->Apply();

        fullscreen_->draw(context.source, volume, buildEffect_.get(),
                          kSliceCount * kSliceResolution, kSliceResolution);

        resolveEffect_->SetUniformInt("uDepthSampler", 1);
        resolveEffect_->SetTexture(1, *context.sourceDepth);
        resolveEffect_->SetUniformInt("uVolumeSampler", 2);
        resolveEffect_->SetTexture(2, *volume);
        const std::array resolveScalars{
            static_cast<float>(kSliceCount),
            static_cast<float>(kSliceResolution),
            context.farPlane,
            range_,
            packedDepth_ ? 1.0f : 0.0f,
        };
        resolveEffect_->SetUniformFloatArray("uVolumetricResolveScalars",
                                             resolveScalars.data(),
                                             static_cast<int>(resolveScalars.size()));
        resolveEffect_->Apply();

        fullscreen_->draw(context.source, context.destination, resolveEffect_.get(),
                          context.width, context.height);
    }

    const std::string& VolumetricFogPass::getName() const
    {
        static const std::string name = "VolumetricFog";
        return name;
    }

    bool VolumetricFogPass::isSupported(GraphicsDevice& device) const
    {
        return device.SupportsCapability(CNA::GraphicsCapability::CustomEffects)
            && buildEffect_ && buildEffect_->IsEffectValid()
            && resolveEffect_ && resolveEffect_->IsEffectValid();
    }

    float VolumetricFogPass::getDensity() const { return density_; }
    void  VolumetricFogPass::setDensity(const float value) { if (value >= 0.0f) density_ = value; }

    float VolumetricFogPass::getAnisotropy() const { return anisotropy_; }
    void  VolumetricFogPass::setAnisotropy(const float value)
    {
        anisotropy_ = std::clamp(value, -0.95f, 0.95f);
    }

    float VolumetricFogPass::getRange() const { return range_; }
    void  VolumetricFogPass::setRange(const float value) { if (value > 0.0f) range_ = value; }

} // namespace CnaRoom::Effects
