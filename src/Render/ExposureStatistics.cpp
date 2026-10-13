// SPDX-License-Identifier: MIT
#include "CnaRoom/Render/ExposureStatistics.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace CnaRoom {

ExposureStatistics::Reading ExposureStatistics::evaluate(std::span<const float> cells, const int width, const int height)
{
    if (width <= 0 || height <= 0)
        throw std::invalid_argument("CnaRoom::ExposureStatistics::evaluate: the cell grid must not be empty");
    const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (cells.size() != count)
        throw std::invalid_argument("CnaRoom::ExposureStatistics::evaluate: cell count does not match the grid");

    // A cell the GPU could not measure (not a number, infinite, negative) is
    // black rather than a poison value that takes the whole log-sum with it.
    std::vector<float> luminance(count);
    for (std::size_t i = 0; i < count; ++i)
        luminance[i] = std::isfinite(cells[i]) ? std::max(cells[i], 0.0f) : 0.0f;

    Reading reading;
    double plainSum = 0.0;
    double weightSum = 0.0, weightedSum = 0.0;
    std::vector<float> weights(count);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            const std::size_t i = static_cast<std::size_t>(y * width + x);
            const float dx = (static_cast<float>(x) + 0.5f) / static_cast<float>(width) * 2.0f - 1.0f;
            const float dy = (static_cast<float>(y) + 0.5f) / static_cast<float>(height) * 2.0f - 1.0f;
            const float w = std::exp(-(dx * dx + dy * dy) / (2.0f * 0.55f * 0.55f));
            weights[i] = w;
            const double lg = std::log(static_cast<double>(std::max(luminance[i], kFloor)));
            plainSum += lg;
            weightSum += w;
            weightedSum += w * lg;
        }
    reading.plain = static_cast<float>(std::exp(plainSum / static_cast<double>(count)));
    const float firstPass = static_cast<float>(std::exp(weightedSum / std::max(weightSum, 1e-9)));
    // Highlights: cells more than 12x above the weighted mean (shades, bulbs,
    // a bright window by night) count a twentieth. Two passes settle it.
    float mean = firstPass;
    float share = 0.0f;
    for (int pass = 0; pass < 2; ++pass)
    {
        double ws = 0.0, wl = 0.0, rejected = 0.0;
        const float limit = mean * 12.0f;
        for (std::size_t i = 0; i < count; ++i)
        {
            const float l = std::max(luminance[i], kFloor);
            float w = weights[i];
            if (l > limit)
            {
                rejected += w;
                w *= 0.05f;
            }
            ws += w;
            wl += w * std::log(static_cast<double>(l));
        }
        mean = static_cast<float>(std::exp(wl / std::max(ws, 1e-9)));
        share = static_cast<float>(rejected / std::max(weightSum, 1e-9));
    }
    reading.weighted = mean;
    reading.highlightShare = share;
    return reading;
}

std::array<std::uint8_t, 3> ExposureStatistics::encodeLuminance(const float luminance)
{
    // Mirrors luminance_cells.*.frag.glsl step for step, so the CPU reference
    // and the GPU pass agree to the quantisation of the code.
    std::uint32_t q = 0;
    if (std::isfinite(luminance) && luminance > 0.0f)
    {
        const float code = std::clamp((std::log2(luminance) - kLog2Min) / (kLog2Max - kLog2Min), 0.0f, 1.0f);
        // min(): code 1.0 rounds to 16777216.0 in float, one past the 24-bit split.
        q = std::min(static_cast<std::uint32_t>(code * static_cast<float>(kCodeMax) + 0.5f), kCodeMax);
    }
    return {static_cast<std::uint8_t>((q >> 16) & 255u), static_cast<std::uint8_t>((q >> 8) & 255u),
            static_cast<std::uint8_t>(q & 255u)};
}

float ExposureStatistics::decodeLuminance(const std::uint8_t r, const std::uint8_t g, const std::uint8_t b)
{
    const std::uint32_t q = (static_cast<std::uint32_t>(r) << 16) | (static_cast<std::uint32_t>(g) << 8)
                          | static_cast<std::uint32_t>(b);
    if (q == 0) return 0.0f;
    const double code = static_cast<double>(q) / static_cast<double>(kCodeMax);
    return static_cast<float>(std::exp2(code * (kLog2Max - kLog2Min) + kLog2Min));
}

}  // namespace CnaRoom
