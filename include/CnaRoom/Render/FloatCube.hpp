// SPDX-License-Identifier: MIT
#pragma once

#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <memory>
#include <string>
#include <vector>

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDevice;
    class IndexBuffer;
    class RenderTarget2D;
    class RenderTargetCube;
    class ShaderEffect;
    class Texture2D;
    class TextureCube;
    class VertexBuffer;
    namespace PackedVector { struct HalfVector4; }
}

namespace CnaRoom {

/// Legacy half-float cube uploader from before CNA supported direct half-float
/// TextureCube transfers (CNA_FINDINGS R-21). It draws an RGBE strip into
/// RenderTargetCube faces and self-tests the result. Keep it until a direct
/// upload path has been checked for face orientation and night-time lighting.
class FloatCubeUploader
{
public:
    explicit FloatCubeUploader(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);
    ~FloatCubeUploader();

    /// True once the self-test has passed; false disables the path (see reason()).
    [[nodiscard]] bool supported();
    [[nodiscard]] const std::string& reason() const { return reason_; }

    /// faces: 6 * size * size RGB values, face-major (+X -X +Y -Y +Z -Z), rows
    /// top to bottom, the layout EnvironmentProcessor::faceDirection reads.
    /// Returns null when unsupported or the upload fails.
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::RenderTargetCube>
    upload(const std::vector<Microsoft::Xna::Framework::Vector3>& faces, int size);

private:
    bool uploadInto(Microsoft::Xna::Framework::Graphics::RenderTargetCube& cube,
                    const std::vector<Microsoft::Xna::Framework::Vector3>& faces, int size, bool flipV);
    bool verify(Microsoft::Xna::Framework::Graphics::RenderTargetCube& cube,
                const std::vector<Microsoft::Xna::Framework::Vector3>& faces, int size, float& worstError);
    void sampleFace(Microsoft::Xna::Framework::Graphics::TextureCube& cube, int face,
                    std::vector<Microsoft::Xna::Framework::Graphics::PackedVector::HalfVector4>& pixels);
    void drawQuad(Microsoft::Xna::Framework::Graphics::ShaderEffect& effect);
    void selfTest();

    Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect> decode_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect> probe_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::VertexBuffer> quad_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::IndexBuffer> quadIndices_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> strip_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::RenderTarget2D> readback_;
    int stripSize_ = 0;
    bool tested_ = false;
    bool supported_ = false;
    bool flipV_ = false;
    std::string reason_;
};

}  // namespace CnaRoom
