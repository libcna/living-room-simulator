// SPDX-License-Identifier: MIT
#pragma once

#include <memory>
#include <string>

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
 * setting sun, cut every so often to a studio set of coloured panels with a
 * moving chart and a ticker, and now and then a colour test card. The image
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

private:
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
