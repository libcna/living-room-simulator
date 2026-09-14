// SPDX-License-Identifier: MIT
#include "CnaRoom/Render/Camera.hpp"

#include <algorithm>
#include <cmath>

using namespace Microsoft::Xna::Framework;

namespace CnaRoom {

void Camera::setPosition(const Vector3& position)
{
    position_ = position;
    dirty_ = true;
}

void Camera::setOrientation(float yawRadians, float pitchRadians)
{
    yaw_ = std::remainder(yawRadians, 6.28318530718f);
    pitch_ = std::clamp(pitchRadians, -kMaxPitch, kMaxPitch);
    dirty_ = true;
}

void Camera::setPerspective(float verticalFovRadians, float aspect, float nearPlane, float farPlane)
{
    fov_ = verticalFovRadians;
    aspect_ = aspect;
    near_ = nearPlane;
    far_ = farPlane;
    dirty_ = true;
}

void Camera::setAspect(float aspect)
{
    aspect_ = aspect;
    dirty_ = true;
}

Vector3 Camera::forward() const
{
    const float cp = std::cos(pitch_);
    return Vector3(-std::sin(yaw_) * cp, std::sin(pitch_), -std::cos(yaw_) * cp);
}

Vector3 Camera::right() const
{
    return Vector3(std::cos(yaw_), 0.0f, -std::sin(yaw_));
}

Vector3 Camera::groundForward() const
{
    return Vector3(-std::sin(yaw_), 0.0f, -std::cos(yaw_));
}

const Matrix& Camera::view() const
{
    if (dirty_) rebuild();
    return view_;
}

const Matrix& Camera::projection() const
{
    if (dirty_) rebuild();
    return projection_;
}

const BoundingFrustum& Camera::frustum() const
{
    if (dirty_) rebuild();
    return frustum_;
}

Matrix Camera::projectionForRange(float nearPlane, float farPlane) const
{
    return Matrix::CreatePerspectiveFieldOfView(fov_, aspect_, nearPlane, farPlane);
}

void Camera::rebuild() const
{
    view_ = Matrix::CreateLookAt(position_, position_ + forward(), Vector3::Up);
    projection_ = Matrix::CreatePerspectiveFieldOfView(fov_, aspect_, near_, far_);
    frustum_ = BoundingFrustum(view_ * projection_);
    dirty_ = false;
}

}  // namespace CnaRoom
