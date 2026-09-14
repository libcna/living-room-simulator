// SPDX-License-Identifier: MIT
#pragma once

#include "CnaRoom/Render/Camera.hpp"
#include "CnaRoom/Render/Precipitation.hpp"
#include "CnaRoom/Render/TelevisionContent.hpp"
#include "CnaRoom/Render/RenderSettings.hpp"
#include "CnaRoom/Render/SkySystem.hpp"
#include "CnaRoom/Render/Steam.hpp"
#include "CnaRoom/Render/Sunbeams.hpp"
#include "CnaRoom/Render/ContactShadows.hpp"

#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include "System/Diagnostics/Stopwatch.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDevice;
    class PbrEffect;
    class RenderTarget2D;
    class TextureCube;
}

namespace CNA::Graphics {
    class DecalPass;
    class AutoExposureEXT;
    class CascadedShadowMap;
    class CubeShadowMap;
    class DepthNormalPrepass;
    class GpuTimer;
    class RenderPipeline;
}

namespace CnaRoom {

class ExposureMeter;
class FloatCubeUploader;
class GpuMesh;
class PlanarReflection;
struct ReflectionPlane;
class Vignette;
struct Material;

/// An environment captured from inside the room: what a surface near it
/// reflects and receives as ambient light, instead of the sky.
struct InteriorProbe
{
    Microsoft::Xna::Framework::Vector3 position;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::TextureCube> environment;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::TextureCube> irradiance;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::TextureCube> prefiltered;
    /// Half-float prefiltered cubes, one per roughness class (kSpecularClasses); empty when unsupported.
    std::vector<std::unique_ptr<Microsoft::Xna::Framework::Graphics::TextureCube>> prefilteredClasses;
    float scale = 1.0f;          ///< radiance per texel unit
    int prefilteredMips = 5;
    Microsoft::Xna::Framework::Vector3 meanIrradiance{0.02f, 0.02f, 0.02f};   ///< E / pi in scene units: what it paints on white
};

/// Roughness classes the half-float specular cubes are prefiltered for.
constexpr std::array<float, 5> kSpecularClasses = {0.06f, 0.28f, 0.50f, 0.72f, 0.94f};
[[nodiscard]] inline int specularClassFor(float roughness)
{
    int best = 0;
    float bestDistance = 1e9f;
    for (int i = 0; i < static_cast<int>(kSpecularClasses.size()); ++i)
    {
        const float d = std::abs(kSpecularClasses[static_cast<std::size_t>(i)] - roughness);
        if (d < bestDistance) { bestDistance = d; best = i; }
    }
    return best;
}

/// An artificial light in the room. Intensities are in the scene's radiometric
/// units where the noon sun is about 3.6 (see Lamp::fromLumens).
struct Lamp
{
    std::string name;
    Microsoft::Xna::Framework::Vector3 position;
    Microsoft::Xna::Framework::Vector3 colour{1.0f, 0.85f, 0.65f};
    float intensity = 0.0025f;     ///< radiant intensity in scene units (after dimming)
    float fullIntensity = 0.0025f; ///< the intensity when fully on
    float range = 8.0f;
    bool on = false;
    bool spot = false;
    Microsoft::Xna::Framework::Vector3 direction{0.0f, -1.0f, 0.0f};
    float innerAngle = 0.5f;
    float outerAngle = 0.9f;
    bool castsShadow = false;      ///< at most one lamp per frame gets a cube shadow
    bool daylightPortal = false;   ///< a window's sky light: left out of the probe captures (they see the sky themselves)

