// SPDX-License-Identifier: MIT
#pragma once

#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <vector>

namespace CnaRoom {

/// Exact cosine convolution of a cube map on the CPU.
///
/// faces: 6 * size * size radiance values, face-major (+X -X +Y -Y +Z -Z),
/// rows top to bottom, in the layout EnvironmentProcessor::faceDirection reads.
/// The input is first block-averaged to small x small per face (the
/// convolution is smooth, so a coarse input loses nothing visible), then every
/// output texel of the out x out result integrates E(n) = sum L(d) max(0, n.d) dw
/// and stores E / pi, the split-sum convention (a uniform environment L gives L).
std::vector<Microsoft::Xna::Framework::Vector3> convolveIrradiance(
    const std::vector<Microsoft::Xna::Framework::Vector3>& faces, int size, int small, int out);

/// GGX-prefiltered radiance for one roughness (the split-sum's first term,
/// N = V = R, Hammersley importance sampling as CNA's own product does),
/// bilinear over the float faces, out x out per face. Returned in the input's
/// units; the caller scales and uploads it (one cube per roughness class:
/// the shader samples level 0 when the cube reports a single mip).
std::vector<Microsoft::Xna::Framework::Vector3> prefilterSpecular(
    const std::vector<Microsoft::Xna::Framework::Vector3>& faces, int size, int out, float roughness, int samples);

/// Bilinear sample of a float cube at a direction (faceDirection's layout).
[[nodiscard]] Microsoft::Xna::Framework::Vector3 sampleCube(const std::vector<Microsoft::Xna::Framework::Vector3>& faces, int size,
                                                            const Microsoft::Xna::Framework::Vector3& direction);

}  // namespace CnaRoom
