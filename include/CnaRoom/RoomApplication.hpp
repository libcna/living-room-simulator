// SPDX-License-Identifier: MIT
#pragma once

#include "CnaRoom/Render/Camera.hpp"
#include "CnaRoom/Render/CameraController.hpp"
#include "CnaRoom/Render/Overlay.hpp"
#include "CnaRoom/Render/RenderSettings.hpp"
#include "CnaRoom/Sim/TimeOfDay.hpp"
#include "CnaRoom/Sim/WeatherSystem.hpp"

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Input/KeyboardState.hpp"

#include <memory>
#include <string>
#include <vector>

namespace Microsoft::Xna::Framework {
    class GraphicsDeviceManager;
}

namespace Microsoft::Xna::Framework::Content { class ContentManager; }

namespace CnaRoom {

class MaterialLibrary;
class ModelLibrary;
class RoomScene;
class SceneRenderer;

/**
 * @brief The application: owns the window, the device and the room.
 *
 * A thin CNA @c Game subclass. Everything that is not lifecycle plumbing lives
 * in the systems it owns, so the interesting code stays testable without a
 * graphics device.
 */
class RoomApplication : public Microsoft::Xna::Framework::Game
{
public:
    RoomApplication();
    ~RoomApplication() override;

    /// Applies command-line options. Returns false when the arguments ask for
    /// something that ends the program before it starts (--help).
    bool configure(int argc, char** argv);

protected:
    void Initialize() override;
    void LoadContent() override;
    void Update(Microsoft::Xna::Framework::GameTime& gameTime) override;
    void Draw(const Microsoft::Xna::Framework::GameTime& gameTime) override;

private:
    void captureScreenshot(const std::string& path);
    void handleHotkeys(const Microsoft::Xna::Framework::Input::KeyboardState& keyboard,
                       const Microsoft::Xna::Framework::Input::KeyboardState& previous);
    void applyViewpoint(const std::string& name);
    void updateSky();
    /// Lamps at dusk/dawn, exposure adaptation, probe re-bakes as the light changes.
    void updateSchedules(float dt);
    void updateWeather(float dt);
    [[nodiscard]] float scheduledExposure() const;
    void logFrameStats();

    std::unique_ptr<Microsoft::Xna::Framework::GraphicsDeviceManager> graphics_;
    std::unique_ptr<MaterialLibrary> materials_;
    std::unique_ptr<Microsoft::Xna::Framework::Content::ContentManager> compiledContent_;   ///< rooted at <assets>/cnb
    std::unique_ptr<ModelLibrary> models_;
    std::unique_ptr<SceneRenderer> renderer_;
    std::unique_ptr<RoomScene> scene_;

    RenderSettings settings_;
    Camera camera_;
    CameraController controller_;
    Microsoft::Xna::Framework::Input::KeyboardState previousKeyboard_;

    // Sun placement until the time-of-day system lands: elevation/azimuth in degrees.
    TimeOfDay clock_;
    WeatherSystem weather_;
    bool weatherForced_ = false;   ///< --clouds/--haze given: the weather system does not drive the sky
    bool sunOverride_ = false;      ///< --sun or the sun hotkeys: the clock no longer drives the sun
    bool exposureLocked_ = false;   ///< --exposure given: no adaptation
    int exposureMode_ = 2;          ///< 0 fixed, 1 analytic schedule, 2 image-based within the schedule's range
    std::unique_ptr<Overlay> overlay_;
    float exposureSmoothed_ = -1.0f;
    float exposureBias_ = 0.0f;     ///< --ev: stops added to the adapted exposure (a camera's compensation dial)
    float probeBakeSunY_ = 99.0f;   ///< sun height the probes were last captured under
    bool probeBakeLamps_ = false;
    float sunElevationDegrees_ = 38.0f;
    float sunAzimuthDegrees_ = 205.0f;
    float cloudCoverage_ = 0.35f;
    float cloudDensity_ = 0.5f;
    float haze_ = 0.05f;
    float skyIntensity_ = 1.0f;
    int lampsMode_ = 2;     ///< 0 off, 1 on, 2 automatic (on when the sun is low)
    int televisionMode_ = 2;

    int frameBudget_ = 0;
    int logEvery_ = 0;
    std::string probeDumpDirectory_;
    std::string depthDumpPath_;
    bool deterministic_ = false;
    int modelTextureCap_ = 1024;
    int framesDrawn_ = 0;
    std::string screenshotPath_;
    std::string recordDirectory_;   ///< --record: every recordEvery_th frame saved here as a numbered PNG
    int recordEvery_ = 1;
    std::string startView_ = "entrance";
    bool cameraOverride_ = false;
    Microsoft::Xna::Framework::Vector3 cameraOverridePosition_{0.0f, 1.6f, 0.0f};
    float cameraOverrideYaw_ = 0.0f, cameraOverridePitch_ = 0.0f;
    // --dolly: the camera glides from where it starts to a target (a camera spec or a viewpoint)
    // over a number of seconds, eased, for recorded clips.
    std::string dollyTarget_;
    float dollySeconds_ = 0.0f;
    float dollyElapsed_ = 0.0f;
    bool dollyActive_ = false;
    Microsoft::Xna::Framework::Vector3 dollyFromPosition_, dollyToPosition_;
    float dollyFromYaw_ = 0.0f, dollyToYaw_ = 0.0f, dollyFromPitch_ = 0.0f, dollyToPitch_ = 0.0f;
    float elapsedSeconds_ = 0.0f;
    bool contentLoaded_ = false;
    std::uint32_t seed_ = 20260912u;
    std::string assetDirectory_ = CNA_ROOM_DEFAULT_ASSET_DIR;
    std::string textureCache_;      ///< empty: <assets>/cache/textures; "off": bake every run
    int textureSize_ = 1024;
};

}  // namespace CnaRoom
