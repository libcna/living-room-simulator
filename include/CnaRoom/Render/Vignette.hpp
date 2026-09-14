// SPDX-License-Identifier: MIT
#pragma once

#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <memory>
#include <string>

namespace Microsoft::Xna::Framework::Graphics {
    class BlendState;
    class GraphicsDevice;
    class IndexBuffer;
    class ShaderEffect;
    class VertexBuffer;
}

namespace CnaRoom {

/// A lens vignette multiplied over the finished frame (the back buffer,
/// after the pipeline's tonemapping): the corners fall off as a photograph's
/// do. Drawn as a clip-space quad with a destination-times-source blend.
class Vignette
{
public:
    explicit Vignette(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);
    ~Vignette();
    [[nodiscard]] bool supported() const { return supported_; }
    [[nodiscard]] const std::string& reason() const { return reason_; }
    /// strength 0..1: how dark the corners go; width and height of the frame;
    /// gain: a per-channel multiplier over the whole frame (white balance),
    /// each at most 1.
    void draw(float strength, int width, int height, const Microsoft::Xna::Framework::Vector3& gain);

private:
    Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect> effect_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::VertexBuffer> quad_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::IndexBuffer> quadIndices_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::BlendState> multiply_;
    bool supported_ = false;
    std::string reason_;
};

}  // namespace CnaRoom
