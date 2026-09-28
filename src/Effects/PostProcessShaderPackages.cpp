// SPDX-License-Identifier: MS-PL
#include "PostProcessShaderPackages.hpp"
#include "AerialPerspectiveShaderPackage.hpp"


#include "CNA/Graphics/ShaderCodeEXT.hpp"
#include "shaders/post_process/PostProcessHlsl.generated.hpp"
#include "shaders/post_process/PostProcessShaderPackage.generated.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace CnaRoom::Effects::detail
{
    namespace
    {
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

        std::string HdrSamplerPrecision(const std::string_view source)
        {
            std::string text(source);
            const auto headerEnd = text.find('\n');
            text.insert(headerEnd == std::string::npos ? 0 : headerEnd + 1,
                        "precision highp sampler2D;\nprecision highp sampler3D;\nprecision highp samplerCube;\n");
            return text;
        }

        [[nodiscard]] CNA::Graphics::ShaderPackageEXT MakeFullscreenPackage(
            const TextStage& esFragment, const TextStage& desktopFragment,
            const SpirVStage& vulkanFragment, const TextStage& wgslFragment,
            std::vector<CNA::Graphics::ShaderBindingRequirementEXT> additionalRequirements = {})
        {
            using namespace CnaRoom::Effects::detail::PostProcessGenerated;
            std::vector<CNA::Graphics::ShaderBindingRequirementEXT> requirements;
            requirements.reserve(additionalRequirements.size() + 1);
            requirements.emplace_back(
                "texture1", 0, CNA::Graphics::ShaderBindingTypeEXT::SampledTexture2D,
                CNA::ShaderStageEXT::Fragment);
            for (CNA::Graphics::ShaderBindingRequirementEXT& requirement : additionalRequirements)
                requirements.push_back(std::move(requirement));
            std::vector<CNA::Graphics::ShaderCodeEXT> variants{
                    CNA::Graphics::ShaderCodeEXT(CNA::ShaderLanguageEXT::GlslEs,
                                  CNA::ShaderStageEXT::Vertex, "main",
                                  "post_process/fullscreen.es.vert.glsl",
                                  std::string(kFullscreenEsVertexSource)),
                    CNA::Graphics::ShaderCodeEXT(CNA::ShaderLanguageEXT::GlslEs,
                                  CNA::ShaderStageEXT::Fragment, "main",
                                  esFragment.label, HdrSamplerPrecision(esFragment.source)),
                    CNA::Graphics::ShaderCodeEXT(CNA::ShaderLanguageEXT::GlslDesktop,
                                  CNA::ShaderStageEXT::Vertex, "main",
                                  "post_process/fullscreen.desktop.vert.glsl",
                                  std::string(kFullscreenDesktopVertexSource)),
                    CNA::Graphics::ShaderCodeEXT(CNA::ShaderLanguageEXT::GlslDesktop,
                                  CNA::ShaderStageEXT::Fragment, "main",
                                  desktopFragment.label,
                                  std::string(desktopFragment.source)),
                    CNA::Graphics::ShaderCodeEXT(CNA::ShaderLanguageEXT::SpirV,
                                  CNA::ShaderStageEXT::Vertex, "main",
                                  "post_process/fullscreen.vulkan.vert.spv",
                                  ToBytes(kFullscreenVulkanVertexSpirV,
                                          kFullscreenVulkanVertexSpirVByteSize)),
                    CNA::Graphics::ShaderCodeEXT(CNA::ShaderLanguageEXT::SpirV,
                                  CNA::ShaderStageEXT::Fragment, "main",
                                  vulkanFragment.label,
                                  ToBytes(vulkanFragment.words,
                                          vulkanFragment.byteSize)),
                    // plans/plan_webgpu_modern_graphics.md WMG-0005: the WGSL the generator derives
                    // from the same Vulkan GLSL, so a WGSL renderer runs the pass rather than
                    // falling back to a copy of its input.
                    CNA::Graphics::ShaderCodeEXT(CNA::ShaderLanguageEXT::Wgsl,
                                  CNA::ShaderStageEXT::Vertex, "main",
                                  "post_process/fullscreen.vulkan.vert.wgsl",
                                  std::string(kFullscreenVulkanVertexWgsl)),
                    CNA::Graphics::ShaderCodeEXT(CNA::ShaderLanguageEXT::Wgsl,
                                  CNA::ShaderStageEXT::Fragment, "main",
                                  wgslFragment.label, std::string(wgslFragment.source)),
                };
            const std::string_view hlslFragment =
                PostProcessHlslGenerated::FindFragment(vulkanFragment.label);
            if (!hlslFragment.empty())
            {
                variants.emplace_back(
                    CNA::ShaderLanguageEXT::Hlsl, CNA::ShaderStageEXT::Vertex,
                    "main", "post_process/fullscreen.hlsl",
                    std::string(PostProcessHlslGenerated::kFullscreenVertexHlsl));
                variants.emplace_back(
                    CNA::ShaderLanguageEXT::Hlsl, CNA::ShaderStageEXT::Fragment,
                    "main", std::string(vulkanFragment.label) + " -> hlsl",
                    std::string(hlslFragment));
            }
            return CNA::Graphics::ShaderPackageEXT(
                std::move(variants),
                {CNA::ShaderStageEXT::Vertex, CNA::ShaderStageEXT::Fragment},
                std::move(requirements));
        }
    }

    CNA::Graphics::ShaderPackageEXT CreateAerialPerspectiveShaderPackage()
    {
        using namespace CnaRoom::Effects::detail::PostProcessGenerated;
        return MakeFullscreenPackage(
            {kAerialPerspectiveEsFragmentSource,
             "post_process/aerial_perspective.es.frag.glsl"},
            {kAerialPerspectiveDesktopFragmentSource,
             "post_process/aerial_perspective.desktop.frag.glsl"},
            {kAerialPerspectiveVulkanFragmentSpirV,
             kAerialPerspectiveVulkanFragmentSpirVByteSize,
             "post_process/aerial_perspective.vulkan.frag.spv"},
            {kAerialPerspectiveVulkanFragmentWgsl,
             "post_process/aerial_perspective.vulkan.frag.wgsl"},
            {CNA::Graphics::ShaderBindingRequirementEXT(
                "uDepthSampler", 1, CNA::Graphics::ShaderBindingTypeEXT::SampledTexture2D,
                CNA::ShaderStageEXT::Fragment)});
    }

    CNA::Graphics::ShaderPackageEXT CreateBloomExtractShaderPackage()
    {
        using namespace CnaRoom::Effects::detail::PostProcessGenerated;
        return MakeFullscreenPackage(
            {kBloomExtractEsFragmentSource, "post_process/bloom_extract.es.frag.glsl"},
            {kBloomExtractDesktopFragmentSource,
             "post_process/bloom_extract.desktop.frag.glsl"},
            {kBloomExtractVulkanFragmentSpirV,
             kBloomExtractVulkanFragmentSpirVByteSize,
             "post_process/bloom_extract.vulkan.frag.spv"},
            {kBloomExtractVulkanFragmentWgsl,
             "post_process/bloom_extract.vulkan.frag.wgsl"});
    }

    CNA::Graphics::ShaderPackageEXT CreateBloomBlurShaderPackage()
    {
        using namespace CnaRoom::Effects::detail::PostProcessGenerated;
        return MakeFullscreenPackage(
            {kBloomBlurEsFragmentSource, "post_process/bloom_blur.es.frag.glsl"},
            {kBloomBlurDesktopFragmentSource,
             "post_process/bloom_blur.desktop.frag.glsl"},
            {kBloomBlurVulkanFragmentSpirV,
             kBloomBlurVulkanFragmentSpirVByteSize,
             "post_process/bloom_blur.vulkan.frag.spv"},
            {kBloomBlurVulkanFragmentWgsl,
             "post_process/bloom_blur.vulkan.frag.wgsl"});
    }

    CNA::Graphics::ShaderPackageEXT CreateBloomUpsampleShaderPackage()
    {
        using namespace CnaRoom::Effects::detail::PostProcessGenerated;
        return MakeFullscreenPackage(
            {kBloomUpsampleEsFragmentSource,
             "post_process/bloom_upsample.es.frag.glsl"},
            {kBloomUpsampleDesktopFragmentSource,
             "post_process/bloom_upsample.desktop.frag.glsl"},
            {kBloomUpsampleVulkanFragmentSpirV,
             kBloomUpsampleVulkanFragmentSpirVByteSize,
             "post_process/bloom_upsample.vulkan.frag.spv"},
            {kBloomUpsampleVulkanFragmentWgsl,
             "post_process/bloom_upsample.vulkan.frag.wgsl"},
            {CNA::Graphics::ShaderBindingRequirementEXT(
                "uSmallerSampler", 1, CNA::Graphics::ShaderBindingTypeEXT::SampledTexture2D,
                CNA::ShaderStageEXT::Fragment)});
    }

    CNA::Graphics::ShaderPackageEXT CreateBloomCombineShaderPackage()
    {
        using namespace CnaRoom::Effects::detail::PostProcessGenerated;
        return MakeFullscreenPackage(
            {kBloomCombineEsFragmentSource,
             "post_process/bloom_combine.es.frag.glsl"},
            {kBloomCombineDesktopFragmentSource,
             "post_process/bloom_combine.desktop.frag.glsl"},
            {kBloomCombineVulkanFragmentSpirV,
             kBloomCombineVulkanFragmentSpirVByteSize,
             "post_process/bloom_combine.vulkan.frag.spv"},
            {kBloomCombineVulkanFragmentWgsl,
             "post_process/bloom_combine.vulkan.frag.wgsl"},
            {CNA::Graphics::ShaderBindingRequirementEXT(
                "uBloomSampler", 1, CNA::Graphics::ShaderBindingTypeEXT::SampledTexture2D,
                CNA::ShaderStageEXT::Fragment)});
    }

    CNA::Graphics::ShaderPackageEXT CreateChromaticAberrationShaderPackage()
    {
        using namespace CnaRoom::Effects::detail::PostProcessGenerated;
        return MakeFullscreenPackage(
            {kChromaticEsFragmentSource, "post_process/chromatic.es.frag.glsl"},
            {kChromaticDesktopFragmentSource,
             "post_process/chromatic.desktop.frag.glsl"},
            {kChromaticVulkanFragmentSpirV, kChromaticVulkanFragmentSpirVByteSize,
             "post_process/chromatic.vulkan.frag.spv"},
            {kChromaticVulkanFragmentWgsl,
             "post_process/chromatic.vulkan.frag.wgsl"});
    }

    CNA::Graphics::ShaderPackageEXT CreateContactShadowShaderPackage()
    {
        using namespace CnaRoom::Effects::detail::PostProcessGenerated;
        return MakeFullscreenPackage(
            {kContactShadowEsFragmentSource,
             "post_process/contact_shadow.es.frag.glsl"},
            {kContactShadowDesktopFragmentSource,
             "post_process/contact_shadow.desktop.frag.glsl"},
            {kContactShadowVulkanFragmentSpirV,
             kContactShadowVulkanFragmentSpirVByteSize,
             "post_process/contact_shadow.vulkan.frag.spv"},
            {kContactShadowVulkanFragmentWgsl,
             "post_process/contact_shadow.vulkan.frag.wgsl"},
            {CNA::Graphics::ShaderBindingRequirementEXT(
                "uDepthSampler", 1, CNA::Graphics::ShaderBindingTypeEXT::SampledTexture2D,
                CNA::ShaderStageEXT::Fragment)});
    }

    CNA::Graphics::ShaderPackageEXT CreateCrtShaderPackage()
    {
        using namespace CnaRoom::Effects::detail::PostProcessGenerated;
        return MakeFullscreenPackage(
            {kCrtEsFragmentSource, "post_process/crt.es.frag.glsl"},
            {kCrtDesktopFragmentSource, "post_process/crt.desktop.frag.glsl"},
            {kCrtVulkanFragmentSpirV, kCrtVulkanFragmentSpirVByteSize,
             "post_process/crt.vulkan.frag.spv"},
            {kCrtVulkanFragmentWgsl,
             "post_process/crt.vulkan.frag.wgsl"});
    }

    CNA::Graphics::ShaderPackageEXT CreateDecalShaderPackage()
    {
        using namespace CnaRoom::Effects::detail::PostProcessGenerated;
        return MakeFullscreenPackage(
            {kDecalEsFragmentSource, "post_process/decal.es.frag.glsl"},
            {kDecalDesktopFragmentSource,
             "post_process/decal.desktop.frag.glsl"},
            {kDecalVulkanFragmentSpirV, kDecalVulkanFragmentSpirVByteSize,
             "post_process/decal.vulkan.frag.spv"},
            {kDecalVulkanFragmentWgsl,
             "post_process/decal.vulkan.frag.wgsl"},
            {
                CNA::Graphics::ShaderBindingRequirementEXT(
                    "uDecalSampler", 1, CNA::Graphics::ShaderBindingTypeEXT::SampledTexture2D,
                    CNA::ShaderStageEXT::Fragment),
                CNA::Graphics::ShaderBindingRequirementEXT(
                    "uNormalSampler", 2, CNA::Graphics::ShaderBindingTypeEXT::SampledTexture2D,
                    CNA::ShaderStageEXT::Fragment),
            });
    }

    CNA::Graphics::ShaderPackageEXT CreateDepthEffectShaderPackage()
    {
        using namespace CnaRoom::Effects::detail::PostProcessGenerated;
        return MakeFullscreenPackage(
            {kDepthEffectEsFragmentSource,
             "post_process/depth_effect.es.frag.glsl"},
            {kDepthEffectDesktopFragmentSource,
             "post_process/depth_effect.desktop.frag.glsl"},
            {kDepthEffectVulkanFragmentSpirV,
             kDepthEffectVulkanFragmentSpirVByteSize,
             "post_process/depth_effect.vulkan.frag.spv"},
            {kDepthEffectVulkanFragmentWgsl,
             "post_process/depth_effect.vulkan.frag.wgsl"},
            {CNA::Graphics::ShaderBindingRequirementEXT(
                "uPalette", 1, CNA::Graphics::ShaderBindingTypeEXT::SampledTexture2D,
                CNA::ShaderStageEXT::Fragment)});
    }

    CNA::Graphics::ShaderPackageEXT CreateColorGradeStripShaderPackage()
    {
        using namespace CnaRoom::Effects::detail::PostProcessGenerated;
        return MakeFullscreenPackage(
            {kColorGradeStripEsFragmentSource,
             "post_process/color_grade_strip.es.frag.glsl"},
            {kColorGradeStripDesktopFragmentSource,
             "post_process/color_grade_strip.desktop.frag.glsl"},
            {kColorGradeStripVulkanFragmentSpirV,
             kColorGradeStripVulkanFragmentSpirVByteSize,
             "post_process/color_grade_strip.vulkan.frag.spv"},
            {kColorGradeStripVulkanFragmentWgsl,
             "post_process/color_grade_strip.vulkan.frag.wgsl"},
            {CNA::Graphics::ShaderBindingRequirementEXT(
                "uLutSampler", 1, CNA::Graphics::ShaderBindingTypeEXT::SampledTexture2D,
                CNA::ShaderStageEXT::Fragment)});
    }

    CNA::Graphics::ShaderPackageEXT CreateColorGradeInterpolatedStripShaderPackage()
    {
        using namespace CnaRoom::Effects::detail::PostProcessGenerated;
        return MakeFullscreenPackage(
            {kColorGradeInterpolatedStripEsFragmentSource,
             "post_process/color_grade_interpolated_strip.es.frag.glsl"},
            {kColorGradeInterpolatedStripDesktopFragmentSource,
             "post_process/color_grade_interpolated_strip.desktop.frag.glsl"},
            {kColorGradeInterpolatedStripVulkanFragmentSpirV,
             kColorGradeInterpolatedStripVulkanFragmentSpirVByteSize,
             "post_process/color_grade_interpolated_strip.vulkan.frag.spv"},
            {kColorGradeInterpolatedStripVulkanFragmentWgsl,
             "post_process/color_grade_interpolated_strip.vulkan.frag.wgsl"},
            {CNA::Graphics::ShaderBindingRequirementEXT(
                "uLutSampler", 1, CNA::Graphics::ShaderBindingTypeEXT::SampledTexture2D,
                CNA::ShaderStageEXT::Fragment)});
    }

    CNA::Graphics::ShaderPackageEXT CreateColorGradeVolumeShaderPackage()
    {
        using namespace CnaRoom::Effects::detail::PostProcessGenerated;
        return MakeFullscreenPackage(
            {kColorGradeVolumeEsFragmentSource,
             "post_process/color_grade_volume.es.frag.glsl"},
            {kColorGradeVolumeDesktopFragmentSource,
             "post_process/color_grade_volume.desktop.frag.glsl"},
            {kColorGradeVolumeVulkanFragmentSpirV,
             kColorGradeVolumeVulkanFragmentSpirVByteSize,
             "post_process/color_grade_volume.vulkan.frag.spv"},
            {kColorGradeVolumeVulkanFragmentWgsl,
             "post_process/color_grade_volume.vulkan.frag.wgsl"},
            {CNA::Graphics::ShaderBindingRequirementEXT(
                "uLutVolume", 1, CNA::Graphics::ShaderBindingTypeEXT::SampledTexture3D,
                CNA::ShaderStageEXT::Fragment)});
    }

    CNA::Graphics::ShaderPackageEXT CreateFxaaShaderPackage()
    {
        using namespace CnaRoom::Effects::detail::PostProcessGenerated;
        return MakeFullscreenPackage(
            {kFxaaEsFragmentSource, "post_process/fxaa.es.frag.glsl"},
            {kFxaaDesktopFragmentSource, "post_process/fxaa.desktop.frag.glsl"},
            {kFxaaVulkanFragmentSpirV, kFxaaVulkanFragmentSpirVByteSize,
             "post_process/fxaa.vulkan.frag.spv"},
            {kFxaaVulkanFragmentWgsl,
             "post_process/fxaa.vulkan.frag.wgsl"});
    }

    CNA::Graphics::ShaderPackageEXT CreateFilmGrainShaderPackage()
    {
        using namespace CnaRoom::Effects::detail::PostProcessGenerated;
        return MakeFullscreenPackage(
            {kFilmGrainEsFragmentSource, "post_process/film_grain.es.frag.glsl"},
            {kFilmGrainDesktopFragmentSource,
             "post_process/film_grain.desktop.frag.glsl"},
            {kFilmGrainVulkanFragmentSpirV,
             kFilmGrainVulkanFragmentSpirVByteSize,
             "post_process/film_grain.vulkan.frag.spv"},
            {kFilmGrainVulkanFragmentWgsl,
             "post_process/film_grain.vulkan.frag.wgsl"});
    }

    CNA::Graphics::ShaderPackageEXT CreateTonemapShaderPackage()
    {
        using namespace CnaRoom::Effects::detail::PostProcessGenerated;
        return MakeFullscreenPackage(
            {kTonemapEsFragmentSource, "post_process/tonemap.es.frag.glsl"},
            {kTonemapDesktopFragmentSource, "post_process/tonemap.desktop.frag.glsl"},
            {kTonemapVulkanFragmentSpirV, kTonemapVulkanFragmentSpirVByteSize,
             "post_process/tonemap.vulkan.frag.spv"},
            {kTonemapVulkanFragmentWgsl,
             "post_process/tonemap.vulkan.frag.wgsl"});
    }

    CNA::Graphics::ShaderPackageEXT CreateWeightedTransparencyResolveShaderPackage()
    {
        using namespace CnaRoom::Effects::detail::PostProcessGenerated;
        return MakeFullscreenPackage(
            {kWeightedTransparencyResolveEsFragmentSource,
             "post_process/weighted_transparency_resolve.es.frag.glsl"},
            {kWeightedTransparencyResolveDesktopFragmentSource,
             "post_process/weighted_transparency_resolve.desktop.frag.glsl"},
            {kWeightedTransparencyResolveVulkanFragmentSpirV,
             kWeightedTransparencyResolveVulkanFragmentSpirVByteSize,
             "post_process/weighted_transparency_resolve.vulkan.frag.spv"},
            {kWeightedTransparencyResolveVulkanFragmentWgsl,
             "post_process/weighted_transparency_resolve.vulkan.frag.wgsl"},
            {CNA::Graphics::ShaderBindingRequirementEXT(
                "uRevealage", 1, CNA::Graphics::ShaderBindingTypeEXT::SampledTexture2D,
                CNA::ShaderStageEXT::Fragment)});
    }

    CNA::Graphics::ShaderPackageEXT CreateLensFlareShaderPackage()
    {
        using namespace CnaRoom::Effects::detail::PostProcessGenerated;
        return MakeFullscreenPackage(
            {kLensFlareEsFragmentSource, "post_process/lens_flare.es.frag.glsl"},
            {kLensFlareDesktopFragmentSource,
             "post_process/lens_flare.desktop.frag.glsl"},
            {kLensFlareVulkanFragmentSpirV,
             kLensFlareVulkanFragmentSpirVByteSize,
             "post_process/lens_flare.vulkan.frag.spv"},
            {kLensFlareVulkanFragmentWgsl,
             "post_process/lens_flare.vulkan.frag.wgsl"});
    }

    CNA::Graphics::ShaderPackageEXT CreateHdrDisplayShaderPackage()
    {
        using namespace CnaRoom::Effects::detail::PostProcessGenerated;
        return MakeFullscreenPackage(
            {kHdrDisplayEsFragmentSource, "post_process/hdr_display.es.frag.glsl"},
            {kHdrDisplayDesktopFragmentSource,
             "post_process/hdr_display.desktop.frag.glsl"},
            {kHdrDisplayVulkanFragmentSpirV,
             kHdrDisplayVulkanFragmentSpirVByteSize,
             "post_process/hdr_display.vulkan.frag.spv"},
            {kHdrDisplayVulkanFragmentWgsl,
             "post_process/hdr_display.vulkan.frag.wgsl"});
    }

    CNA::Graphics::ShaderPackageEXT CreateHeightFogShaderPackage()
    {
        using namespace CnaRoom::Effects::detail::PostProcessGenerated;
        return MakeFullscreenPackage(
            {kHeightFogEsFragmentSource, "post_process/height_fog.es.frag.glsl"},
            {kHeightFogDesktopFragmentSource,
             "post_process/height_fog.desktop.frag.glsl"},
            {kHeightFogVulkanFragmentSpirV,
             kHeightFogVulkanFragmentSpirVByteSize,
             "post_process/height_fog.vulkan.frag.spv"},
            {kHeightFogVulkanFragmentWgsl,
             "post_process/height_fog.vulkan.frag.wgsl"},
            {CNA::Graphics::ShaderBindingRequirementEXT(
                "uDepthSampler", 1, CNA::Graphics::ShaderBindingTypeEXT::SampledTexture2D,
                CNA::ShaderStageEXT::Fragment)});
    }

    CNA::Graphics::ShaderPackageEXT CreateDepthOfFieldShaderPackage()
    {
        using namespace CnaRoom::Effects::detail::PostProcessGenerated;
        return MakeFullscreenPackage(
            {kDepthOfFieldEsFragmentSource,
             "post_process/depth_of_field.es.frag.glsl"},
            {kDepthOfFieldDesktopFragmentSource,
             "post_process/depth_of_field.desktop.frag.glsl"},
            {kDepthOfFieldVulkanFragmentSpirV,
             kDepthOfFieldVulkanFragmentSpirVByteSize,
             "post_process/depth_of_field.vulkan.frag.spv"},
            {kDepthOfFieldVulkanFragmentWgsl,
             "post_process/depth_of_field.vulkan.frag.wgsl"},
            {CNA::Graphics::ShaderBindingRequirementEXT(
                "uDepthSampler", 1, CNA::Graphics::ShaderBindingTypeEXT::SampledTexture2D,
                CNA::ShaderStageEXT::Fragment)});
    }

    CNA::Graphics::ShaderPackageEXT CreateMotionBlurShaderPackage()
    {
        using namespace CnaRoom::Effects::detail::PostProcessGenerated;
        return MakeFullscreenPackage(
            {kMotionBlurEsFragmentSource, "post_process/motion_blur.es.frag.glsl"},
            {kMotionBlurDesktopFragmentSource,
             "post_process/motion_blur.desktop.frag.glsl"},
            {kMotionBlurVulkanFragmentSpirV,
             kMotionBlurVulkanFragmentSpirVByteSize,
             "post_process/motion_blur.vulkan.frag.spv"},
            {kMotionBlurVulkanFragmentWgsl,
             "post_process/motion_blur.vulkan.frag.wgsl"},
            {
                CNA::Graphics::ShaderBindingRequirementEXT(
                    "uDepthSampler", 1, CNA::Graphics::ShaderBindingTypeEXT::SampledTexture2D,
                    CNA::ShaderStageEXT::Fragment),
                CNA::Graphics::ShaderBindingRequirementEXT(
                    "uVelocitySampler", 2, CNA::Graphics::ShaderBindingTypeEXT::SampledTexture2D,
                    CNA::ShaderStageEXT::Fragment),
            });
    }

    CNA::Graphics::ShaderPackageEXT CreateSsaoOcclusionShaderPackage()
    {
        using namespace CnaRoom::Effects::detail::PostProcessGenerated;
        return MakeFullscreenPackage(
            {kSsaoOcclusionEsFragmentSource,
             "post_process/ssao_occlusion.es.frag.glsl"},
            {kSsaoOcclusionDesktopFragmentSource,
             "post_process/ssao_occlusion.desktop.frag.glsl"},
            {kSsaoOcclusionVulkanFragmentSpirV,
             kSsaoOcclusionVulkanFragmentSpirVByteSize,
             "post_process/ssao_occlusion.vulkan.frag.spv"},
            {kSsaoOcclusionVulkanFragmentWgsl,
             "post_process/ssao_occlusion.vulkan.frag.wgsl"},
            {
                CNA::Graphics::ShaderBindingRequirementEXT(
                    "uNormalSampler", 1, CNA::Graphics::ShaderBindingTypeEXT::SampledTexture2D,
                    CNA::ShaderStageEXT::Fragment),
                CNA::Graphics::ShaderBindingRequirementEXT(
                    "uNoiseSampler", 2, CNA::Graphics::ShaderBindingTypeEXT::SampledTexture2D,
                    CNA::ShaderStageEXT::Fragment),
            });
    }

    CNA::Graphics::ShaderPackageEXT CreateSsaoComposeShaderPackage()
    {
        using namespace CnaRoom::Effects::detail::PostProcessGenerated;
        return MakeFullscreenPackage(
            {kSsaoComposeEsFragmentSource, "post_process/ssao_compose.es.frag.glsl"},
            {kSsaoComposeDesktopFragmentSource,
             "post_process/ssao_compose.desktop.frag.glsl"},
            {kSsaoComposeVulkanFragmentSpirV,
             kSsaoComposeVulkanFragmentSpirVByteSize,
             "post_process/ssao_compose.vulkan.frag.spv"},
            {kSsaoComposeVulkanFragmentWgsl,
             "post_process/ssao_compose.vulkan.frag.wgsl"},
            {CNA::Graphics::ShaderBindingRequirementEXT(
                "uOcclusionSampler", 1, CNA::Graphics::ShaderBindingTypeEXT::SampledTexture2D,
                CNA::ShaderStageEXT::Fragment)});
    }

    CNA::Graphics::ShaderPackageEXT CreateSsrShaderPackage()
    {
        using namespace CnaRoom::Effects::detail::PostProcessGenerated;
        return MakeFullscreenPackage(
            {kSsrEsFragmentSource, "post_process/ssr.es.frag.glsl"},
            {kSsrDesktopFragmentSource, "post_process/ssr.desktop.frag.glsl"},
            {kSsrVulkanFragmentSpirV, kSsrVulkanFragmentSpirVByteSize,
             "post_process/ssr.vulkan.frag.spv"},
            {kSsrVulkanFragmentWgsl,
             "post_process/ssr.vulkan.frag.wgsl"},
            {
                CNA::Graphics::ShaderBindingRequirementEXT(
                    "uDepthSampler", 1, CNA::Graphics::ShaderBindingTypeEXT::SampledTexture2D,
                    CNA::ShaderStageEXT::Fragment),
                CNA::Graphics::ShaderBindingRequirementEXT(
                    "uNormalSampler", 2, CNA::Graphics::ShaderBindingTypeEXT::SampledTexture2D,
                    CNA::ShaderStageEXT::Fragment),
            });
    }

    CNA::Graphics::ShaderPackageEXT CreateSpatialUpscaleShaderPackage()
    {
        using namespace CnaRoom::Effects::detail::PostProcessGenerated;
        return MakeFullscreenPackage(
            {kSpatialUpscaleEsFragmentSource,
             "post_process/spatial_upscale.es.frag.glsl"},
            {kSpatialUpscaleDesktopFragmentSource,
             "post_process/spatial_upscale.desktop.frag.glsl"},
            {kSpatialUpscaleVulkanFragmentSpirV,
             kSpatialUpscaleVulkanFragmentSpirVByteSize,
             "post_process/spatial_upscale.vulkan.frag.spv"},
            {kSpatialUpscaleVulkanFragmentWgsl,
             "post_process/spatial_upscale.vulkan.frag.wgsl"});
    }

    CNA::Graphics::ShaderPackageEXT CreateLightShaftShaderPackage()
    {
        using namespace CnaRoom::Effects::detail::PostProcessGenerated;
        return MakeFullscreenPackage(
            {kLightShaftEsFragmentSource, "post_process/light_shaft.es.frag.glsl"},
            {kLightShaftDesktopFragmentSource,
             "post_process/light_shaft.desktop.frag.glsl"},
            {kLightShaftVulkanFragmentSpirV,
             kLightShaftVulkanFragmentSpirVByteSize,
             "post_process/light_shaft.vulkan.frag.spv"},
            {kLightShaftVulkanFragmentWgsl,
             "post_process/light_shaft.vulkan.frag.wgsl"});
    }
}
