// SPDX-License-Identifier: MIT
#pragma once

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <memory>
#include <string>
#include <vector>

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDevice;
    class RenderTarget2D;
    class ShaderEffect;
    class Texture2D;
}
namespace CNA::Graphics { class FullscreenPass; }

namespace CnaRoom {

/**
 * @brief What the television shows: a small render target refreshed every frame.
 *
 * Synthetic programming drawn by a fragment shader (nothing photographic, so
 * nothing to license): a slow-panning landscape with drifting clouds and a
 * setting sun, a night drive down a dark road, a studio set of coloured panels
 * with a moving chart and a ticker, and now and then a colour test card, each
 * fading up from black; the dark stretches let the room's light dip. The image
 * is written sRGB-encoded because it is bound as an emissive map, which the
 * PBR shader decodes; a faint scanline flicker keeps it from reading as a
 * still picture.
 */
class TelevisionContent
{
public:
    explicit TelevisionContent(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, int width = 480, int height = 270);
    ~TelevisionContent();
    TelevisionContent(const TelevisionContent&) = delete;
    TelevisionContent& operator=(const TelevisionContent&) = delete;

    [[nodiscard]] bool supported() const { return supported_; }
    [[nodiscard]] const std::string& reason() const { return reason_; }
    /// Redraws the picture for `seconds` (call once per frame while the set is on).
    void update(float seconds);
    [[nodiscard]] Microsoft::Xna::Framework::Graphics::Texture2D* texture() const;
    /// The picture's mean colour (linear), from a 32x18 render of the same
    /// programme read back after each update(): the light the set throws
    /// into the room. False until the first readback (or after it failed).
    [[nodiscard]] bool hasMean() const { return meanValid_; }
    [[nodiscard]] const Microsoft::Xna::Framework::Vector3& meanColour() const { return mean_; }

private:
    void readMean();
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::RenderTarget2D> meanTarget_;
    std::vector<Microsoft::Xna::Framework::Color> meanPixels_;
    Microsoft::Xna::Framework::Vector3 mean_;
    bool meanValid_ = false;
    bool meanFailed_ = false;
    Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::RenderTarget2D> target_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect> effect_;
    std::unique_ptr<CNA::Graphics::FullscreenPass> pass_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> white_;
    int width_, height_;
    bool supported_ = false;
    std::string reason_;
};

}  // namespace CnaRoom
