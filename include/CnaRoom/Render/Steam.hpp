// SPDX-License-Identifier: MIT
#pragma once

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <memory>
#include <string>

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDevice;
    class IndexBuffer;
    class ShaderEffect;
    class VertexBuffer;
}

namespace CnaRoom {

/**
 * @brief Steam over a hot drink: a small plume of soft billboards animated in the vertex shader.
 *
 * A fixed set of puffs is born on the cup's rim, rises a quarter of a metre
 * over a couple of seconds, sways, grows and thins; the fragment shader cuts
 * each puff into wisps with a little noise that drifts upward. Lit as a white
 * scattering medium by the irradiance at the cup (the nearest probe's mean),
 * so the plume sits in the frame's exposure by day and by lamplight alike.
 * Drawn after the opaque pass with depth testing, premultiplied alpha.
 */
class Steam
{
public:
    struct Params
    {
        Microsoft::Xna::Framework::Vector3 origin;      ///< where the puffs are born (world): a rim, a chimney pot
        float radius = 0.035f;                          ///< the birth disc's radius
        float rise = 0.22f;                             ///< how high a puff climbs over its life (m)
        float life = 2.4f;                              ///< seconds from birth to gone
        float size0 = 0.016f, size1 = 0.08f;            ///< a puff's half size at birth and at the end (m)
        Microsoft::Xna::Framework::Vector3 drift;       ///< the wind: metres per second the puffs are carried
        Microsoft::Xna::Framework::Vector3 radiance;    ///< what a puff scatters (scene units)
        float strength = 1.0f;                          ///< 0..1 opacity scale (0 draws nothing)
        float opacity = 0.8f;                           ///< a puff's peak alpha before the wisps cut it
        float time = 0.0f;
        Microsoft::Xna::Framework::Matrix viewProjection;
        Microsoft::Xna::Framework::Vector3 cameraRight;
        Microsoft::Xna::Framework::Vector3 cameraUp;
    };

    explicit Steam(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, int puffs = 24);
    ~Steam();
    Steam(const Steam&) = delete;
    Steam& operator=(const Steam&) = delete;

    [[nodiscard]] bool supported() const { return supported_; }
    [[nodiscard]] const std::string& reason() const { return reason_; }

    /// Draws into the current target with depth testing (after the opaque pass).
    void draw(const Params& params);

private:
    Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect> effect_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::VertexBuffer> vertices_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::IndexBuffer> indices_;
    int puffs_ = 0;
    bool supported_ = false;
    std::string reason_;
};

}  // namespace CnaRoom
