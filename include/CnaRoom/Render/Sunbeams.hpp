// SPDX-License-Identifier: MIT
#pragma once

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <array>
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

namespace CnaRoom {

/**
 * @brief Sunlight scattered by the air in the room: beams through the windows.
 *
 * A fullscreen pass drawn additively over the opaque HDR scene. Each pixel's
 * camera ray is clipped to the room's box and marched in steps; at each step
 * the cascaded shadow atlas says whether the key light reaches that point in
 * the air, and the lit steps add in-scattered light (Henyey-Greenstein phase,
 * forward-biased, so the beam glows most when the view looks toward the
 * window). The march ends at the prepass depth, so nothing is drawn over a
 * surface nearer than the air. The medium is confined to the room: the sky's
 * haze answers for the outside.
 *
 * The pipeline's own `VolumetricFogPass` cannot be used for this: the
 * pipeline never hands it a shadow map (R-26), and its chain is private.
 */
class Sunbeams
{
public:
    struct Inputs
    {
        Microsoft::Xna::Framework::Matrix inverseViewProjection;   ///< clip -> world (the full view-projection)
        Microsoft::Xna::Framework::Vector3 cameraPosition;
        Microsoft::Xna::Framework::Vector3 cameraForward;
        float prepassFarPlane = 16.0f;
        Microsoft::Xna::Framework::Graphics::Texture2D* depth = nullptr;   ///< the prepass depth (view depth / far)
        bool depthPacked = true;
        Microsoft::Xna::Framework::Graphics::Texture2D* shadowAtlas = nullptr;
        int cascadeCount = 0;
        std::array<Microsoft::Xna::Framework::Matrix, 4> cascadeMatrices{};
        std::array<float, 4> splitDistances{};
        Microsoft::Xna::Framework::Vector2 shadowTexel{0.0f, 0.0f};
        float shadowBias = 0.006f;
        Microsoft::Xna::Framework::Vector3 lightDirection;   ///< the direction the light travels
        Microsoft::Xna::Framework::Vector3 lightColour;      ///< scene units (irradiance)
        float density = 0.05f;        ///< scattering coefficient per metre
        float anisotropy = 0.6f;      ///< Henyey-Greenstein g
        int steps = 24;
        Microsoft::Xna::Framework::Vector3 roomMin;
        Microsoft::Xna::Framework::Vector3 roomMax;
        int debug = 0;                ///< 1 paints the decoded depth, 2 the world position, 3 the shadow factor at the surface, 4 the atlas, 5 the raw scatter, 6 the ray's clip to the room
        // Dust motes: specks drifting in the room's air, drawn where the key
        // light reaches them (the same atlas compare), camera-facing.
        int motes = 400;              ///< how many of the buffer's motes to draw (0 off)
        float moteSize = 2.0f;        ///< a speck's diameter in pixels at a 540-line frame (a point of glare, whatever its distance)
        int viewportWidth = 0, viewportHeight = 0;   ///< filled by draw()
        float moteBrightness = 1.0f;
        float time = 0.0f;            ///< scene seconds for the drift
        Microsoft::Xna::Framework::Matrix viewProjection;
        Microsoft::Xna::Framework::Vector3 cameraRight;
        Microsoft::Xna::Framework::Vector3 cameraUp;
        // The march at half size: drawn into a target of its own before the
        // scene target is bound (march()), and added over the scene with a
        // bilinear upsample (the beams are low frequency; the cost is a quarter).
        bool halfResolution = true;
    };

    explicit Sunbeams(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);
    ~Sunbeams();

    [[nodiscard]] bool supported() const { return supported_; }
    [[nodiscard]] const std::string& reason() const { return reason_; }

    /// The half-size march into its own target; call with no scene target
    /// bound (it leaves the back buffer bound). draw() then adds the result.
    /// Without it (or at full size) draw() marches directly over the scene.
    void march(const Inputs& in, int width, int height);
    /// Draws the beams over the bound target (additive): the upsampled march
    /// from march(), else a full-size march; then the motes.
    void draw(const Inputs& in, int width, int height);

private:
    Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect> effect_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::VertexBuffer> quad_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::IndexBuffer> quadIndices_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect> copyEffect_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::RenderTarget2D> halfTarget_;
    int halfWidth_ = 0, halfHeight_ = 0;
    bool halfTargetFailed_ = false;
    bool halfMarched_ = false;   ///< march() filled halfTarget_ for this frame
    bool ensureHalfTarget(int width, int height);
    void marchQuad(const Inputs& in, bool opaque);
    void drawQuad();
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect> moteEffect_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::VertexBuffer> moteVertices_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::IndexBuffer> moteIndices_;
    int moteCapacity_ = 0;
    void drawMotes(const Inputs& in);
    bool supported_ = false;
    std::string reason_;
};

}  // namespace CnaRoom
