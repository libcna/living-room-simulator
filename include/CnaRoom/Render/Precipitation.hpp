// SPDX-License-Identifier: MIT
#pragma once

#include "Microsoft/Xna/Framework/BoundingBox.hpp"
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
 * @brief Rain, snow and hail as one static particle mesh animated in the vertex shader.
 *
 * A fixed set of particles lives in a wrapping volume; each one falls at the
 * type's speed with the wind's drift and re-enters at the top. The vertex
 * shader builds a camera-facing quad stretched along the fall direction for
 * streaks, or a round sprite for snow and hail, so nothing is uploaded per
 * frame. Radiance is scene-referred: the streaks add a fraction of the sky's
 * ambient light (and the street lights at night) over the background.
 */
class Precipitation
{
public:
    enum class Kind { None, Rain, Snow, Hail };

    struct Params
    {
        Kind kind = Kind::None;
        float intensity = 0.0f;                 ///< 0..1 fraction of the particles shown
        Microsoft::Xna::Framework::Vector3 wind{0.0f, 0.0f, 0.0f};   ///< m/s, horizontal
        Microsoft::Xna::Framework::Vector3 radiance{0.1f, 0.1f, 0.1f};///< what a lit drop reflects
        Microsoft::Xna::Framework::BoundingBox volume;               ///< where the particles live
        float timeSeconds = 0.0f;
    };

    explicit Precipitation(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, int particles = 9000);
    ~Precipitation();
    Precipitation(const Precipitation&) = delete;
    Precipitation& operator=(const Precipitation&) = delete;

    [[nodiscard]] bool supported() const { return supported_; }
    [[nodiscard]] const std::string& reason() const { return reason_; }

    /// Draws into the current target with depth testing (call after the opaque pass).
    void draw(const Params& params, const Microsoft::Xna::Framework::Matrix& view,
              const Microsoft::Xna::Framework::Matrix& projection,
              const Microsoft::Xna::Framework::Vector3& cameraPosition);

private:
    Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::VertexBuffer> vertices_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::IndexBuffer> indices_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect> effect_;
    int particles_ = 0;
    bool supported_ = false;
    std::string reason_;
};

}  // namespace CnaRoom
