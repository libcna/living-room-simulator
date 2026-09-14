// SPDX-License-Identifier: MIT
// CPU cube-map products: the bilinear sampler's face mapping, the irradiance
// convolution's normalisation and the GGX prefilter's blur.
#include "CnaRoom/Render/Irradiance.hpp"

#include "CNA/Graphics/EnvironmentProcessor.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace CnaRoom;

namespace {

int failures = 0;

void check(bool condition, const std::string& what)
{
    if (!condition)
    {
        ++failures;
        std::printf("FAIL: %s\n", what.c_str());
    }
}

std::vector<Vector3> uniformCube(int size, const Vector3& value)
{
    return std::vector<Vector3>(static_cast<std::size_t>(6 * size * size), value);
}

}  // namespace

int main()
{
    const int size = 16;
    // Sampling at a texel's own direction returns that texel, on every face.
    std::vector<Vector3> faces = uniformCube(size, Vector3::Zero);
    for (int f = 0; f < 6; ++f)
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
                faces[static_cast<std::size_t>(f * size * size + y * size + x)] =
                    Vector3(static_cast<float>(f), static_cast<float>(x), static_cast<float>(y));
    for (int f = 0; f < 6; ++f)
    {
        const int x = 3 + f, y = 11 - f;
        const Vector3 d = CNA::Graphics::EnvironmentProcessor::faceDirection(
            f, (static_cast<float>(x) + 0.5f) / size, (static_cast<float>(y) + 0.5f) / size);
        const Vector3 got = sampleCube(faces, size, d);
        check(std::fabs(got.X - static_cast<float>(f)) < 1e-3f && std::fabs(got.Y - static_cast<float>(x)) < 1e-2f
                  && std::fabs(got.Z - static_cast<float>(y)) < 1e-2f,
              "sampleCube maps face " + std::to_string(f) + " texel back to itself (got " + std::to_string(got.X) + ","
                  + std::to_string(got.Y) + "," + std::to_string(got.Z) + ")");
    }

    // A uniform environment: irradiance / pi equals the radiance, and the
    // prefiltered value equals it at every roughness.
    const std::vector<Vector3> uniform = uniformCube(size, Vector3(0.5f, 0.25f, 1.0f));
    const std::vector<Vector3> irradiance = convolveIrradiance(uniform, size, 8, 8);
    for (const Vector3& e : irradiance)
        check(std::fabs(e.X - 0.5f) < 0.03f && std::fabs(e.Y - 0.25f) < 0.02f && std::fabs(e.Z - 1.0f) < 0.06f,
              "uniform irradiance stays the radiance (got " + std::to_string(e.X) + "," + std::to_string(e.Y) + "," + std::to_string(e.Z) + ")");
    for (const float roughness : {0.06f, 0.5f, 0.94f})
    {
        const std::vector<Vector3> filtered = prefilterSpecular(uniform, size, 8, roughness, 32);
        for (const Vector3& v : filtered)
            check(std::fabs(v.X - 0.5f) < 1e-3f && std::fabs(v.Z - 1.0f) < 1e-3f,
                  "uniform prefilter at roughness " + std::to_string(roughness) + " stays the radiance");
    }

    // One bright face: the sharp class keeps it bright at its centre and the
    // rough class spreads it (lower peak, non-zero on the neighbouring faces).
    std::vector<Vector3> bright = uniformCube(size, Vector3::Zero);
    for (int i = 0; i < size * size; ++i) bright[static_cast<std::size_t>(i)] = Vector3(10.0f, 10.0f, 10.0f);   // +X face
    const std::vector<Vector3> sharp = prefilterSpecular(bright, size, 8, 0.06f, 64);
    const std::vector<Vector3> rough = prefilterSpecular(bright, size, 8, 0.94f, 64);
    const std::size_t centre = static_cast<std::size_t>(0 * 64 + 3 * 8 + 3);   // +X face, near the middle
    const std::size_t sideFace = static_cast<std::size_t>(4 * 64 + 3 * 8 + 3);  // +Z face centre
    check(sharp[centre].X > 9.0f, "sharp prefilter keeps the bright face (got " + std::to_string(sharp[centre].X) + ")");
    check(rough[centre].X < sharp[centre].X, "rough prefilter lowers the peak (got " + std::to_string(rough[centre].X) + ")");
    check(rough[sideFace].X > 0.3f && sharp[sideFace].X < 0.5f,
          "rough prefilter spreads onto the neighbouring face (rough " + std::to_string(rough[sideFace].X) + ", sharp "
              + std::to_string(sharp[sideFace].X) + ")");

    if (failures == 0) std::printf("cna_room_cube_tests: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
