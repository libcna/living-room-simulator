// SPDX-License-Identifier: MIT
#pragma once

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <memory>
#include <string>

namespace Microsoft::Xna::Framework::Graphics {
    class BlendState;
    class GraphicsDevice;
    class IndexBuffer;
    class RenderTarget2D;
    class ShaderEffect;
    class Texture2D;
    class VertexBuffer;
}
namespace CNA::Graphics {
    class ContactShadowPass;
}

namespace CnaRoom {

/// Screen-space contact shadows for the sun: CNAEXT's `ContactShadowPass`
/// marched over the prepass depth before the frame opens (into a visibility
/// mask, a white source darkened where a short ray toward the sun meets a
/// nearer surface), then multiplied into the lit scene after the opaque pass
/// with a Zero/SourceColor blend, since the pipeline's scene target cannot be
/// read back and rebound mid-frame.
class ContactShadows
{
public:
    struct Inputs
    {
        Microsoft::Xna::Framework::Graphics::Texture2D* depth = nullptr;     ///< the prepass depth
        Microsoft::Xna::Framework::Graphics::Texture2D* normals = nullptr;   ///< the prepass view-space normals (n * 0.5 + 0.5)
        Microsoft::Xna::Framework::Matrix view;
        Microsoft::Xna::Framework::Matrix projection;
        Microsoft::Xna::Framework::Matrix inverseProjection;
        Microsoft::Xna::Framework::Matrix inverseView;
        float nearPlane = 0.05f;
        float farPlane = 16.0f;
        Microsoft::Xna::Framework::Vector3 lightDirection;   ///< the direction the light travels
        float intensity = 0.5f;      ///< how dark a full hit goes (0..1)
        float maxDistance = 0.12f;   ///< metres the ray walks toward the light
        float thickness = 0.10f;     ///< assumed occluder thickness, metres
        float bias = 0.015f;         ///< self-shadowing tolerance, metres
        int steps = 12;
        int debug = 0;               ///< 1 paints the mask instead of multiplying it
    };

    explicit ContactShadows(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);
    ~ContactShadows();

    [[nodiscard]] bool supported() const { return supported_; }
    [[nodiscard]] const std::string& reason() const { return reason_; }

    /// Marches the mask; call before the pipeline opens the frame (the prepass must be drawn).
    void march(const Inputs& in, int width, int height);
    /// Multiplies the mask into the bound scene target; call after the opaque pass.
    void apply();

private:
    bool ensureMask(int width, int height);
    void drawQuad();

    Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
    std::unique_ptr<CNA::Graphics::ContactShadowPass> pass_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect> copyEffect_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::VertexBuffer> quad_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::IndexBuffer> quadIndices_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> white_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::RenderTarget2D> mask_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::BlendState> multiply_;
    Microsoft::Xna::Framework::Graphics::Texture2D* normals_ = nullptr;
    Microsoft::Xna::Framework::Vector3 lightView_;   ///< toward the light, view space
    int maskWidth_ = 0, maskHeight_ = 0;
    bool maskFailed_ = false;
    bool marched_ = false;
    int debug_ = 0;
    bool supported_ = false;
    bool loggedFallback_ = false;
    std::string reason_;
};

}  // namespace CnaRoom
