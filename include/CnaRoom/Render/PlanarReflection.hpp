// SPDX-License-Identifier: MIT
#pragma once

#include "Microsoft/Xna/Framework/BoundingFrustum.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Plane.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <array>
#include <memory>
#include <string>

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDevice;
    class RenderTarget2D;
    class ShaderEffect;
    class Texture2D;
}

namespace CnaRoom {

class Camera;
class GpuMesh;
struct Material;

/// A mirror plane in the scene: the normal faces the room, the corners bound
/// the reflective surfaces that share it (for visibility only).
struct ReflectionPlane
{
    std::string name;
    Microsoft::Xna::Framework::Plane plane;
    std::array<Microsoft::Xna::Framework::Vector3, 4> corners{};
    float minExposure = 0.0f;   ///< capture only from this exposure up (window panes: night)
    bool exteriorOnly = false;  ///< capture the street and sky only (a wet road mirrors nothing indoors)
    float skipBelow = -1.0e9f;  ///< items whose bounds top out under this height are left out (the ground under a wet road)
    bool enabled = true;        ///< switched by the scene (the road only while it is wet)
};

/// Planar reflections: the scene is rendered once more from the camera
/// mirrored in the plane, with the near plane laid onto the mirror (oblique
/// projection, so nothing behind the glass leaks in), into a half-float
/// target; a reflective surface then projects its world position through
/// that render's view-projection to look its reflection up. Mirrors and the
/// television screen use it; the probes' 32 px prefiltered cube gave them a
/// blurred blob instead of the room.
class PlanarReflection
{
public:
    PlanarReflection(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, int width, int height);
    ~PlanarReflection();

    [[nodiscard]] bool supported() const { return supported_; }
    [[nodiscard]] const std::string& reason() const { return reason_; }
    void resize(int width, int height);
    [[nodiscard]] int width() const { return width_; }
    [[nodiscard]] int height() const { return height_; }

    /// Builds the mirrored view, the oblique projection (off-centre, tightened
    /// to the plane's corners so the capture spends its pixels on the visible
    /// glass) and their frustum for the given camera. False when the camera
    /// is behind the plane or none of the plane's corners is in view.
    bool prepare(const ReflectionPlane& plane, const Camera& camera);
    [[nodiscard]] const Microsoft::Xna::Framework::Matrix& view() const { return view_; }
    [[nodiscard]] const Microsoft::Xna::Framework::Matrix& projection() const { return projection_; }
    [[nodiscard]] const Microsoft::Xna::Framework::BoundingFrustum& frustum() const { return frustum_; }
    [[nodiscard]] const Microsoft::Xna::Framework::Vector3& mirroredPosition() const { return mirroredPosition_; }
    [[nodiscard]] Microsoft::Xna::Framework::Graphics::RenderTarget2D* target() const { return target_.get(); }
    [[nodiscard]] bool valid() const { return valid_; }
    void invalidate() { valid_ = false; }

    /// Binds the capture target and clears it; endCapture() restores the back buffer.
    void beginCapture();
    void endCapture();

    /// Draws a reflective surface with the main camera. The emissive texture
    /// (the television's picture) is added on top of the reflection.
    /// Additive: a pane over the scene already drawn (reflection only, no depth write).
    void drawSurface(const Material& material, const Microsoft::Xna::Framework::Matrix& world,
                     const Microsoft::Xna::Framework::Matrix& view, const Microsoft::Xna::Framework::Matrix& projection,
                     const Microsoft::Xna::Framework::Vector3& cameraPosition, const GpuMesh& mesh, bool encodeSrgb,
                     bool additive = false);

private:
    Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::RenderTarget2D> target_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect> effect_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> black_;
    int width_ = 0, height_ = 0;
    bool supported_ = false;
    bool valid_ = false;
    bool loggedOblique_ = false;
    std::string reason_;
    Microsoft::Xna::Framework::Matrix view_{};
    Microsoft::Xna::Framework::Matrix projection_{};
    Microsoft::Xna::Framework::Matrix viewProjection_{};
    Microsoft::Xna::Framework::BoundingFrustum frustum_{Microsoft::Xna::Framework::Matrix::getIdentityProperty()};
    Microsoft::Xna::Framework::Vector3 mirroredPosition_{};
    Microsoft::Xna::Framework::Vector3 planeNormal_{};
};

}  // namespace CnaRoom
