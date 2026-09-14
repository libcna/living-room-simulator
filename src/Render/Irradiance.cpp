// SPDX-License-Identifier: MIT
#include "CnaRoom/Render/Irradiance.hpp"

#include "CNA/Graphics/EnvironmentProcessor.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

using Microsoft::Xna::Framework::MathHelper;
using Microsoft::Xna::Framework::Vector3;

namespace CnaRoom {

std::vector<Vector3> convolveIrradiance(const std::vector<Vector3>& faces, int size, int small, int out)
{
    std::vector<Vector3> result(static_cast<std::size_t>(6 * out * out), Vector3::Zero);
    const std::size_t count = static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
    if (size <= 0 || small <= 0 || out <= 0 || faces.size() != 6u * count) return result;
    small = std::min(small, size);

    struct Texel { Vector3 direction; Vector3 radiance; float solidAngle; };
    std::vector<Texel> texels;
    texels.reserve(static_cast<std::size_t>(6 * small * small));
    for (int f = 0; f < 6; ++f)
        for (int j = 0; j < small; ++j)
            for (int i = 0; i < small; ++i)
            {
                // Block average over the input texels that fall into this cell.
                const int x0 = i * size / small, x1 = std::max(x0 + 1, (i + 1) * size / small);
                const int y0 = j * size / small, y1 = std::max(y0 + 1, (j + 1) * size / small);
                Vector3 sum = Vector3::Zero;
                int n = 0;
                for (int y = y0; y < y1; ++y)
                    for (int x = x0; x < x1; ++x)
                    {
                        sum = sum + faces[static_cast<std::size_t>(f) * count + static_cast<std::size_t>(y) * static_cast<std::size_t>(size)
                                          + static_cast<std::size_t>(x)];
                        ++n;
                    }
                const float u = 2.0f * (static_cast<float>(i) + 0.5f) / static_cast<float>(small) - 1.0f;
                const float v = 2.0f * (static_cast<float>(j) + 0.5f) / static_cast<float>(small) - 1.0f;
                Vector3 d = CNA::Graphics::EnvironmentProcessor::faceDirection(
                    f, (static_cast<float>(i) + 0.5f) / static_cast<float>(small), (static_cast<float>(j) + 0.5f) / static_cast<float>(small));
                d.Normalize();
                // Solid angle of a cube-face cell: (2/small)^2 / (1 + u^2 + v^2)^(3/2).
                const float solid = 4.0f / (static_cast<float>(small * small) * std::pow(1.0f + u * u + v * v, 1.5f));
                texels.push_back(Texel{d, sum * (1.0f / static_cast<float>(std::max(n, 1))), solid});
            }

    for (int f = 0; f < 6; ++f)
        for (int y = 0; y < out; ++y)
            for (int x = 0; x < out; ++x)
            {
                Vector3 n = CNA::Graphics::EnvironmentProcessor::faceDirection(
                    f, (static_cast<float>(x) + 0.5f) / static_cast<float>(out), (static_cast<float>(y) + 0.5f) / static_cast<float>(out));
                n.Normalize();
                Vector3 e = Vector3::Zero;
                for (const Texel& t : texels)
                {
                    const float c = Vector3::Dot(n, t.direction);
                    if (c > 0.0f) e = e + t.radiance * (c * t.solidAngle);
                }
                result[static_cast<std::size_t>(f * out * out + y * out + x)] = e * (1.0f / MathHelper::Pi);
            }
    return result;
}

Vector3 sampleCube(const std::vector<Vector3>& faces, int size, const Vector3& d)
{
    // Face and face-local (a, b) in -1..1, the inverse of EnvironmentProcessor::faceDirection.
    const float ax = std::abs(d.X), ay = std::abs(d.Y), az = std::abs(d.Z);
    int face;
    float a, b;
    if (ax >= ay && ax >= az)
    {
        if (d.X > 0.0f) { face = 0; a = -d.Z / ax; b = d.Y / ax; }
        else            { face = 1; a = d.Z / ax;  b = d.Y / ax; }
    }
    else if (ay >= az)
    {
        if (d.Y > 0.0f) { face = 2; a = d.X / ay; b = -d.Z / ay; }
        else            { face = 3; a = d.X / ay; b = d.Z / ay; }
    }
    else
    {
        if (d.Z > 0.0f) { face = 4; a = d.X / az;  b = d.Y / az; }
        else            { face = 5; a = -d.X / az; b = d.Y / az; }
    }
    const float u = (a + 1.0f) * 0.5f, v = (1.0f - b) * 0.5f;
    const float fx = std::clamp(u * static_cast<float>(size) - 0.5f, 0.0f, static_cast<float>(size - 1));
    const float fy = std::clamp(v * static_cast<float>(size) - 0.5f, 0.0f, static_cast<float>(size - 1));
    const int x0 = static_cast<int>(fx), y0 = static_cast<int>(fy);
    const int x1 = std::min(x0 + 1, size - 1), y1 = std::min(y0 + 1, size - 1);
    const float tx = fx - static_cast<float>(x0), ty = fy - static_cast<float>(y0);
    const std::size_t base = static_cast<std::size_t>(face) * static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
    const auto at = [&](int x, int y) { return faces[base + static_cast<std::size_t>(y) * static_cast<std::size_t>(size) + static_cast<std::size_t>(x)]; };
    const Vector3 top = at(x0, y0) * (1.0f - tx) + at(x1, y0) * tx;
    const Vector3 bottom = at(x0, y1) * (1.0f - tx) + at(x1, y1) * tx;
    return top * (1.0f - ty) + bottom * ty;
}

std::vector<Vector3> prefilterSpecular(const std::vector<Vector3>& faces, int size, int out, float roughness, int samples)
{
    std::vector<Vector3> result(static_cast<std::size_t>(6 * out * out), Vector3::Zero);
    const std::size_t count = static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
    if (size <= 0 || out <= 0 || samples <= 0 || faces.size() != 6u * count) return result;
    const float alpha = std::max(roughness * roughness, 1e-4f);
    // Hammersley points and the GGX half vectors around +Z, shared by every texel.
    struct Sample { float x, y, z; };
    std::vector<Sample> halves(static_cast<std::size_t>(samples));
    for (int i = 0; i < samples; ++i)
    {
        std::uint32_t bits = static_cast<std::uint32_t>(i);
        bits = (bits << 16u) | (bits >> 16u);
        bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
        bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
        bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
        bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
        const float e1 = (static_cast<float>(i) + 0.5f) / static_cast<float>(samples);
        const float e2 = static_cast<float>(bits) * 2.3283064365386963e-10f;
        const float phi = 2.0f * MathHelper::Pi * e1;
        const float cosTheta = std::sqrt((1.0f - e2) / (1.0f + (alpha * alpha - 1.0f) * e2));
        const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
        halves[static_cast<std::size_t>(i)] = Sample{sinTheta * std::cos(phi), sinTheta * std::sin(phi), cosTheta};
    }
    for (int f = 0; f < 6; ++f)
        for (int y = 0; y < out; ++y)
            for (int x = 0; x < out; ++x)
            {
                Vector3 n = CNA::Graphics::EnvironmentProcessor::faceDirection(
                    f, (static_cast<float>(x) + 0.5f) / static_cast<float>(out), (static_cast<float>(y) + 0.5f) / static_cast<float>(out));
                n.Normalize();
                // Tangent frame about n.
                const Vector3 up = std::abs(n.Z) < 0.999f ? Vector3(0.0f, 0.0f, 1.0f) : Vector3(1.0f, 0.0f, 0.0f);
                Vector3 t = Vector3::Cross(up, n);
                t.Normalize();
                const Vector3 bt = Vector3::Cross(n, t);
                Vector3 sum = Vector3::Zero;
                float weight = 0.0f;
                for (const Sample& h : halves)
                {
                    const Vector3 half = t * h.x + bt * h.y + n * h.z;
                    const float vDotH = Vector3::Dot(n, half);
                    Vector3 l = half * (2.0f * vDotH) - n;
                    const float nDotL = Vector3::Dot(n, l);
                    if (nDotL <= 0.0f) continue;
                    l.Normalize();
                    sum = sum + sampleCube(faces, size, l) * nDotL;
                    weight += nDotL;
                }
                result[static_cast<std::size_t>(f * out * out + y * out + x)] = weight > 1e-9f ? sum * (1.0f / weight) : Vector3::Zero;
            }
    return result;
}

}  // namespace CnaRoom