    /// 1 scene unit of irradiance ~ 25 000 lux, so a bulb's lumens convert
    /// to radiant intensity as lumens / (4 pi 25000).
    [[nodiscard]] static float fromLumens(float lumens) { return lumens / (4.0f * 3.14159265f * 25000.0f); }
};

/// One drawable: a mesh, a material and a world transform.
struct SceneItem
{
    const GpuMesh* mesh = nullptr;
    const Material* material = nullptr;
    Microsoft::Xna::Framework::Matrix world = Microsoft::Xna::Framework::Matrix::getIdentityProperty();
    Microsoft::Xna::Framework::BoundingBox worldBounds;
    Microsoft::Xna::Framework::BoundingSphere worldSphere;
    bool castsShadow = true;
    bool shadowOnly = false;    ///< a caster that is never drawn (a proxy: sparse leaf clusters for dappled sun)
    bool exterior = false;      ///< outside the room: lit by the sky environment, not a probe
    const InteriorProbe* probe = nullptr;
    int lamp = -1;              ///< the punctual light bound when drawing this item (-1: none)
    std::string name;
};

/**
 * @brief Draws the scene through CNA's engine layer.
 *
 * Frame order: cull, cascaded shadow pass for the dominant sky light,
 * depth/normal prepass, then inside RenderPipeline::begin/end the sky, the
 * opaque items with PbrEffect and the transparent items, followed by the
 * pipeline's post-process chain (SSAO, bloom, tonemap, FXAA).
 */
class SceneRenderer
{
public:
    struct Stats
    {
        int drawCalls = 0;
        int shadowDrawCalls = 0;
        int visibleItems = 0;
        int totalItems = 0;
        std::size_t triangles = 0;
        int postPasses = 0;
        bool usedSceneTarget = false;
        float cullMs = 0.0f, shadowMs = 0.0f, prepassMs = 0.0f, skyMs = 0.0f, opaqueMs = 0.0f;
        float reflectionMs = 0.0f;
        float sunbeamMs = 0.0f;
        float contactMs = 0.0f;
        int reflectionDrawCalls = 0;
        float postMs = 0.0f, frameMs = 0.0f;
        double gpuShadowMs = -1.0, gpuPrepassMs = -1.0, gpuSkyMs = -1.0, gpuOpaqueMs = -1.0, gpuSunbeamMs = -1.0;
        double gpuPostMs = -1.0;
    };

    explicit SceneRenderer(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);
    ~SceneRenderer();
    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;

    void initialise(const RenderSettings& settings, int width, int height);
    void applySettings(const RenderSettings& settings);
    void resize(int width, int height);

    [[nodiscard]] SkySystem& sky() { return sky_; }
    [[nodiscard]] const SkySystem& sky() const { return sky_; }

    void addItem(SceneItem item);
    /// Registers a mirror plane; materials refer to it by the returned index.
    int addReflectionPlane(const ReflectionPlane& plane);
    void setReflectionPlaneEnabled(int index, bool enabled);
    /// The per-channel gain the auto white balance applied this frame (linear, the largest 1).
    [[nodiscard]] const Microsoft::Xna::Framework::Vector3& whiteBalanceGain() const { return whiteBalanceGain_; }
    void clearScene();
    [[nodiscard]] std::size_t itemCount() const { return items_.size(); }
    /// Mutable access for animated items; call updateBounds after changing the world matrix.
    [[nodiscard]] SceneItem& item(std::size_t index) { return items_[index]; }

