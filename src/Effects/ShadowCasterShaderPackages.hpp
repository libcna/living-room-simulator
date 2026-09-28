// SPDX-License-Identifier: MS-PL
#pragma once


#include "CNA/Graphics/ShaderPackageEXT.hpp"

namespace CnaRoom::Effects::detail
{
    /** @brief Creates the rigid directional/cascade shadow-caster package. */
    [[nodiscard]] CNA::Graphics::ShaderPackageEXT CreateDirectionalShadowCasterPackage();

    /** @brief Creates the skinned directional shadow-caster package. */
    [[nodiscard]] CNA::Graphics::ShaderPackageEXT CreateSkinnedDirectionalShadowCasterPackage();

    /** @brief Creates the point-light cube shadow-caster package. */
    [[nodiscard]] CNA::Graphics::ShaderPackageEXT CreateCubeShadowCasterPackage();

    /** @brief Creates the spot-light shadow-caster package. */
    [[nodiscard]] CNA::Graphics::ShaderPackageEXT CreateSpotShadowCasterPackage();
}
