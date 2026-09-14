// SPDX-License-Identifier: MIT
#pragma once

#include <memory>
#include <string>
#include <vector>

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDevice;
    class RenderTarget2D;
}
namespace CNA::Graphics {
    class ComputeShader;
    template <typename T> class StorageBufferT;
}

namespace CnaRoom {

/// Scene luminance for the exposure, measured from the HDR scene target
/// by a compute shader (one thread per 64 x 36 cell, an 8 x 8 box of taps
/// each; a draw would have to rebind the pipeline's DiscardContents scene
/// target and lose the frame) and CPU statistics: a centre-weighted
/// geometric mean that down-weights highlights far above the rest of the
/// frame (a lamp shade filling a quarter of the view must not drag the room
/// into darkness, and a window must not either). CNA's AutoExposureEXT
/// log-average is kept for comparison in the frame log.
class ExposureMeter
{
public:
    explicit ExposureMeter(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);
    ~ExposureMeter();

    [[nodiscard]] bool supported() const { return supported_; }
    [[nodiscard]] const std::string& reason() const { return reason_; }

    struct Reading
    {
        float weighted = -1.0f;      ///< centre-weighted, highlight-rejected geometric mean luminance
        float plain = -1.0f;         ///< plain geometric mean over the frame
        float highlightShare = 0.0f; ///< fraction of the weight that was down-weighted
    };
    /// Reads the scene target (linear HDR). Invalid reading when unsupported.
    Reading measure(Microsoft::Xna::Framework::Graphics::RenderTarget2D& scene);

private:
    Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
    std::unique_ptr<CNA::Graphics::ComputeShader> reducer_;
    std::unique_ptr<CNA::Graphics::StorageBufferT<float>> cells_;
    std::vector<float> luminance_;
    bool supported_ = false;
    std::string reason_;
};

}  // namespace CnaRoom
