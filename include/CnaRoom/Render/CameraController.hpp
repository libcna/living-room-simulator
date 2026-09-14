// SPDX-License-Identifier: MIT
#pragma once

#include "CnaRoom/Render/Camera.hpp"

#include "Microsoft/Xna/Framework/Input/KeyboardState.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

namespace CnaRoom {

/**
 * @brief Free-flying camera controls, frame-rate independent.
 *
 * W/S/A/D move, arrow left/right turn, arrow up/down look, Q/E move down/up,
 * Shift sprints, Ctrl is precise. Velocity is smoothed with a short time
 * constant so movement starts and stops without a jolt.
 */
class CameraController
{
public:
    struct Settings
    {
        float walkSpeed = 1.5f;         ///< m/s
        float sprintMultiplier = 3.0f;
        float precisionMultiplier = 0.25f;
        float turnSpeed = 1.6f;         ///< rad/s
        float acceleration = 12.0f;     ///< 1/s, velocity smoothing
    };

    void update(Camera& camera, const Microsoft::Xna::Framework::Input::KeyboardState& keyboard,
                float deltaSeconds);
    void reset();

    [[nodiscard]] const Settings& settings() const { return settings_; }
    [[nodiscard]] Settings& settings() { return settings_; }
    [[nodiscard]] const Microsoft::Xna::Framework::Vector3& velocity() const { return velocity_; }

private:
    Settings settings_;
    Microsoft::Xna::Framework::Vector3 velocity_{0.0f, 0.0f, 0.0f};
};

}  // namespace CnaRoom
