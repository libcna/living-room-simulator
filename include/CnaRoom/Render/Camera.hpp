// SPDX-License-Identifier: MIT
#pragma once

#include "Microsoft/Xna/Framework/BoundingFrustum.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

namespace CnaRoom {

/**
 * @brief A perspective camera described by position, yaw and pitch (no roll).
 *
 * Units are metres, Y is up, yaw 0 looks down -Z (XNA forward). Pitch is
 * clamped short of the poles so the view matrix never degenerates.
 */
class Camera
{
public:
    void setPosition(const Microsoft::Xna::Framework::Vector3& position);
    void setOrientation(float yawRadians, float pitchRadians);
    void setPerspective(float verticalFovRadians, float aspect, float nearPlane, float farPlane);
    void setAspect(float aspect);

    [[nodiscard]] const Microsoft::Xna::Framework::Vector3& position() const { return position_; }
    [[nodiscard]] float yaw() const { return yaw_; }
    [[nodiscard]] float pitch() const { return pitch_; }
    [[nodiscard]] float verticalFov() const { return fov_; }
    [[nodiscard]] float nearPlane() const { return near_; }
    [[nodiscard]] float farPlane() const { return far_; }
    [[nodiscard]] float aspect() const { return aspect_; }

    [[nodiscard]] Microsoft::Xna::Framework::Vector3 forward() const;
    [[nodiscard]] Microsoft::Xna::Framework::Vector3 right() const;
    /// Forward projected onto the ground plane, for walking-style movement.
    [[nodiscard]] Microsoft::Xna::Framework::Vector3 groundForward() const;

    [[nodiscard]] const Microsoft::Xna::Framework::Matrix& view() const;
    [[nodiscard]] const Microsoft::Xna::Framework::Matrix& projection() const;
    [[nodiscard]] const Microsoft::Xna::Framework::BoundingFrustum& frustum() const;
    /// The same perspective with a different depth range (shadow fitting).
    [[nodiscard]] Microsoft::Xna::Framework::Matrix projectionForRange(float nearPlane,
                                                                      float farPlane) const;

    static constexpr float kMaxPitch = 1.5533f;  // 89 degrees

private:
    void rebuild() const;

    Microsoft::Xna::Framework::Vector3 position_{0.0f, 1.6f, 0.0f};
    float yaw_ = 0.0f;
    float pitch_ = 0.0f;
    float fov_ = 1.0472f;  // 60 degrees vertical
    float aspect_ = 16.0f / 9.0f;
    float near_ = 0.05f;
    float far_ = 400.0f;

    mutable bool dirty_ = true;
    mutable Microsoft::Xna::Framework::Matrix view_{};
    mutable Microsoft::Xna::Framework::Matrix projection_{};
    mutable Microsoft::Xna::Framework::BoundingFrustum frustum_{
        Microsoft::Xna::Framework::Matrix::getIdentityProperty()};
};

}  // namespace CnaRoom
