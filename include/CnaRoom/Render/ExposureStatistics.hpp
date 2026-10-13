// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace CnaRoom {

/// The CPU half of the exposure meter: statistics over a grid of cell
/// luminances, and the codec for the RGBA8 cells the GPU pass writes. No
/// graphics device is involved, so every number here is unit-testable.
///
/// The statistics are a centre-weighted geometric mean that down-weights
/// highlights far above the rest of the frame (a lamp shade filling a quarter
/// of the view must not drag the room into darkness, and a window must not
/// either), next to the plain geometric mean the auto exposure adapts to.
class ExposureStatistics
{
public:
    /// The cell grid the meter measures: 64 x 36 cells over the frame.
    static constexpr int kCellsX = 64;
    static constexpr int kCellsY = 36;
    /// Cells below this count as this; it keeps black cells from dominating the log.
    static constexpr float kFloor = 1e-5f;
    /// The GPU pass writes log2(luminance) clamped to this range as a 24-bit code.
    static constexpr float kLog2Min = -24.0f;
    static constexpr float kLog2Max = 24.0f;
    static constexpr std::uint32_t kCodeMax = 16777215u;

    struct Reading
    {
        float weighted = -1.0f;      ///< centre-weighted, highlight-rejected geometric mean luminance
        float plain = -1.0f;         ///< plain geometric mean over the frame
        float highlightShare = 0.0f; ///< fraction of the weight that was down-weighted
        [[nodiscard]] bool valid() const { return weighted > 0.0f && plain > 0.0f; }
    };

    /// Statistics over a row-major `width` x `height` grid of linear cell
    /// luminances. A cell that is not finite or is negative counts as black.
    /// Throws std::invalid_argument when the grid is empty or the array does
    /// not match it.
    [[nodiscard]] static Reading evaluate(std::span<const float> cells, int width, int height);

    /// The 24-bit code the GPU pass writes for one cell, as the R, G, B bytes
    /// of an RGBA8 texel: log2(luminance) mapped from [kLog2Min, kLog2Max] onto
    /// [0, kCodeMax]. Zero means black or not a number.
    [[nodiscard]] static std::array<std::uint8_t, 3> encodeLuminance(float luminance);

    /// The inverse of encodeLuminance; code zero decodes to 0.
    [[nodiscard]] static float decodeLuminance(std::uint8_t r, std::uint8_t g, std::uint8_t b);
};

}  // namespace CnaRoom
