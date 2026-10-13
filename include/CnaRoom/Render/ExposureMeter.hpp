// SPDX-License-Identifier: MIT
#pragma once

#include "CnaRoom/Render/ExposureStatistics.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"

#include <memory>
#include <string>
#include <vector>

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDevice;
    class RenderTarget2D;
    class ShaderEffect;
}
namespace CnaRoom::Effects {
    class FullscreenPass;
}

namespace CnaRoom {

/// Scene luminance for the exposure, measured from the HDR scene target: one
/// fullscreen vertex/fragment pass draws the frame into a 64 x 36 RGBA8 target
/// (each fragment the mean of an 8 x 8 box of taps over its cell, as Rec.709
/// luminance, packed as a 24-bit log2 code), a plain Color readback brings the
/// cells to the CPU, and ExposureStatistics turns them into the centre-weighted,
/// highlight-rejecting reading the exposure uses and the plain geometric mean
/// the auto exposure adapts to. The whole frame is one reading, so both get
/// it from the same pass.
class ExposureMeter
{
public:
    using Reading = ExposureStatistics::Reading;

    explicit ExposureMeter(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);
    ~ExposureMeter();
    ExposureMeter(const ExposureMeter&) = delete;
    ExposureMeter& operator=(const ExposureMeter&) = delete;

    [[nodiscard]] bool supported() const { return supported_; }
    [[nodiscard]] const std::string& reason() const { return reason_; }

    /// Reads the scene target (linear HDR). It binds its own target for the
    /// duration and restores what was bound, so call it with the scene target
    /// unbound (after the frame's post chain). Invalid reading when unsupported.
    Reading measure(Microsoft::Xna::Framework::Graphics::RenderTarget2D& scene);

private:
    Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
    std::unique_ptr<CnaRoom::Effects::FullscreenPass> pass_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect> effect_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::RenderTarget2D> cells_;
    std::vector<Microsoft::Xna::Framework::Color> texels_;
    std::vector<float> luminance_;
    bool supported_ = false;
    std::string reason_;
};

}  // namespace CnaRoom