    /// Seconds since the last frame (auto exposure adaptation, television playback).
    void setFrameTime(float dt, float seconds) { frameDt_ = dt; frameSeconds_ = seconds; }
    /// Image-based exposure from the HDR scene's log-average luminance (CNAEXT AutoExposureEXT).
    void setAutoExposureEnabled(bool enabled) { autoExposureEnabled_ = enabled; }
    [[nodiscard]] bool autoExposureAvailable() const { return autoExposure_ != nullptr; }
    /// The exposure the measurement asks for (< 0 until the first measured frame).
    [[nodiscard]] float measuredExposure() const { return measuredExposure_; }
    [[nodiscard]] float measuredLuminance() const { return measuredLuminance_; }
    /// CNA's whole-frame log-average, for comparison with the centre-weighted meter.
    [[nodiscard]] float plainLuminance() const { return plainLuminance_; }
    [[nodiscard]] float highlightShare() const { return highlightShare_; }
    /// Autofocus: the prepass depth at the frame's centre pulls the lens's
    /// focus at `perSecond` (a capture wants it settled within a frame or two).
    void setFocusPullRate(float perSecond) { focusRate_ = perSecond; }
    /// The distance the lens is focused at this frame (metres), and the last measured one.
    [[nodiscard]] float focusDistance() const { return focusSmoothed_; }
    [[nodiscard]] float measuredFocus() const { return measuredFocus_; }
    [[nodiscard]] TelevisionContent* television() { return television_.get(); }
    void setTelevisionPlaying(bool playing) { televisionPlaying_ = playing; }
    /// Height fog for the exterior (density per metre, 0 disables), colour is CNA's daylight haze.
    void setFog(float density, float falloff, float baseHeight)
    {
        fogDensity_ = density;
        fogFalloff_ = falloff;
        fogBaseHeight_ = baseHeight;
    }
    /// A lightning flash: a third directional light from `direction` at `strength` (scene radiance).
    void setLightning(float strength, const Microsoft::Xna::Framework::Vector3& direction);
    /// Rain/snow/hail drawn after the opaque pass (a copy is kept).
    void setPrecipitation(const Precipitation::Params& params);
    /// Steam over a hot drink: the rim's centre and radius (drawn while RenderSettings::steam is on).
    void setSteam(const Microsoft::Xna::Framework::Vector3& origin, float radius) { steamOrigin_ = origin; steamRadius_ = radius; steamSet_ = true; }
    /// A decal projected onto the lit room after the opaque pass: a unit box scaled and placed
    /// (the texture on its local X/Y, projecting along local +Z); the texture's alpha is the mark.
    void addDecal(Microsoft::Xna::Framework::Graphics::Texture2D* texture, const Microsoft::Xna::Framework::Matrix& world, float opacity,
                  const Microsoft::Xna::Framework::Vector3& tint = Microsoft::Xna::Framework::Vector3(0.0f, 0.0f, 0.0f));
    void clearDecals() { decals_.clear(); }
    [[nodiscard]] std::size_t decalCount() const { return decals_.size(); }
    /// Smoke outside: one plume per chimney, lit by the sky (replaces the previous set).
    struct SmokePlume
    {
        Microsoft::Xna::Framework::Vector3 origin;
        Microsoft::Xna::Framework::Vector3 drift;   ///< m/s
        float strength = 1.0f;
    };
    void setSmokePlumes(std::vector<SmokePlume> plumes) { smokePlumes_ = std::move(plumes); }
    /// The nearest probe's mean irradiance (E / pi, scene units): the light on a white surface there.
    [[nodiscard]] Microsoft::Xna::Framework::Vector3 irradianceAt(const Microsoft::Xna::Framework::Vector3& position) const;
    [[nodiscard]] const Precipitation* precipitation() const { return precipitation_.get(); }
    /// Recomputes world bounds after a world matrix changed.
    static void updateBounds(SceneItem& item);

    void render(const Camera& camera, const RenderSettings& settings);

    /// Captures the room from each position (six faces into a cube), derives
    /// the split-sum products and assigns every interior item its nearest
    /// probe. `iterations` > 1 re-captures with the previous probes bound so
    /// the result carries more than one bounce. Load-time work (seconds).
    void bakeInteriorProbes(const std::vector<Microsoft::Xna::Framework::Vector3>& positions,
                            const RenderSettings& settings, int iterations = 2);
    /// Queues the same work to be spread over the following frames (one
    /// face per frame from render()); probes keep their previous products
    /// until each is complete.
    void requestProbeBake(const std::vector<Microsoft::Xna::Framework::Vector3>& positions,
                          const RenderSettings& settings, int iterations = 1);
    /// Captures up to `faces` faces of the queued bake; returns true while work remains.
    bool stepProbeBake(int faces);
    /// Game minutes that pass per frame: the per-frame bake budget is sized so a
    /// full rebake completes within about ten game minutes (one face at least,
    /// a quarter of the queue at most), whatever the clock's speed or frame rate.
    void setProbeBakePace(float gameMinutesPerFrame) { probeBakeMinutesPerFrame_ = std::max(0.0f, gameMinutesPerFrame); }
    [[nodiscard]] int probeBakeFacesPerFrame() const;
    [[nodiscard]] bool probeBakePending() const { return !probeBakeQueue_.empty(); }
    [[nodiscard]] std::size_t probeCount() const { return probes_.size(); }

