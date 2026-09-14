// SPDX-License-Identifier: MIT
#include "CnaRoom/Render/CameraController.hpp"

#include "Microsoft/Xna/Framework/Input/Keys.hpp"

#include <algorithm>
#include <cmath>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Input;

namespace CnaRoom {

void CameraController::update(Camera& camera, const KeyboardState& keyboard, float deltaSeconds)
{
    const float dt = std::clamp(deltaSeconds, 0.0f, 0.1f);

    float yaw = camera.yaw();
    float pitch = camera.pitch();
    const float turn = settings_.turnSpeed * dt
                       * (keyboard.IsKeyDown(Keys::LeftControl) ? settings_.precisionMultiplier
                                                                 : 1.0f);
    if (keyboard.IsKeyDown(Keys::Left)) yaw += turn;
    if (keyboard.IsKeyDown(Keys::Right)) yaw -= turn;
    if (keyboard.IsKeyDown(Keys::Up)) pitch += turn;
    if (keyboard.IsKeyDown(Keys::Down)) pitch -= turn;
    camera.setOrientation(yaw, pitch);

    Vector3 wish = Vector3::Zero;
    if (keyboard.IsKeyDown(Keys::W)) wish = wish + camera.groundForward();
    if (keyboard.IsKeyDown(Keys::S)) wish = wish - camera.groundForward();
    if (keyboard.IsKeyDown(Keys::D)) wish = wish + camera.right();
    if (keyboard.IsKeyDown(Keys::A)) wish = wish - camera.right();
    if (keyboard.IsKeyDown(Keys::E)) wish = wish + Vector3::Up;
    if (keyboard.IsKeyDown(Keys::Q)) wish = wish - Vector3::Up;

    const float length = wish.Length();
    if (length > 1e-4f) wish = wish * (1.0f / length);

    float speed = settings_.walkSpeed;
    if (keyboard.IsKeyDown(Keys::LeftShift)) speed *= settings_.sprintMultiplier;
    if (keyboard.IsKeyDown(Keys::LeftControl)) speed *= settings_.precisionMultiplier;
    const Vector3 target = wish * speed;

    // Exponential smoothing towards the wanted velocity: frame-rate independent.
    const float blend = 1.0f - std::exp(-settings_.acceleration * dt);
    velocity_ = velocity_ + (target - velocity_) * blend;
    if (velocity_.Length() < 1e-4f && length <= 1e-4f) velocity_ = Vector3::Zero;

    camera.setPosition(camera.position() + velocity_ * dt);
}

void CameraController::reset()
{
    velocity_ = Vector3::Zero;
}

}  // namespace CnaRoom
