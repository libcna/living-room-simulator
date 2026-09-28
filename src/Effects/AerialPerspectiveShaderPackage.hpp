// SPDX-License-Identifier: MS-PL
#pragma once


#include "CNA/Graphics/ShaderPackageEXT.hpp"

namespace CnaRoom::Effects::detail
{
    /** @brief Creates the portable aerial-perspective fullscreen shader package. */
    [[nodiscard]] CNA::Graphics::ShaderPackageEXT CreateAerialPerspectiveShaderPackage();
}