    /// Replaces the lamp list; the shadowed lamp's cube map is rebuilt on the next frame.
    void setLamps(std::vector<Lamp> lamps);
    /// The box the sunbeams' medium fills (the room's interior).
    void setSunbeamVolume(const Microsoft::Xna::Framework::Vector3& min, const Microsoft::Xna::Framework::Vector3& max);
    [[nodiscard]] const std::vector<Lamp>& lamps() const { return lamps_; }
    [[nodiscard]] std::vector<Lamp>& lamps() { lampsDirty_ = true; return lamps_; }
    [[nodiscard]] float probeBakeSeconds() const { return probeBakeSeconds_; }

    [[nodiscard]] const Stats& stats() const { return stats_; }
    [[nodiscard]] const std::vector<std::string>& limitations() const { return limitations_; }
    /// Writes every baked probe's captured faces and irradiance as PNG strips
    /// into the directory (debugging aid; empty disables).
    void setProbeDumpDirectory(std::string directory) { probeDumpDirectory_ = std::move(directory); }
    /// Writes the next frame's prepass depth as a grey PNG (0..10 m), a diagnostic for the autofocus.
    void setDepthDumpPath(std::string path) { depthDumpPath_ = std::move(path); }
    [[nodiscard]] Microsoft::Xna::Framework::BoundingBox sceneBounds() const;

private:
    void cull(const Camera& camera);
    void drawShadows(const Camera& camera, const RenderSettings& settings);
    void drawPrepass(const Camera& camera, const RenderSettings& settings);
    void readFocus(const RenderSettings& settings);
    [[nodiscard]] Microsoft::Xna::Framework::Vector3 whiteBalanceGain(const RenderSettings& settings) const;
    void dumpDepth(const Camera& camera, const RenderSettings& settings);
    void drawOpaque(const Camera& camera, const RenderSettings& settings);
    bool sunbeamInputs(const Camera& camera, const RenderSettings& settings, Sunbeams::Inputs& in);
    void marchSunbeams(const Camera& camera, const RenderSettings& settings);
    void marchContactShadows(const Camera& camera, const RenderSettings& settings);
    void drawDecals(const Camera& camera, const RenderSettings& settings);
    void drawSunbeams(const Camera& camera, const RenderSettings& settings);
    void drawTransparent(const Camera& camera, const RenderSettings& settings);
    void applyLighting(const RenderSettings& settings);
    void applyEnvironment(const InteriorProbe* probe, const RenderSettings& settings, int specularClass);
    void applyMaterial(const Material& material, const Microsoft::Xna::Framework::Matrix& world,
                       const Microsoft::Xna::Framework::Matrix& view,
                       const Microsoft::Xna::Framework::Matrix& projection,
                       const InteriorProbe* probe, int lampIndex);
    void captureProbe(InteriorProbe& probe, Microsoft::Xna::Framework::Graphics::RenderTarget2D& target,
                      int size, const RenderSettings& settings, bool skyOnly);
    void captureProbeFace(InteriorProbe& probe, Microsoft::Xna::Framework::Graphics::RenderTarget2D& target,
                          int size, const RenderSettings& settings, bool skyOnly, int face,
                          std::vector<Microsoft::Xna::Framework::Vector3>& faces, float& peak);
    void drawReflections(const Camera& camera, const RenderSettings& settings);
    void dumpReflection(PlanarReflection& reflection, std::size_t plane);
    [[nodiscard]] PlanarReflection* activeReflection(const Material& material) const;
    void dumpProbe(const InteriorProbe& probe, int size, const std::vector<Microsoft::Xna::Framework::Vector3>& faces);
    void finishProbe(InteriorProbe& probe, int size, const std::vector<Microsoft::Xna::Framework::Vector3>& faces,
                     float peak, const RenderSettings& settings);
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::TextureCube> integrateIrradiance(
        const std::vector<Microsoft::Xna::Framework::Vector3>& faces, int size, float scale, float gain);
    void detectCaptureFormat();
    void detectProbeMapping(Microsoft::Xna::Framework::Graphics::RenderTarget2D& target, int size,
                            const RenderSettings& settings);
    void assignProbes();
    void assignLamps();
    void drawLampShadow();
    void applyLamp(int lampIndex);

    Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
    SkySystem sky_;
    std::unique_ptr<FloatCubeUploader> floatCubes_;
    std::vector<std::unique_ptr<PlanarReflection>> reflections_;   ///< one capture per plane
    std::vector<ReflectionPlane> reflectionPlanes_;
    bool reflectionsUnsupported_ = false;
    bool mirrorPass_ = false;          ///< drawing the mirrored scene: winding is flipped
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::PbrEffect> effect_;
    std::unique_ptr<CNA::Graphics::RenderPipeline> pipeline_;
    std::unique_ptr<CNA::Graphics::CascadedShadowMap> shadows_;
    std::unique_ptr<CNA::Graphics::DepthNormalPrepass> prepass_;

