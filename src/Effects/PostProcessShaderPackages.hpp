// SPDX-License-Identifier: MS-PL
#pragma once


#include "CNA/Graphics/ShaderPackageEXT.hpp"

namespace CnaRoom::Effects::detail
{
    /** @brief Creates the portable bloom-extraction fullscreen shader package. */
    [[nodiscard]] CNA::Graphics::ShaderPackageEXT CreateBloomExtractShaderPackage();

    /** @brief Creates the portable bloom-blur fullscreen shader package. */
    [[nodiscard]] CNA::Graphics::ShaderPackageEXT CreateBloomBlurShaderPackage();

    /** @brief Creates the portable bloom-upsample fullscreen shader package. */
    [[nodiscard]] CNA::Graphics::ShaderPackageEXT CreateBloomUpsampleShaderPackage();

    /** @brief Creates the portable bloom-composite fullscreen shader package. */
    [[nodiscard]] CNA::Graphics::ShaderPackageEXT CreateBloomCombineShaderPackage();

    /** @brief Creates the portable chromatic-aberration fullscreen shader package. */
    [[nodiscard]] CNA::Graphics::ShaderPackageEXT CreateChromaticAberrationShaderPackage();

    /** @brief Creates the portable screen-space contact-shadow shader package. */
    [[nodiscard]] CNA::Graphics::ShaderPackageEXT CreateContactShadowShaderPackage();

    /** @brief Creates the portable CRT fullscreen shader package. */
    [[nodiscard]] CNA::Graphics::ShaderPackageEXT CreateCrtShaderPackage();

    /** @brief Creates the portable screen-space decal shader package. */
    [[nodiscard]] CNA::Graphics::ShaderPackageEXT CreateDecalShaderPackage();

    /** @brief Creates the portable colour-depth reduction shader package. */
    [[nodiscard]] CNA::Graphics::ShaderPackageEXT CreateDepthEffectShaderPackage();

    /** @brief Creates the portable filtered-strip colour-grade shader package. */
    [[nodiscard]] CNA::Graphics::ShaderPackageEXT CreateColorGradeStripShaderPackage();

    /** @brief Creates the portable exact/tetrahedral strip colour-grade shader package. */
    [[nodiscard]] CNA::Graphics::ShaderPackageEXT CreateColorGradeInterpolatedStripShaderPackage();

    /** @brief Creates the portable volume-LUT colour-grade shader package. */
    [[nodiscard]] CNA::Graphics::ShaderPackageEXT CreateColorGradeVolumeShaderPackage();

    /** @brief Creates the portable FXAA fullscreen shader package. */
    [[nodiscard]] CNA::Graphics::ShaderPackageEXT CreateFxaaShaderPackage();

    /** @brief Creates the portable film-grain fullscreen shader package. */
    [[nodiscard]] CNA::Graphics::ShaderPackageEXT CreateFilmGrainShaderPackage();

    /** @brief Creates the portable tonemap fullscreen shader package. */
    [[nodiscard]] CNA::Graphics::ShaderPackageEXT CreateTonemapShaderPackage();

    /** @brief Creates the portable weighted-transparency resolve shader package. */
    [[nodiscard]] CNA::Graphics::ShaderPackageEXT CreateWeightedTransparencyResolveShaderPackage();

    /** @brief Creates the portable lens-flare fullscreen shader package. */
    [[nodiscard]] CNA::Graphics::ShaderPackageEXT CreateLensFlareShaderPackage();

    /** @brief Creates the portable HDR-display-output fullscreen shader package. */
    [[nodiscard]] CNA::Graphics::ShaderPackageEXT CreateHdrDisplayShaderPackage();

    /** @brief Creates the portable analytic height-fog fullscreen shader package. */
    [[nodiscard]] CNA::Graphics::ShaderPackageEXT CreateHeightFogShaderPackage();

    /** @brief Creates the portable depth-of-field fullscreen shader package. */
    [[nodiscard]] CNA::Graphics::ShaderPackageEXT CreateDepthOfFieldShaderPackage();

    /** @brief Creates the portable camera/object motion-blur fullscreen shader package. */
    [[nodiscard]] CNA::Graphics::ShaderPackageEXT CreateMotionBlurShaderPackage();

    /** @brief Creates the portable SSAO-estimation fullscreen shader package. */
    [[nodiscard]] CNA::Graphics::ShaderPackageEXT CreateSsaoOcclusionShaderPackage();

    /** @brief Creates the portable SSAO blur/composition fullscreen shader package. */
    [[nodiscard]] CNA::Graphics::ShaderPackageEXT CreateSsaoComposeShaderPackage();

    /** @brief Creates the portable screen-space-reflection fullscreen shader package. */
    [[nodiscard]] CNA::Graphics::ShaderPackageEXT CreateSsrShaderPackage();

    /** @brief Creates the portable spatial-upscale fullscreen shader package. */
    [[nodiscard]] CNA::Graphics::ShaderPackageEXT CreateSpatialUpscaleShaderPackage();

    /** @brief Creates the portable light-shaft fullscreen shader package. */
    [[nodiscard]] CNA::Graphics::ShaderPackageEXT CreateLightShaftShaderPackage();
}
