// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <vector>

namespace CnaRoom::Assets {

/// A CPU RGBA8 image with helpers for procedural texture synthesis.
struct Image
{
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;

    Image() = default;
    Image(int w, int h) : width(w), height(h), rgba(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u, 255) {}

    [[nodiscard]] std::size_t at(int x, int y) const
    {
        x = ((x % width) + width) % width;
        y = ((y % height) + height) % height;
        return (static_cast<std::size_t>(y) * static_cast<std::size_t>(width)
                + static_cast<std::size_t>(x)) * 4u;
    }
    void set(int x, int y, float r, float g, float b, float a = 1.0f);
    /// Downsamples by 2 with a box filter; sRGB images are averaged in linear light.
    [[nodiscard]] Image halved(bool srgb) const;
};

/// A single-channel float field, the working representation of noise, height
/// and masks before they are packed into channels.
struct Field
{
    int width = 0;
    int height = 0;
    std::vector<float> values;

    Field() = default;
    Field(int w, int h, float fill = 0.0f)
        : width(w), height(h), values(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), fill) {}

    [[nodiscard]] float at(int x, int y) const
    {
        x = ((x % width) + width) % width;
        y = ((y % height) + height) % height;
        return values[static_cast<std::size_t>(y) * static_cast<std::size_t>(width)
                      + static_cast<std::size_t>(x)];
    }
    float& ref(int x, int y)
    {
        return values[static_cast<std::size_t>(y) * static_cast<std::size_t>(width)
                      + static_cast<std::size_t>(x)];
    }
    /// Bilinear sample with wrap, in texel units.
    [[nodiscard]] float sample(float x, float y) const;
};

/// Deterministic hash-based noise (tileable when the lattice period divides the size).
namespace Noise {
    [[nodiscard]] float hash(int x, int y, std::uint32_t seed);
    /// Value noise on a lattice with the given period (tileable); coordinates in lattice units.
    [[nodiscard]] float value(float x, float y, int period, std::uint32_t seed);
    /// Fractal sum of value noise; frequency in lattice cells across the unit square.
    [[nodiscard]] float fbm(float u, float v, int baseFrequency, int octaves, float gain,
                            std::uint32_t seed);
    /// Worley/cellular distance (F1) tileable.
    [[nodiscard]] float cellular(float u, float v, int frequency, std::uint32_t seed);
}

/// Converts a height field into a tangent-space normal map (Y up in texture space,
/// glTF convention: green points up).
[[nodiscard]] Image normalMapFromHeight(const Field& height, float strength);

}  // namespace CnaRoom::Assets