    enum class GpuStage { Shadow, Prepass, Sky, Opaque, Sunbeams, Count };
    std::array<std::unique_ptr<CNA::Graphics::GpuTimer>, static_cast<std::size_t>(GpuStage::Count)> gpuStage_;
    std::array<double, static_cast<std::size_t>(GpuStage::Count)> gpuStageMs_{};
    bool gpuTimingAvailable_ = false;

    std::vector<SceneItem> items_;
    std::vector<std::size_t> visibleOpaque_;
    std::vector<std::size_t> visibleTransparent_;
    const Material* appliedMaterial_ = nullptr;
    const InteriorProbe* appliedProbe_ = nullptr;
    int appliedSpecularClass_ = -1;
    bool environmentBound_ = false;
    bool usingSceneTarget_ = false;
    float lightScale_ = 1.0f;     ///< < 1 while capturing a probe into an 8-bit target
    bool capturing_ = false;
    bool captureHdr_ = false;     ///< probe captures go into a half-float target (no sRGB encode, no clipping)
    bool captureFormatKnown_ = false;
    std::vector<std::unique_ptr<InteriorProbe>> probes_;
    float probeBakeSeconds_ = 0.0f;
    struct ProbeBakeStep { std::size_t probe; int face; };
    std::vector<ProbeBakeStep> probeBakeQueue_;
    std::vector<Microsoft::Xna::Framework::Vector3> probeBakeFaces_;
    float probeBakePeak_ = 1e-4f;
    int probeBakeSize_ = 64;
    int probeBakeStepsDone_ = 0;
    int probeBakeCount_ = 0;
    System::Diagnostics::Stopwatch probeBakeWatch_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::RenderTarget2D> probeTarget_;
    bool probeMirrorX_ = true;    ///< readback orientation, verified against the sky at first bake
    bool probeFlipY_ = false;
    bool probeMappingKnown_ = false;
    std::size_t probeBakeQueueTotal_ = 0;
    float probeBakeMinutesPerFrame_ = 0.0f;
    const RenderSettings* currentSettings_ = nullptr;
    std::vector<Lamp> lamps_;
    bool lampsDirty_ = true;
    float lightning_ = 0.0f;
    Microsoft::Xna::Framework::Vector3 lightningDirection_{0.3f, -0.8f, 0.5f};
    std::unique_ptr<Precipitation> precipitation_;
    Precipitation::Params precipitationParams_;
    std::unique_ptr<Steam> steam_;
    std::unique_ptr<Steam> smoke_;   ///< a denser population for the chimneys
    Microsoft::Xna::Framework::Vector3 steamOrigin_;
    float steamRadius_ = 0.035f;
    bool steamSet_ = false;
    bool loggedSteam_ = false;
    bool loggedSmoke_ = false;
    bool loggedLampHaze_ = false;
    std::vector<SmokePlume> smokePlumes_;
    bool loggedLampAssignment_ = false;
    void drawSteam(const Camera& camera, const RenderSettings& settings);
    std::unique_ptr<CNA::Graphics::AutoExposureEXT> autoExposure_;
    bool autoExposureEnabled_ = false;
    float measuredExposure_ = -1.0f;
    float measuredLuminance_ = -1.0f;
    float plainLuminance_ = -1.0f;
    float highlightShare_ = 0.0f;
    std::unique_ptr<ExposureMeter> exposureMeter_;
    std::unique_ptr<Vignette> vignette_;
    std::unique_ptr<Sunbeams> sunbeams_;
    std::unique_ptr<ContactShadows> contact_;
    std::unique_ptr<CNA::Graphics::DecalPass> decalPass_;
    struct Decal
    {
        Microsoft::Xna::Framework::Graphics::Texture2D* texture = nullptr;
        Microsoft::Xna::Framework::Matrix world;
        float opacity = 1.0f;
        Microsoft::Xna::Framework::Vector3 tint;
    };
    std::vector<Decal> decals_;
    Microsoft::Xna::Framework::Vector3 sunbeamMin_, sunbeamMax_;
    bool sunbeamVolumeSet_ = false;
    Microsoft::Xna::Framework::Vector3 keyLightDirection_{0.0f, -1.0f, 0.0f};   ///< the cascades' light, as fitted this frame
    Microsoft::Xna::Framework::Vector3 keyLightColour_;
    float appliedGrain_ = -1.0f, appliedAberration_ = -1.0f;
    float focusRate_ = 6.0f;
    Microsoft::Xna::Framework::Vector3 whiteBalanceGain_{1.0f, 1.0f, 1.0f};
    float focusSmoothed_ = 3.0f, measuredFocus_ = -1.0f, appliedFocus_ = -1.0f;
    bool focusReadable_ = true;
    std::string depthDumpPath_;
    std::unique_ptr<TelevisionContent> television_;
    bool televisionPlaying_ = false;
    float frameDt_ = 0.0f;
    float frameSeconds_ = 0.0f;
    float fogDensity_ = 0.0f, fogFalloff_ = 0.1f, fogBaseHeight_ = 0.0f;
    float appliedFogDensity_ = -1.0f, appliedBloomThreshold_ = -1.0f, appliedFlareThreshold_ = -1.0f;
    int shadowedLamp_ = -1;
    int lampShadowFace_ = 6;      ///< next cube face to render; 6 = up to date
    float appliedExposure_ = -1.0f;
    int appliedLamp_ = -2;
    std::unique_ptr<CNA::Graphics::CubeShadowMap> lampShadow_;
    int width_ = 1280;
    int height_ = 720;
    mutable bool loggedCascades_ = false;
    bool loggedSunbeams_ = false;
    bool loggedSunbeamSkip_ = false;
    bool dumpedAtlasEarly_ = false;
    void dumpAtlas(const char* path);
    bool prepassDrawn_ = false;     ///< the depth prepass ran this frame
    bool cascadesFitted_ = false;   ///< the cascades were fitted this frame (light above the horizon)
    bool loggedPassTimings_ = false;
    bool loggedSceneFormat_ = false;
    bool loggedIrradianceFormat_ = false;
    bool loggedCascadeCasters_ = false;
    bool loggedSpecularClasses_ = false;
    std::string probeDumpDirectory_;
    int probeDumpCount_ = 0;
    int reflectionDumpCount_ = 0;
    Microsoft::Xna::Framework::Vector3 lastIrradianceMean_;
    std::vector<std::uint8_t> lastIrradianceStrip_;   ///< 6 faces side by side, RGBA sRGB
    int lastIrradianceSize_ = 0;
    int timedFrames_ = 0;
    Stats stats_;
    std::vector<std::string> limitations_;
};

}  // namespace CnaRoom
