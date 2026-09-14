// SPDX-License-Identifier: MIT
#include "CnaRoom/Render/SceneRenderer.hpp"

#include "CnaRoom/Render/ExposureMeter.hpp"
#include "CnaRoom/Render/FloatCube.hpp"
#include "CnaRoom/Render/GpuMesh.hpp"
#include "CnaRoom/Render/Irradiance.hpp"
#include "CnaRoom/Render/PlanarReflection.hpp"
#include "CnaRoom/Render/Material.hpp"
#include "CnaRoom/Render/Sunbeams.hpp"
#include "CnaRoom/Render/Vignette.hpp"

#include "CNA/Graphics/AutoExposureEXT.hpp"
#include "CNA/Graphics/CascadedShadowMap.hpp"
#include "CNA/Graphics/CubeShadowMap.hpp"
#include "CNA/Graphics/PointLightEXT.hpp"
#include "CNA/Graphics/DepthNormalPrepass.hpp"
#include "CNA/Graphics/DirectionalLightEXT.hpp"
#include "CNA/Graphics/EnvironmentProcessor.hpp"
#include "CNA/Graphics/GpuTimer.hpp"
#include "CNA/Graphics/RenderPipeline.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "CNA/Graphics/RenderQuality.hpp"
#include "CNA/Graphics/ShadowQuality.hpp"
#include "CNA/Graphics/TonemappingMode.hpp"
#include "CNA/Graphics/TransparencyMode.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/Logger.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfVector4.hpp"
#include "Microsoft/Xna/Framework/BoundingFrustum.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/ContainmentType.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DirectionalLight.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ImageBasedLightEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PunctualLightEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureTransformEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <filesystem>
#include "System/Diagnostics/Stopwatch.hpp"

#include <array>
#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <limits>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Graphics::CascadedShadowMap;
using CNA::Graphics::DepthNormalPrepass;
using CNA::Graphics::DirectionalLightEXT;
using CNA::Graphics::GpuTimer;
using CNA::Graphics::RenderPipeline;
using CNA::Graphics::RenderQuality;
using CNA::Graphics::ShadowQuality;
using CNA::Graphics::TonemappingMode;
using CNA::Graphics::TransparencyMode;
using System::Diagnostics::Stopwatch;

namespace CnaRoom {

namespace {

ShadowQuality toShadowQuality(int quality)
{
    switch (quality)
    {
        case 0: return ShadowQuality::Disabled;
        case 1: return ShadowQuality::Low;
        case 2: return ShadowQuality::Medium;
        case 3: return ShadowQuality::High;
        default: return ShadowQuality::Ultra;
    }
}

RenderQuality toRenderQuality(int quality)
{
    switch (quality)
    {
        case 1: return RenderQuality::Low;
        case 2: return RenderQuality::Medium;
        case 3: return RenderQuality::High;
        default: return RenderQuality::Ultra;
    }
}

TonemappingMode toTonemap(int mode)
{
    switch (mode)
    {
        case 0: return TonemappingMode::None;
        case 1: return TonemappingMode::Reinhard;
        case 3: return TonemappingMode::Filmic;
        default: return TonemappingMode::Aces;
    }
}

float milliseconds(const Stopwatch& watch)
{
    return static_cast<float>(watch.getElapsedTicksProperty()) / 10000.0f;
}

Vector3 transformPoint(const Vector3& p, const Matrix& m)
{
    return Vector3(p.X * m.M11 + p.Y * m.M21 + p.Z * m.M31 + m.M41,
                   p.X * m.M12 + p.Y * m.M22 + p.Z * m.M32 + m.M42,
                   p.X * m.M13 + p.Y * m.M23 + p.Z * m.M33 + m.M43);
}

}  // namespace

SceneRenderer::SceneRenderer(GraphicsDevice& device) : device_(device), sky_(device) {}

SceneRenderer::~SceneRenderer() = default;

void SceneRenderer::initialise(const RenderSettings& settings, int width, int height)
{
    if (floatCubes_ == nullptr)
    {
        floatCubes_ = std::make_unique<FloatCubeUploader>(device_);
        sky_.setFloatCubeUploader(floatCubes_.get());
    }
    width_ = std::max(1, width);
    height_ = std::max(1, height);
    limitations_.clear();

    if (!device_.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        limitations_.emplace_back("the renderer has no 3D pipeline; nothing will be drawn");

    effect_ = std::make_unique<PbrEffect>(device_);

    pipeline_ = std::make_unique<RenderPipeline>(device_);
    pipeline_->resize(width_, height_);

    if (settings.shadows)
    {
        if (device_.SupportsShadowSamplingEXT())
        {
            try
            {
                shadows_ = std::make_unique<CascadedShadowMap>(
                    device_, toShadowQuality(settings.shadowQuality),
                    std::clamp(settings.shadowCascades, 2, CascadedShadowMap::kMaxCascades));
            }
            catch (const std::exception& failure)
            {
                limitations_.emplace_back(std::string("no shadow map: ") + failure.what());
                shadows_.reset();
            }
            if (shadows_ != nullptr && !shadows_->isSupported())
            {
                limitations_.emplace_back("cascaded shadow maps are unavailable on this renderer");
                shadows_.reset();
            }
        }
        else
        {
            limitations_.emplace_back("the renderer cannot sample a shadow map");
        }
    }

    prepass_ = std::make_unique<DepthNormalPrepass>(device_, width_, height_);
    if (prepass_->getPrepassEffect() == nullptr || !prepass_->getPrepassEffect()->IsEffectValid())
    {
        limitations_.emplace_back("the depth/normal prepass did not compile; SSAO/SSR are off");
        prepass_.reset();
    }

    gpuTimingAvailable_ = false;
    for (auto& timer : gpuStage_)
    {
        timer = std::make_unique<GpuTimer>(device_);
        if (!timer->isSupported())
        {
            CNA::Logger::Info("cna-room: GPU timing unavailable -- " + timer->getUnsupportedReason());
            for (auto& other : gpuStage_) other.reset();
            break;
        }
        gpuTimingAvailable_ = true;
    }
    gpuStageMs_.fill(-1.0);

    if (!device_.SupportsCapability(CNA::GraphicsCapability::FloatRenderTargets)
        && !device_.SupportsCapability(CNA::GraphicsCapability::HalfFloatRenderTargets))
        limitations_.emplace_back("no float render targets; HDR resolves straight to the back buffer");

    precipitation_ = std::make_unique<Precipitation>(device_);
    if (!precipitation_->supported()) limitations_.emplace_back("no precipitation: " + precipitation_->reason());
    steam_ = std::make_unique<Steam>(device_);
    if (!steam_->supported()) limitations_.emplace_back("no steam: " + steam_->reason());
    else smoke_ = std::make_unique<Steam>(device_, 64);
    television_ = std::make_unique<TelevisionContent>(device_);
    vignette_ = std::make_unique<Vignette>(device_);
    sunbeams_ = std::make_unique<Sunbeams>(device_);
    if (!sunbeams_->supported()) limitations_.emplace_back("sunbeams off: " + sunbeams_->reason());
    if (!vignette_->supported()) limitations_.emplace_back("vignette off: " + vignette_->reason());
    for (auto& reflection : reflections_)
        reflection->resize(std::max(16, static_cast<int>(static_cast<float>(width) * settings.reflectionScale)),
                           std::max(16, static_cast<int>(static_cast<float>(height) * settings.reflectionScale)));
    if (!television_->supported()) limitations_.emplace_back("no television picture: " + television_->reason());
    try
    {
        autoExposure_ = std::make_unique<CNA::Graphics::AutoExposureEXT>(device_);
        autoExposure_->setKeyValue(0.05f);   // log-average of this scene: ~0.075 by day, ~0.0006 under the lamps
        exposureMeter_ = std::make_unique<ExposureMeter>(device_);
        if (!exposureMeter_->supported()) limitations_.emplace_back("exposure meter off (" + exposureMeter_->reason() + "): CNA's log-average used");
        autoExposure_->setExposureRange(0.3f, 2000.0f);
        autoExposure_->setAdaptationSpeeds(2.5f, 1.2f);
    }
    catch (const std::exception& failure)
    {
        limitations_.emplace_back(std::string("no image-based exposure: ") + failure.what());
    }

    sky_.build();
    if (!sky_.isSupported()) limitations_.emplace_back(sky_.unsupportedReason());

    applySettings(settings);

    for (const std::string& limitation : limitations_) CNA::Logger::Warn("cna-room: " + limitation);
}

void SceneRenderer::applySettings(const RenderSettings& settings)
{
    if (pipeline_ == nullptr) return;
    auto& p = pipeline_->getSettings();
    p.setHDREnabled(settings.hdr);
    p.setExposure(settings.exposure);
    appliedExposure_ = settings.exposure;
    p.setTonemappingMode(toTonemap(settings.tonemap));
    p.setRenderQuality(toRenderQuality(settings.shadowQuality));
    p.setBloomEnabled(settings.bloom);
    p.setBloomIntensity(settings.bloomIntensity);
    p.setBloomThreshold(settings.bloomThreshold);
    p.setBloomIterations(settings.bloomIterations);
    p.setSSAOEnabled(settings.ssao && prepass_ != nullptr);
    p.setSSAORadius(settings.ssaoRadius);
    p.setSSAOIntensity(settings.ssaoIntensity);
    p.setSSAOSampleCount(settings.ssaoSamples);
    p.setFXAAEnabled(settings.fxaa);
    p.setSSREnabled(settings.ssr && prepass_ != nullptr);
    p.setColorGradeEnabled(settings.colourGrade);
    // A thin lens over the prepass depth: the frame's centre pulls the focus
    // (readFocus), the rest blurs by its circle of confusion.
    p.setDOFEnabled(settings.depthOfField && prepass_ != nullptr);
    p.setDOFFocalLength(settings.dofFocalLength);
    p.setDOFFNumber(settings.dofFNumber);
    p.setDOFMaxRadius(settings.dofMaxRadius);
    p.setLensFlareIntensity(std::max(settings.lensFlare, 0.0f));
    p.setLensFlareDispersal(settings.lensFlareDispersal);
    p.setTransparencyMode(TransparencyMode::Sorted);
    p.setShadowsEnabled(false);  // shadows are driven by this renderer, not the pipeline
    if (shadows_ != nullptr)
    {
        shadows_->setSplitLambda(settings.shadowSplitLambda);
        shadows_->setBlendBand(settings.shadowBlendBand);
        shadows_->setDebugTintEnabled(settings.shadowDebugTint);
    }
}

void SceneRenderer::resize(int width, int height)
{
    for (auto& reflection : reflections_)
        if (reflection != nullptr && currentSettings_ != nullptr)
            reflection->resize(std::max(16, static_cast<int>(static_cast<float>(width) * currentSettings_->reflectionScale)),
                               std::max(16, static_cast<int>(static_cast<float>(height) * currentSettings_->reflectionScale)));
    width_ = std::max(1, width);
    height_ = std::max(1, height);
    if (pipeline_ != nullptr) pipeline_->resize(width_, height_);
    if (prepass_ != nullptr) prepass_->resize(width_, height_);
}

void SceneRenderer::updateBounds(SceneItem& item)
{
    if (item.mesh == nullptr) return;
    const BoundingBox& local = item.mesh->bounds();
    Vector3 lo(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
               std::numeric_limits<float>::max());
    Vector3 hi(-lo.X, -lo.Y, -lo.Z);
    for (int i = 0; i < 8; ++i)
    {
        const Vector3 corner((i & 1) ? local.Max.X : local.Min.X, (i & 2) ? local.Max.Y : local.Min.Y,
                             (i & 4) ? local.Max.Z : local.Min.Z);
        const Vector3 w = transformPoint(corner, item.world);
        lo = Vector3::Min(lo, w);
        hi = Vector3::Max(hi, w);
    }
    item.worldBounds = BoundingBox(lo, hi);
    item.worldSphere = BoundingSphere::CreateFromBoundingBox(item.worldBounds);
}

int SceneRenderer::addReflectionPlane(const ReflectionPlane& plane)
{
    reflectionPlanes_.push_back(plane);
    const RenderSettings* settings = currentSettings_;
    const float scale = settings != nullptr ? settings->reflectionScale : 0.5f;
    auto reflection = std::make_unique<PlanarReflection>(device_, std::max(16, static_cast<int>(static_cast<float>(width_) * scale)),
                                                         std::max(16, static_cast<int>(static_cast<float>(height_) * scale)));
    if (!reflection->supported() && !reflectionsUnsupported_)
    {
        reflectionsUnsupported_ = true;
        limitations_.emplace_back("planar reflections off: " + reflection->reason());
    }
    reflections_.push_back(std::move(reflection));
    return static_cast<int>(reflectionPlanes_.size()) - 1;
}

Vector3 SceneRenderer::whiteBalanceGain(const RenderSettings& settings) const
{
    // The illuminant a camera would balance against: the sky's ambient by
    // day (with a share of the sun), the lamps' 2700 K once they carry the
    // room at night, the moon and sky glow when they do not; the strength
    // takes that much of its chromaticity out, with the brightest channel
    // held at 1 so the frame only ever loses a little in the others.
    if (settings.whiteBalance <= 0.0f) return Vector3(1.0f, 1.0f, 1.0f);
    const auto luminance = [](const Vector3& v) { return 0.2126f * v.X + 0.7152f * v.Y + 0.0722f * v.Z; };
    const auto chroma = [&luminance](const Vector3& v) {
        const float l = luminance(v);
        return l > 1e-7f ? v * (1.0f / l) : Vector3(1.0f, 1.0f, 1.0f);
    };
    const SkyLighting& lighting = sky_.lighting();
    // The sky's light in the room: its ambient, a share of the sun's and of
    // the moon's directional light (the moon's colour is a directional
    // radiance ~10x the night ambient's).
    const Vector3 skyLight = lighting.ambientColour + lighting.sunColour * 0.05f + lighting.moonColour * 0.3f;
    const Vector3 sky = chroma(skyLight);
    bool lampsOn = false;
    for (const Lamp& lamp : lamps_) lampsOn = lampsOn || (lamp.on && lamp.intensity > 0.0f);
    // Which light carries the room: the sky's (through the windows) until
    // its ambient falls under ~0.003 at dusk, the lamps' 2700 K from there
    // when they are on (a lamp-lit room reads ~0.0004 in these units).
    const float lampShare = lampsOn ? 1.0f - std::clamp(luminance(skyLight) / 0.003f, 0.0f, 1.0f) : 0.0f;
    const Vector3 illuminant = chroma(sky * (1.0f - lampShare) + chroma(Vector3(1.0f, 0.78f, 0.55f)) * lampShare);
    // A camera's auto balance takes a daylight cast nearly out and a lamp's
    // 2700 K only part of the way (a lamp-lit room photographs warm): the
    // setting is scaled 1.25x under the sky and 0.5x under the lamps.
    const float k = std::clamp(settings.whiteBalance * (1.25f * (1.0f - lampShare) + 0.5f * lampShare), 0.0f, 1.0f);
    Vector3 gain(std::pow(std::max(illuminant.X, 1e-3f), -k), std::pow(std::max(illuminant.Y, 1e-3f), -k),
                 std::pow(std::max(illuminant.Z, 1e-3f), -k));
    const float top = std::max({gain.X, gain.Y, gain.Z});
    return gain * (1.0f / top);
}

void SceneRenderer::setReflectionPlaneEnabled(int index, bool enabled)
{
    if (index >= 0 && static_cast<std::size_t>(index) < reflectionPlanes_.size())
        reflectionPlanes_[static_cast<std::size_t>(index)].enabled = enabled;
}

PlanarReflection* SceneRenderer::activeReflection(const Material& material) const
{
    if (material.reflectionPlane < 0 || static_cast<std::size_t>(material.reflectionPlane) >= reflections_.size()) return nullptr;
    if (capturing_ || mirrorPass_) return nullptr;
    if (currentSettings_ == nullptr || !currentSettings_->planarReflections) return nullptr;
    PlanarReflection* reflection = reflections_[static_cast<std::size_t>(material.reflectionPlane)].get();
    return reflection != nullptr && reflection->supported() && reflection->valid() ? reflection : nullptr;
}

void SceneRenderer::addItem(SceneItem item)
{
    updateBounds(item);
    items_.push_back(std::move(item));
    if (!probes_.empty()) assignProbes();
}

void SceneRenderer::assignProbes()
{
    for (SceneItem& item : items_)
    {
        item.probe = nullptr;
        if (item.exterior || probes_.empty()) continue;
        float best = 1e30f;
        for (const auto& probe : probes_)
        {
            const float d = Vector3::DistanceSquared(probe->position, item.worldSphere.Center);
            if (d < best)
            {
                best = d;
                item.probe = probe.get();
            }
        }
    }
}

namespace {

float decodeSrgb(int value)
{
    const float c = static_cast<float>(value) / 255.0f;
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

constexpr float kProbeGlowScale = 0.12f;

std::uint8_t encodeCube(float linear, float scale)
{
    return static_cast<std::uint8_t>(std::lround(std::clamp(linear / std::max(scale, 1e-6f), 0.0f, 1.0f) * 255.0f));
}

/// The camera basis that renders cube face `face` so that texel (u, v) of the
/// readback (after the detected mirror/flip) sees EnvironmentProcessor::faceDirection(face, u, v).
void faceBasis(int face, Vector3& forward, Vector3& up)
{
    using CNA::Graphics::EnvironmentProcessor;
    forward = EnvironmentProcessor::faceDirection(face, 0.5f, 0.5f);
    const Vector3 down = EnvironmentProcessor::faceDirection(face, 0.5f, 1.0f)
                         - EnvironmentProcessor::faceDirection(face, 0.5f, 0.0f);
    up = -down;
    const float l = up.Length();
    if (l > 1e-5f) up = up * (1.0f / l);
}

}  // namespace

void SceneRenderer::captureProbeFace(InteriorProbe& probe, RenderTarget2D& target, int size,
                                     const RenderSettings& settings, bool skyOnly, int f,
                                     std::vector<Vector3>& faces, float& peak)
{
    // Into a half-float target the radiance is read back as it is. The 8-bit
    // fallback captures at half brightness so sunlit surfaces near 2.0 still
    // fit, with the effect's sRGB encode keeping the dark end in more than a
    // few steps; both are undone when the texel goes into the cube.
    lightScale_ = captureHdr_ ? 1.0f : 0.5f;
    capturing_ = true;
    appliedLamp_ = -2;   // re-apply the lamp under the capture's rules (the portals are left out)
    usingSceneTarget_ = false;
    const std::size_t count = static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
    if (faces.size() != 6u * count) faces.assign(6u * count, Vector3::Zero);
    std::vector<Color> captured(captureHdr_ ? 0u : count, Color::Black);
    std::vector<PackedVector::HalfVector4> capturedHdr(captureHdr_ ? count : 0u);

    Vector3 forward, up;
    faceBasis(f, forward, up);
    const Matrix view = Matrix::CreateLookAt(probe.position, probe.position + forward, up);
    const Matrix projection = Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver2, 1.0f, 0.05f, 300.0f);
    const BoundingFrustum frustum(view * projection);

    device_.SetRenderTarget(&target);
    device_.Clear(Color::Black, 1.0f);
    device_.setDepthStencilStateProperty(DepthStencilState::None);
    device_.setBlendStateProperty(BlendState::Opaque);
    if (settings.sky) sky_.draw(view, projection, size, size, !captureHdr_, lightScale_);
    if (!skyOnly)
    {
        device_.setDepthStencilStateProperty(DepthStencilState::Default);
        applyLighting(settings);
        // Shadows: the cascades were fitted to the viewing camera; the
        // receiver picks a cascade by depth along that camera, so the
        // capture keeps whatever the last frame fitted (correct for the
        // first cascade's neighbourhood, unshadowed beyond it).
        for (const SceneItem& item : items_)
        {
            if (item.material->isBlended() || item.shadowOnly) continue;
            if (frustum.Contains(item.worldSphere) == ContainmentType::Disjoint) continue;
            applyMaterial(*item.material, item.world, view, projection, item.probe, item.lamp);
            item.mesh->draw(device_);
        }
    }
    device_.SetRenderTarget(nullptr);
    if (captureHdr_) target.GetData(capturedHdr.data(), static_cast<int>(count));
    else target.GetData(captured.data(), static_cast<int>(count));
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const int sx = probeMirrorX_ ? size - 1 - x : x;
            const int sy = probeFlipY_ ? size - 1 - y : y;
            const std::size_t index = static_cast<std::size_t>(sy) * static_cast<std::size_t>(size) + static_cast<std::size_t>(sx);
            Vector3 radiance;
            if (captureHdr_)
            {
                const Vector4 v = capturedHdr[index].ToVector4();
                radiance = Vector3(std::max(v.X, 0.0f), std::max(v.Y, 0.0f), std::max(v.Z, 0.0f));
            }
            else
            {
                const Color& pixel = captured[index];
                radiance = Vector3(decodeSrgb(pixel.getRProperty()) / lightScale_,
                                   decodeSrgb(pixel.getGProperty()) / lightScale_,
                                   decodeSrgb(pixel.getBProperty()) / lightScale_);
            }
            faces[static_cast<std::size_t>(f) * count + static_cast<std::size_t>(y) * static_cast<std::size_t>(size) + static_cast<std::size_t>(x)] = radiance;
            peak = std::max(peak, std::max(radiance.X, std::max(radiance.Y, radiance.Z)));
        }
    lightScale_ = 1.0f;
    capturing_ = false;
    appliedLamp_ = -2;
    appliedMaterial_ = nullptr;
    environmentBound_ = false;
}

std::unique_ptr<TextureCube> SceneRenderer::integrateIrradiance(const std::vector<Vector3>& faces, int size, float scale,
                                                                 float gain)
{
    constexpr int kSmall = 8, kOut = 16;
    // Exact cosine convolution over an 8x8-per-face downsample, stored as
    // E / pi relative to the probe's scale (the shader multiplies by it).
    // The bounce gain stands in for the bounces the capture cannot hold.
    std::vector<Vector3> irradiance = convolveIrradiance(faces, size, kSmall, kOut);
    const float invScale = gain / std::max(scale, 1e-6f);
    lastIrradianceMean_ = Vector3::Zero;
    lastIrradianceSize_ = kOut;
    lastIrradianceStrip_.assign(static_cast<std::size_t>(6 * kOut) * kOut * 4u, 255);
    const auto encodeSrgb8 = [](float value) {
        value = std::clamp(value, 0.0f, 1.0f);
        value = value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
        return static_cast<int>(std::lround(value * 255.0f));
    };
    for (int f = 0; f < 6; ++f)
        for (int y = 0; y < kOut; ++y)
            for (int x = 0; x < kOut; ++x)
            {
                Vector3& e = irradiance[static_cast<std::size_t>(f * kOut * kOut + y * kOut + x)];
                e = e * invScale;
                lastIrradianceMean_ = lastIrradianceMean_ + e * (1.0f / static_cast<float>(6 * kOut * kOut));
                const std::size_t stripIndex = (static_cast<std::size_t>(y) * static_cast<std::size_t>(6 * kOut) + static_cast<std::size_t>(f * kOut + x)) * 4u;
                lastIrradianceStrip_[stripIndex] = static_cast<std::uint8_t>(encodeSrgb8(e.X * scale));
                lastIrradianceStrip_[stripIndex + 1] = static_cast<std::uint8_t>(encodeSrgb8(e.Y * scale));
                lastIrradianceStrip_[stripIndex + 2] = static_cast<std::uint8_t>(encodeSrgb8(e.Z * scale));
            }

    // Half-float cube when the renderer can fill one (CNA_FINDINGS R-21): at
    // night the diffuse ambient is ~1/300 of the shade peak that sets the
    // scale, below one 8-bit code.
    if (floatCubes_ != nullptr)
    {
        if (std::unique_ptr<RenderTargetCube> cube = floatCubes_->upload(irradiance, kOut)) return cube;
    }
    // Fallback: 8-bit, sRGB-encoded when the format exists (it does not on EasyGL).
    std::unique_ptr<TextureCube> cube;
    bool srgb = true;
    try
    {
        cube = std::make_unique<TextureCube>(device_, kOut, false, SurfaceFormat::ColorSrgbEXT);
    }
    catch (const std::exception&)
    {
        srgb = false;
        cube = std::make_unique<TextureCube>(device_, kOut, false, SurfaceFormat::Color);
    }
    std::vector<Color> out(static_cast<std::size_t>(kOut) * kOut);
    for (int f = 0; f < 6; ++f)
    {
        for (int y = 0; y < kOut; ++y)
            for (int x = 0; x < kOut; ++x)
            {
                const Vector3& e = irradiance[static_cast<std::size_t>(f * kOut * kOut + y * kOut + x)];
                const auto encode = [&](float value) {
                    value = std::clamp(value, 0.0f, 1.0f);
                    if (srgb) value = value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
                    return static_cast<int>(std::lround(value * 255.0f));
                };
                out[static_cast<std::size_t>(y) * kOut + static_cast<std::size_t>(x)] = Color(encode(e.X), encode(e.Y), encode(e.Z), 255);
            }
        cube->SetData(static_cast<CubeMapFace>(f), out.data(), static_cast<int>(out.size()));
    }
    if (!srgb && !loggedIrradianceFormat_)
    {
        loggedIrradianceFormat_ = true;
        limitations_.emplace_back("irradiance cubes are linear 8-bit (no sRGB cube format, no half-float cube upload: "
                                  + (floatCubes_ != nullptr ? floatCubes_->reason() : std::string("no uploader")) + "): dark rooms quantise");
    }
    return cube;
}

void SceneRenderer::finishProbe(InteriorProbe& probe, int size, const std::vector<Vector3>& faces, float peak,
                                const RenderSettings& settings)
{
    const std::size_t count = static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
    // A fresh cube every time: the previous one may still be bound as the
    // effect's environment while this runs, and EasyGL cannot read a bound
    // cube back for the irradiance integration (it stays valid until the
    // next draw binds the new one).
    probe.environment = std::make_unique<TextureCube>(device_, size, false, SurfaceFormat::Color);
    probe.scale = std::max(peak, 1e-4f) * 1.02f;
    std::vector<Color> face(count);
    for (int f = 0; f < 6; ++f)
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            const Vector3& r = faces[static_cast<std::size_t>(f) * count + i];
            face[i] = Color(static_cast<int>(encodeCube(r.X, probe.scale)), static_cast<int>(encodeCube(r.Y, probe.scale)),
                            static_cast<int>(encodeCube(r.Z, probe.scale)), 255);
        }
        probe.environment->SetData(static_cast<CubeMapFace>(f), face.data(), static_cast<int>(count));
    }
    // Irradiance: integrated here from the float capture (no sampling noise,
    // no 8-bit input) over an 8x8-per-face downsample, then stored sRGB-encoded
    // so the dark end keeps its precision under the shared scale. The bounce
    // gain stands in for the bounces the capture cannot hold.
    CNA::Graphics::EnvironmentProcessor processor(device_);
    const float gain = std::clamp(settings.probeBounceGain, 1.0f, 3.0f);
    probe.irradiance = integrateIrradiance(faces, size, probe.scale, gain);
    {
        // The mean irradiance (E / pi, scene units) says what colour the
        // probe paints on a white surface: a neutral room stays near grey.
        const Vector3 m = lastIrradianceMean_ * probe.scale;
        probe.meanIrradiance = m;
        const float lum = std::max(0.2126f * m.X + 0.7152f * m.Y + 0.0722f * m.Z, 1e-9f);
        CNA::Logger::Info("cna-room: probe at " + std::to_string(probe.position.X) + "," + std::to_string(probe.position.Y) + ","
                          + std::to_string(probe.position.Z) + " mean irradiance " + std::to_string(m.X) + "," + std::to_string(m.Y) + ","
                          + std::to_string(m.Z) + " (chroma " + std::to_string(m.X / lum) + "," + std::to_string(m.Y / lum) + ","
                          + std::to_string(m.Z / lum) + "), peak " + std::to_string(peak));
    }
    if (!probeDumpDirectory_.empty()) dumpProbe(probe, size, faces);
    // Specular from the environment cube through CNA (CNA_FINDINGS R-14).
    probe.prefiltered = processor.generatePrefilteredSpecular(probe.environment.get(), std::min(size, 32), probe.prefilteredMips, 24);
    // And in half-float, one cube per roughness class from the float faces:
    // the 8-bit chain quantises a night room's reflection to a few codes.
    probe.prefilteredClasses.clear();
    if (floatCubes_ != nullptr && floatCubes_->supported() && settings.specularClasses)
    {
        const Stopwatch classWatch = Stopwatch::StartNew();
        const float invScale = 1.0f / std::max(probe.scale, 1e-6f);
        bool complete = true;
        for (const float roughness : kSpecularClasses)
        {
            std::vector<Vector3> filtered = prefilterSpecular(faces, size, 32, roughness, 32);
            for (Vector3& v : filtered) v = v * invScale;
            std::unique_ptr<RenderTargetCube> cube = floatCubes_->upload(filtered, 32);
            if (cube == nullptr) complete = false;
            probe.prefilteredClasses.push_back(std::move(cube));
        }
        if (!complete) probe.prefilteredClasses.clear();
        if (!loggedSpecularClasses_)
        {
            loggedSpecularClasses_ = true;
            CNA::Logger::Info("cna-room: probe specular classes (" + std::to_string(kSpecularClasses.size()) + " x 32 px, 32 samples) in "
                              + std::to_string(static_cast<double>(classWatch.getElapsedTicksProperty()) / 10000.0) + " ms"
                              + (complete ? "" : " -- upload failed, CNA's 8-bit chain used"));
        }
    }
    device_.SetRenderTarget(nullptr);
    environmentBound_ = false;
    appliedProbe_ = nullptr;
}

void SceneRenderer::dumpProbe(const InteriorProbe& /*probe*/, int size, const std::vector<Vector3>& faces)
{
    // Captured faces as a strip (+X -X +Y -Y +Z -Z), exposed so the mean
    // luminance lands on mid grey, plus the irradiance strip at the cube's
    // own scale. For reading a tint or a missing surface, not a picture.
    try
    {
        std::filesystem::create_directories(probeDumpDirectory_);
        const std::size_t count = static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
        double sum = 0.0;
        for (const Vector3& r : faces) sum += 0.2126 * r.X + 0.7152 * r.Y + 0.0722 * r.Z;
        const float exposure = 0.18f / std::max(static_cast<float>(sum / static_cast<double>(faces.size())), 1e-6f);
        const auto encode = [](float value) {
            value = std::clamp(value, 0.0f, 1.0f);
            value = value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
            return static_cast<std::uint8_t>(std::lround(value * 255.0f));
        };
        std::vector<std::uint8_t> strip(static_cast<std::size_t>(6 * size) * static_cast<std::size_t>(size) * 4u, 255);
        for (int f = 0; f < 6; ++f)
            for (int y = 0; y < size; ++y)
                for (int x = 0; x < size; ++x)
                {
                    const Vector3& r = faces[static_cast<std::size_t>(f) * count + static_cast<std::size_t>(y) * static_cast<std::size_t>(size)
                                             + static_cast<std::size_t>(x)];
                    const std::size_t o = (static_cast<std::size_t>(y) * static_cast<std::size_t>(6 * size) + static_cast<std::size_t>(f * size + x)) * 4u;
                    // Reinhard keeps the shades from clipping to a flat white.
                    const auto tone = [&](float v) { v *= exposure; return v / (1.0f + v); };
                    strip[o] = encode(tone(r.X));
                    strip[o + 1] = encode(tone(r.Y));
                    strip[o + 2] = encode(tone(r.Z));
                }
        const std::string stem = probeDumpDirectory_ + "/probe" + std::to_string(probeDumpCount_);
        Texture2D env = Texture2D::CreateFromPixels(device_, 6 * size, size, strip);
        env.SaveAsPng(stem + "-env.png");
        if (lastIrradianceSize_ > 0)
        {
            const int n = lastIrradianceSize_;
            Texture2D image = Texture2D::CreateFromPixels(device_, 6 * n, n, lastIrradianceStrip_);
            image.SaveAsPng(stem + "-irr.png");
        }
        CNA::Logger::Info("cna-room: probe dump " + stem + "-{env,irr}.png (exposure " + std::to_string(exposure) + ")");
        ++probeDumpCount_;
    }
    catch (const std::exception& error)
    {
        CNA::Logger::Warn(std::string("cna-room: probe dump failed: ") + error.what());
    }
}

void SceneRenderer::captureProbe(InteriorProbe& probe, RenderTarget2D& target, int size,
                                 const RenderSettings& settings, bool skyOnly)
{
    std::vector<Vector3> faces;
    float peak = 1e-4f;
    for (int f = 0; f < 6; ++f) captureProbeFace(probe, target, size, settings, skyOnly, f, faces, peak);
    // Environment only (the mapping detection reads it back); no products.
    const std::size_t count = static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
    if (probe.environment == nullptr || probe.environment->getSizeProperty() != size)
        probe.environment = std::make_unique<TextureCube>(device_, size, false, SurfaceFormat::Color);
    probe.scale = peak * 1.02f;
    std::vector<Color> face(count);
    for (int f = 0; f < 6; ++f)
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            const Vector3& r = faces[static_cast<std::size_t>(f) * count + i];
            face[i] = Color(static_cast<int>(encodeCube(r.X, probe.scale)), static_cast<int>(encodeCube(r.Y, probe.scale)),
                            static_cast<int>(encodeCube(r.Z, probe.scale)), 255);
        }
        probe.environment->SetData(static_cast<CubeMapFace>(f), face.data(), static_cast<int>(count));
    }
}

void SceneRenderer::detectCaptureFormat()
{
    // A half-float target keeps the sun disc and lamp glows above 2.0 in the
    // probe; use it when the device can render into one and read it back.
    captureFormatKnown_ = true;
    captureHdr_ = false;
    try
    {
        RenderTarget2D test(device_, 4, 4, false, SurfaceFormat::HalfVector4, DepthFormat::None);
        device_.SetRenderTarget(&test);
        device_.Clear(Color(255, 128, 0, 255));
        device_.SetRenderTarget(nullptr);
        std::array<PackedVector::HalfVector4, 16> pixels{};
        test.GetData(pixels.data(), 16);
        const Vector4 v = pixels[5].ToVector4();
        captureHdr_ = std::abs(v.X - 1.0f) < 0.01f && std::abs(v.Y - 128.0f / 255.0f) < 0.01f && std::abs(v.Z) < 0.01f;
    }
    catch (const std::exception& failure)
    {
        limitations_.emplace_back(std::string("8-bit probe capture: ") + failure.what());
    }
    if (!captureHdr_ && limitations_.empty())
        limitations_.emplace_back("8-bit probe capture: half-float readback gave wrong values");
    CNA::Logger::Info(std::string("cna-room: probe capture ") + (captureHdr_ ? "half-float" : "8-bit sRGB, radiance clipped at 2.0"));
}

void SceneRenderer::detectProbeMapping(RenderTarget2D& target, int size, const RenderSettings& settings)
{
    // Render the sky alone from the origin and find which of the four
    // readback orientations reproduces the CPU sky per texel.
    InteriorProbe test;
    test.position = Vector3(0.0f, 50.0f, 0.0f);
    double bestError = 1e30;
    bool bestMirror = true, bestFlip = false;
    for (int mirror = 0; mirror < 2; ++mirror)
        for (int flip = 0; flip < 2; ++flip)
        {
            probeMirrorX_ = mirror == 1;
            probeFlipY_ = flip == 1;
            captureProbe(test, target, size, settings, true);
            std::vector<Color> texels(static_cast<std::size_t>(size) * static_cast<std::size_t>(size));
            double error = 0.0;
            int samples = 0;
            for (int f = 0; f < 6; ++f)
            {
                test.environment->GetData(static_cast<CubeMapFace>(f), texels.data(), static_cast<int>(texels.size()));
                for (int y = 0; y < size; y += 4)
                    for (int x = 0; x < size; x += 4)
                    {
                        const Vector3 expected = sky_.radiance(CNA::Graphics::EnvironmentProcessor::faceDirection(
                            f, (static_cast<float>(x) + 0.5f) / static_cast<float>(size), (static_cast<float>(y) + 0.5f) / static_cast<float>(size)));
                        const Color& c = texels[static_cast<std::size_t>(y) * static_cast<std::size_t>(size) + static_cast<std::size_t>(x)];
                        const Vector3 got(static_cast<float>(c.getRProperty()) / 255.0f * test.scale,
                                          static_cast<float>(c.getGProperty()) / 255.0f * test.scale,
                                          static_cast<float>(c.getBProperty()) / 255.0f * test.scale);
                        error += static_cast<double>(Vector3::Distance(expected, got));
                        ++samples;
                    }
            }
            error /= std::max(samples, 1);
            if (error < bestError)
            {
                bestError = error;
                bestMirror = probeMirrorX_;
                bestFlip = probeFlipY_;
            }
            CNA::Logger::Info("cna-room: probe mapping mirrorX=" + std::to_string(probeMirrorX_) + " flipY="
                              + std::to_string(probeFlipY_) + " error " + std::to_string(error));
        }
    probeMirrorX_ = bestMirror;
    probeFlipY_ = bestFlip;
    probeMappingKnown_ = true;
}

void SceneRenderer::requestProbeBake(const std::vector<Vector3>& positions, const RenderSettings& settings,
                                     int iterations)
{
    probeBakeQueue_.clear();
    if (!settings.interiorProbes || !device_.SupportsImageBasedLightingEXT() || sky_.brdfLut() == nullptr)
    {
        probes_.clear();
        assignProbes();
        return;
    }
    currentSettings_ = &settings;
    probeBakeSize_ = std::max(16, settings.probeFaceSize);
    if (!captureFormatKnown_) detectCaptureFormat();
    if (probeTarget_ == nullptr || probeTarget_->getWidthProperty() != probeBakeSize_)
        probeTarget_ = std::make_unique<RenderTarget2D>(device_, probeBakeSize_, probeBakeSize_, false,
                                                        captureHdr_ ? SurfaceFormat::HalfVector4 : SurfaceFormat::Color,
                                                        DepthFormat::Depth24, 0, RenderTargetUsage::PreserveContents);
    if (!probeMappingKnown_) detectProbeMapping(*probeTarget_, probeBakeSize_, settings);

    if (probes_.size() != positions.size())
    {
        probes_.clear();
        for (const Vector3& p : positions)
        {
            auto probe = std::make_unique<InteriorProbe>();
            probe->position = p;
            probes_.push_back(std::move(probe));
        }
        assignProbes();
    }
    assignLamps();
    for (int iteration = 0; iteration < std::max(1, iterations); ++iteration)
        for (std::size_t p = 0; p < probes_.size(); ++p)
            for (int f = 0; f < 6; ++f)
                probeBakeQueue_.push_back(ProbeBakeStep{p, f});
    probeBakeFaces_.clear();
    probeBakePeak_ = 1e-4f;
    probeBakeWatch_ = Stopwatch::StartNew();
    probeBakeStepsDone_ = 0;
}

bool SceneRenderer::stepProbeBake(int faces)
{
    if (probeBakeQueue_.empty() || probeTarget_ == nullptr || currentSettings_ == nullptr) return false;
    for (int i = 0; i < std::max(1, faces) && !probeBakeQueue_.empty(); ++i)
    {
        const ProbeBakeStep step = probeBakeQueue_.front();
        probeBakeQueue_.erase(probeBakeQueue_.begin());
        InteriorProbe& probe = *probes_[step.probe];
        captureProbeFace(probe, *probeTarget_, probeBakeSize_, *currentSettings_, false, step.face, probeBakeFaces_, probeBakePeak_);
        ++probeBakeStepsDone_;
        if (step.face == 5)
        {
            finishProbe(probe, probeBakeSize_, probeBakeFaces_, probeBakePeak_, *currentSettings_);
            probeBakePeak_ = 1e-4f;
        }
    }
    if (probeBakeQueue_.empty())
    {
        probeBakeSeconds_ = milliseconds(probeBakeWatch_) / 1000.0f;
        ++probeBakeCount_;
        if (probeBakeCount_ <= 3)
        {
            for (const auto& probe : probes_)
                CNA::Logger::Info("cna-room: probe at " + std::to_string(probe->position.X) + "," + std::to_string(probe->position.Y)
                                  + "," + std::to_string(probe->position.Z) + " peak radiance " + std::to_string(probe->scale));
            CNA::Logger::Info("cna-room: baked " + std::to_string(probes_.size()) + " interior probes at "
                              + std::to_string(probeBakeSize_) + " px, " + std::to_string(probeBakeStepsDone_) + " faces, in "
                              + std::to_string(probeBakeSeconds_) + " s");
        }
        return false;
    }
    return true;
}

void SceneRenderer::bakeInteriorProbes(const std::vector<Vector3>& positions, const RenderSettings& settings,
                                       int iterations)
{
    requestProbeBake(positions, settings, iterations);
    while (stepProbeBake(6)) {}
}

void SceneRenderer::clearScene()
{
    items_.clear();
    visibleOpaque_.clear();
    visibleTransparent_.clear();
}

BoundingBox SceneRenderer::sceneBounds() const
{
    if (items_.empty()) return BoundingBox(Vector3(-1, -1, -1), Vector3(1, 1, 1));
    BoundingBox b = items_.front().worldBounds;
    for (const SceneItem& item : items_) b = BoundingBox::CreateMerged(b, item.worldBounds);
    return b;
}

void SceneRenderer::setLamps(std::vector<Lamp> lamps)
{
    lamps_ = std::move(lamps);
    lampsDirty_ = true;
}

void SceneRenderer::assignLamps()
{
    // One punctual light per draw (PbrEffect's budget): each item takes the
    // lamp that delivers the most irradiance at its centre.
    for (SceneItem& item : items_)
    {
        item.lamp = -1;
        float best = 0.0f;
        for (std::size_t i = 0; i < lamps_.size(); ++i)
        {
            const Lamp& lamp = lamps_[i];
            if (!lamp.on || lamp.intensity <= 0.0f) continue;
            const float d2 = std::max(Vector3::DistanceSquared(lamp.position, item.worldSphere.Center), 0.04f);
            if (d2 > lamp.range * lamp.range * 1.2f) continue;
            const float score = lamp.intensity / d2;
            if (score > best)
            {
                best = score;
                item.lamp = static_cast<int>(i);
            }
        }
    }
    static const bool debugLamps = std::getenv("CNA_ROOM_DEBUG_LAMPS") != nullptr;
    if (debugLamps && !loggedLampAssignment_)
    {
        loggedLampAssignment_ = true;
        for (const SceneItem& item : items_)
        {
            if (item.exterior) continue;
            const std::string lamp = item.lamp >= 0 ? lamps_[static_cast<std::size_t>(item.lamp)].name : "none";
            CNA::Logger::Info("cna-room: lamp for '" + item.name + "' at " + std::to_string(item.worldSphere.Center.X) + "," + std::to_string(item.worldSphere.Center.Y) + ","
                              + std::to_string(item.worldSphere.Center.Z) + " r " + std::to_string(item.worldSphere.Radius) + ": " + lamp);
        }
    }
}

void SceneRenderer::applyLamp(int lampIndex)
{
    if (lampIndex == appliedLamp_) return;
    appliedLamp_ = lampIndex;
    PunctualLightEXT light;
    if (lampIndex >= 0 && lampIndex < static_cast<int>(lamps_.size()) && !(capturing_ && lamps_[static_cast<std::size_t>(lampIndex)].daylightPortal))
    {
        const Lamp& lamp = lamps_[static_cast<std::size_t>(lampIndex)];
        light.Kind = lamp.spot ? PunctualLightKindEXT::Spot : PunctualLightKindEXT::Point;
        light.Position = lamp.position;
        light.Direction = lamp.direction;
        light.DiffuseColor = lamp.colour * (lamp.intensity * lightScale_);
        light.Range = lamp.range;
        light.InnerAngle = lamp.innerAngle;
        light.OuterAngle = lamp.outerAngle;
        if (lampIndex == shadowedLamp_ && lampShadow_ != nullptr)
        {
            light.ShadowCube = lampShadow_->getShadowTexture();
            light.ShadowDepthBias = 0.006f;
        }
    }
    effect_->setPunctualLightEXT(light);
}

void SceneRenderer::drawLampShadow()
{
    if (lampsDirty_)
    {
        lampsDirty_ = false;
        assignLamps();
        shadowedLamp_ = -1;
        float best = 0.0f;
        for (std::size_t i = 0; i < lamps_.size(); ++i)
            if (lamps_[i].on && lamps_[i].castsShadow && lamps_[i].intensity > best)
            {
                best = lamps_[i].intensity;
                shadowedLamp_ = static_cast<int>(i);
            }
        lampShadowFace_ = 0;   // re-render all six faces, spread over the next frames
        if (shadowedLamp_ < 0 || !device_.SupportsShadowSamplingEXT()) return;
        if (lampShadow_ == nullptr)
        {
            try
            {
                lampShadow_ = std::make_unique<CNA::Graphics::CubeShadowMap>(device_, ShadowQuality::Medium);
            }
            catch (const std::exception& failure)
            {
                limitations_.emplace_back(std::string("no lamp shadow: ") + failure.what());
                return;
            }
            if (!lampShadow_->isSupported())
            {
                lampShadow_.reset();
                shadowedLamp_ = -1;
                return;
            }
        }
        const Lamp& lamp = lamps_[static_cast<std::size_t>(shadowedLamp_)];
        CNA::Graphics::PointLightEXT point;
        point.Position = lamp.position;
        point.Range = lamp.range;
        point.Color = lamp.colour;
        point.Intensity = lamp.intensity;
        lampShadow_->update(point);
    }
    if (shadowedLamp_ < 0 || lampShadow_ == nullptr || lampShadowFace_ >= CNA::Graphics::CubeShadowMap::kFaceCount) return;
    ShaderEffect* caster = lampShadow_->getCasterEffect();
    if (caster == nullptr) return;
    const Lamp& lamp = lamps_[static_cast<std::size_t>(shadowedLamp_)];
    device_.setRasterizerStateProperty(RasterizerState::CullNone);
    device_.setDepthStencilStateProperty(DepthStencilState::Default);
    device_.setBlendStateProperty(BlendState::Opaque);
    // Two faces per frame: the whole cube is fresh within three frames of a
    // switch, and each face only draws the casters inside its 90-degree frustum.
    const Matrix projection = CNA::Graphics::CubeShadowMap::computeFaceProjection(lamp.range);
    for (int n = 0; n < 2 && lampShadowFace_ < CNA::Graphics::CubeShadowMap::kFaceCount; ++n, ++lampShadowFace_)
    {
        const int face = lampShadowFace_;
        const Matrix view = CNA::Graphics::CubeShadowMap::computeFaceView(static_cast<CubeMapFace>(face), lamp.position);
        const BoundingFrustum frustum(view * projection);
        lampShadow_->begin(face);
        for (const SceneItem& item : items_)
        {
            if (!item.castsShadow || item.exterior || item.material == nullptr || !item.material->castsShadow) continue;
            if (Vector3::Distance(item.worldSphere.Center, lamp.position) - item.worldSphere.Radius > lamp.range) continue;
            if (frustum.Contains(item.worldSphere) == ContainmentType::Disjoint) continue;
            caster->SetUniformMat4("uWorld", &item.world.M11);
            item.mesh->draw(device_);
            ++stats_.shadowDrawCalls;
        }
        lampShadow_->end();
    }
    device_.SetRenderTarget(nullptr);
}

void SceneRenderer::cull(const Camera& camera)
{
    visibleOpaque_.clear();
    visibleTransparent_.clear();
    const BoundingFrustum& frustum = camera.frustum();
    for (std::size_t i = 0; i < items_.size(); ++i)
    {
        const SceneItem& item = items_[i];
        if (item.mesh == nullptr || item.material == nullptr || item.shadowOnly) continue;
        if (frustum.Contains(item.worldSphere) == ContainmentType::Disjoint) continue;
        if (item.material->isBlended()) visibleTransparent_.push_back(i);
        else visibleOpaque_.push_back(i);
    }
    // Front to back: the depth test then rejects most hidden fragments before
    // the PBR shader runs (overdraw is the opaque pass's main cost).
    const Vector3 eye = camera.position();
    std::sort(visibleOpaque_.begin(), visibleOpaque_.end(), [&](std::size_t a, std::size_t b) {
        const SceneItem& ia = items_[a];
        const SceneItem& ib = items_[b];
        const Vector3 na = Vector3::Clamp(eye, ia.worldBounds.Min, ia.worldBounds.Max);
        const Vector3 nb = Vector3::Clamp(eye, ib.worldBounds.Min, ib.worldBounds.Max);
        return Vector3::DistanceSquared(eye, na) < Vector3::DistanceSquared(eye, nb);
    });
    stats_.visibleItems = static_cast<int>(visibleOpaque_.size() + visibleTransparent_.size());
    stats_.totalItems = static_cast<int>(items_.size());
}

void SceneRenderer::render(const Camera& camera, const RenderSettings& settings)
{
    currentSettings_ = &settings;
    appliedLamp_ = -2;   // a lamp's level may have moved since last frame (the fire, the windows): re-apply
    stats_.drawCalls = 0;
    stats_.shadowDrawCalls = 0;
    stats_.triangles = 0;
    Stopwatch watch = Stopwatch::StartNew();
    if (pipeline_ != nullptr && settings.exposure != appliedExposure_)
    {
        pipeline_->getSettings().setExposure(settings.exposure);
        appliedExposure_ = settings.exposure;
    }
    if (pipeline_ != nullptr)
    {
        // Bloom runs on scene-referred values before the tonemapper, so its
        // threshold is set in display terms and divided by the exposure: the
        // same glow at noon and under the lamps.
        const float threshold = settings.bloomThreshold / std::max(settings.exposure, 1e-3f);
        if (threshold != appliedBloomThreshold_)
        {
            pipeline_->getSettings().setBloomThreshold(threshold);
            appliedBloomThreshold_ = threshold;
        }
        const float flareThreshold = settings.lensFlareThreshold / std::max(settings.exposure, 1e-3f);
        if (flareThreshold != appliedFlareThreshold_)
        {
            pipeline_->getSettings().setLensFlareThreshold(flareThreshold);
            appliedFlareThreshold_ = flareThreshold;
        }
        if (settings.filmGrain != appliedGrain_ || settings.chromaticAberration != appliedAberration_)
        {
            auto& p = pipeline_->getSettings();
            p.setFilmGrainIntensity(std::clamp(settings.filmGrain, 0.0f, 1.0f));
            p.setChromaticAberrationStrength(std::clamp(settings.chromaticAberration, 0.0f, 0.1f));
            p.setVolumetricFogDensity(std::max(settings.volumetricFog, 0.0f));
            appliedGrain_ = settings.filmGrain;
            appliedAberration_ = settings.chromaticAberration;
        }
        if (fogDensity_ != appliedFogDensity_)
        {
            auto& p = pipeline_->getSettings();
            p.setHeightFogDensity(fogDensity_);
            p.setHeightFogFalloff(fogFalloff_);
            p.setHeightFogBaseHeight(fogBaseHeight_);
            appliedFogDensity_ = fogDensity_;
        }
    }
    if (television_ != nullptr && television_->supported() && televisionPlaying_)
    {
        // CNA_ROOM_TV_TIME_SCALE runs the programme faster (its cycle is 70 s; a
        // deterministic capture steps 1/60 s a frame), for measuring its mean.
        static const float tvTimeScale = std::getenv("CNA_ROOM_TV_TIME_SCALE") != nullptr ? std::strtof(std::getenv("CNA_ROOM_TV_TIME_SCALE"), nullptr) : 1.0f;
        television_->update(frameSeconds_ * tvTimeScale);
    }

    const auto openStage = [this](GpuStage stage) {
        auto& timer = gpuStage_[static_cast<std::size_t>(stage)];
        if (timer == nullptr) return;
        if (timer->poll()) gpuStageMs_[static_cast<std::size_t>(stage)] = timer->getLastMilliseconds();
        timer->begin();
    };
    const auto closeStage = [this](GpuStage stage) {
        auto& timer = gpuStage_[static_cast<std::size_t>(stage)];
        if (timer != nullptr) timer->end();
    };

    // Spread probe re-captures over frames (one face per frame; the whole
    // set is 18 faces for three probes at one bounce iteration).
    if (!probeBakeQueue_.empty()) stepProbeBake(1);
    cull(camera);
    const float afterCull = milliseconds(watch);
    openStage(GpuStage::Shadow);
    drawLampShadow();
    drawShadows(camera, settings);
    closeStage(GpuStage::Shadow);
    const float afterShadow = milliseconds(watch);
    openStage(GpuStage::Prepass);
    drawPrepass(camera, settings);
    closeStage(GpuStage::Prepass);
    const float afterPrepass = milliseconds(watch);

    drawReflections(camera, settings);
    const float afterReflections = milliseconds(watch);
    stats_.reflectionMs = afterReflections - afterPrepass;
    marchSunbeams(camera, settings);
    const float afterMarch = milliseconds(watch);

    if (gpuTimingAvailable_) pipeline_->setGpuTimingEnabledEXT(true);
    pipeline_->setCamera(camera.view(), camera.projection(), camera.nearPlane(), settings.prepassFarPlane);
    pipeline_->setTransparentScene([&] { drawTransparent(camera, settings); });
    pipeline_->begin(Color::Black);

    device_.setDepthStencilStateProperty(DepthStencilState::None);
    device_.setBlendStateProperty(BlendState::Opaque);
    openStage(GpuStage::Sky);
    if (settings.sky) sky_.draw(camera.view(), camera.projection(), width_, height_);
    closeStage(GpuStage::Sky);
    const float afterSky = milliseconds(watch);

    openStage(GpuStage::Opaque);
    drawOpaque(camera, settings);
    closeStage(GpuStage::Opaque);
    const float afterOpaque = milliseconds(watch);
    // Its CPU time says little under asynchronous GL; the GPU stage timer does.
    openStage(GpuStage::Sunbeams);
    drawSunbeams(camera, settings);
    closeStage(GpuStage::Sunbeams);
    stats_.sunbeamMs = (milliseconds(watch) - afterOpaque) + (afterMarch - afterReflections);
    // Image-based exposure: the HDR scene target holds this frame's opaque
    // radiance (the pipeline only hands it out while the frame is open, so
    // the measurement runs before end() and asks for next frame's exposure).
    if (autoExposure_ != nullptr && autoExposureEnabled_ && pipeline_->getSceneTarget() != nullptr)
    {
        try
        {
            plainLuminance_ = autoExposure_->measureAverageLuminance(*pipeline_->getSceneTarget());
            measuredLuminance_ = plainLuminance_;
            if (exposureMeter_ != nullptr && exposureMeter_->supported())
            {
                const ExposureMeter::Reading reading = exposureMeter_->measure(*pipeline_->getSceneTarget());
                if (reading.weighted > 0.0f)
                {
                    measuredLuminance_ = reading.weighted;
                    highlightShare_ = reading.highlightShare;
                }
            }
            measuredExposure_ = std::clamp(autoExposure_->getKeyValue() / std::max(measuredLuminance_, 1e-5f), 0.3f, 2000.0f);
        }
        catch (const std::exception& failure)
        {
            limitations_.emplace_back(std::string("image-based exposure stopped: ") + failure.what());
            autoExposure_.reset();
        }
    }
    pipeline_->end();
    whiteBalanceGain_ = whiteBalanceGain(settings);
    if (vignette_ != nullptr)
    {
        // Display-referred: a linear gain g lands as g^(1/2.2) on the encoded frame.
        const Vector3 encoded(std::pow(whiteBalanceGain_.X, 1.0f / 2.2f), std::pow(whiteBalanceGain_.Y, 1.0f / 2.2f),
                              std::pow(whiteBalanceGain_.Z, 1.0f / 2.2f));
        vignette_->draw(settings.vignette, width_, height_, encoded);
    }

    const auto pipelineStats = pipeline_->getStatistics();
    stats_.postPasses = pipelineStats.passesRun;
    stats_.usedSceneTarget = pipelineStats.usedSceneTarget;
    if (!loggedSceneFormat_)
    {
        loggedSceneFormat_ = true;
        CNA::Logger::Info("cna-room: scene target format " + std::to_string(static_cast<int>(pipeline_->getSceneTargetFormat()))
                          + " (HalfVector4 = " + std::to_string(static_cast<int>(SurfaceFormat::HalfVector4)) + ", Color = "
                          + std::to_string(static_cast<int>(SurfaceFormat::Color)) + ")");
    }
    stats_.cullMs = afterCull;
    stats_.shadowMs = afterShadow - afterCull;
    stats_.prepassMs = afterPrepass - afterShadow;
    stats_.skyMs = afterSky - afterMarch;
    stats_.opaqueMs = afterOpaque - afterSky;
    stats_.postMs = milliseconds(watch) - afterOpaque;
    stats_.frameMs = milliseconds(watch);
    stats_.gpuShadowMs = gpuStageMs_[static_cast<std::size_t>(GpuStage::Shadow)];
    stats_.gpuPrepassMs = gpuStageMs_[static_cast<std::size_t>(GpuStage::Prepass)];
    stats_.gpuSkyMs = gpuStageMs_[static_cast<std::size_t>(GpuStage::Sky)];
    stats_.gpuOpaqueMs = gpuStageMs_[static_cast<std::size_t>(GpuStage::Opaque)];
    stats_.gpuSunbeamMs = gpuStageMs_[static_cast<std::size_t>(GpuStage::Sunbeams)];
    stats_.gpuPostMs = -1.0;
    if (pipeline_->isGpuTimingEnabledEXT())
    {
        double post = 0.0;
        bool any = false;
        std::string breakdown;
        for (const auto& pass : pipeline_->getPassTimingsEXT())
        {
            post += pass.Milliseconds;
            any = true;
            breakdown += pass.Name + " " + std::to_string(pass.Milliseconds) + "  ";
        }
        if (any && !loggedPassTimings_ && ++timedFrames_ == 4)
        {
            loggedPassTimings_ = true;
            CNA::Logger::Info("cna-room: post passes (GPU ms): " + breakdown);
        }
        if (any) stats_.gpuPostMs = post;
    }
}

void SceneRenderer::drawShadows(const Camera& camera, const RenderSettings& settings)
{
    cascadesFitted_ = false;
    if (shadows_ == nullptr || !settings.shadows) return;
    const SkyLighting& lighting = sky_.lighting();
    const bool useMoon = !lighting.sunUp && lighting.moonUp;
    DirectionalLightEXT light;
    light.Direction = useMoon ? lighting.moonLightDirection : lighting.lightDirection;
    light.Color = useMoon ? lighting.moonColour : lighting.sunColour;
    // Light from below the horizon (sunset with the moon down): nothing to
    // cast, and the receivers must not read cascades that were never fitted.
    if (light.Direction.Y > -0.02f) return;
    // A key light under a tenth of the ambient (a storm's sun, the moon
    // behind an overcast deck) casts nothing the eye would find: skip the
    // cascades and their draws.
    const auto luminance = [](const Vector3& v) { return 0.2126f * v.X + 0.7152f * v.Y + 0.0722f * v.Z; };
    if (luminance(light.Color) < 0.1f * luminance(lighting.ambientColour)) return;

    const Matrix shadowProjection =
        camera.projectionForRange(settings.nearPlane, std::min(settings.shadowDistance, settings.farPlane));
    shadows_->update(light, camera.view(), shadowProjection);
    cascadesFitted_ = true;
    keyLightDirection_ = light.Direction;
    keyLightColour_ = light.Color;
    ShaderEffect* caster = shadows_->getCasterEffect();
    if (caster == nullptr) return;

    if (!loggedCascades_)
    {
        loggedCascades_ = true;
        std::string splits;
        for (int i = 0; i < shadows_->getCascadeCount(); ++i)
            splits += std::to_string(shadows_->getSplitDistance(i)) + " ";
        CNA::Logger::Info("cna-room: shadow cascades " + std::to_string(shadows_->getCascadeCount()) + " at "
                          + std::to_string(shadows_->getCascadeSize()) + " px, splits " + splits);
    }

    device_.setRasterizerStateProperty(RasterizerState::CullNone);
    device_.setDepthStencilStateProperty(DepthStencilState::Default);
    device_.setBlendStateProperty(BlendState::Opaque);

    // Per cascade, only casters that can reach its slice of the view frustum:
    // the slice's corners are taken into a light-aligned basis and a caster
    // must overlap their box across the light (u, v) and lie no further along
    // the light's travel than the slice's far side (anything beyond the
    // receivers cannot shadow them).
    Vector3 travel = light.Direction;
    travel.Normalize();
    const Vector3 axis = std::abs(travel.Y) < 0.9f ? Vector3::Up : Vector3::UnitX;
    Vector3 u = Vector3::Cross(axis, travel);
    u.Normalize();
    const Vector3 v = Vector3::Cross(travel, u);
    float sliceNear = settings.nearPlane;
    for (int cascade = 0; cascade < shadows_->getCascadeCount(); ++cascade)
    {
        const float sliceFar = std::min(shadows_->getSplitDistance(cascade), std::min(settings.shadowDistance, settings.farPlane));
        const BoundingFrustum slice(camera.view() * camera.projectionForRange(sliceNear, std::max(sliceFar, sliceNear + 0.01f)));
        std::vector<Vector3> corners = slice.GetCorners();
        Vector3 centre = Vector3::Zero;
        float uMin = 1e30f, uMax = -1e30f, vMin = 1e30f, vMax = -1e30f, wMax = -1e30f;
        for (const Vector3& c : corners)
        {
            centre = centre + c;
            uMin = std::min(uMin, Vector3::Dot(c, u));
            uMax = std::max(uMax, Vector3::Dot(c, u));
            vMin = std::min(vMin, Vector3::Dot(c, v));
            vMax = std::max(vMax, Vector3::Dot(c, v));
            wMax = std::max(wMax, Vector3::Dot(c, travel));
        }
        centre = centre * (1.0f / static_cast<float>(std::max<std::size_t>(corners.size(), 1)));
        float radius = 0.0f;
        for (const Vector3& c : corners) radius = std::max(radius, Vector3::Distance(centre, c));
        sliceNear = sliceFar;

        // Casters too small to show in this cascade's texels are skipped: a
        // 2048 px atlas over a 5 m slice resolves ~3 mm, over 45 m ~2 cm, and
        // a prop under a few centimetres across leaves no readable shadow.
        const float texel = 2.0f * radius / static_cast<float>(std::max(shadows_->getCascadeSize(), 1));
        const float minimumRadius = std::max(0.04f, texel * 6.0f);
        shadows_->begin(cascade);
        int drawn = 0, culledBox = 0, culledSmall = 0;
        for (const SceneItem& item : items_)
        {
            if (!item.castsShadow || item.material == nullptr || !item.material->castsShadow) continue;
            if (item.worldSphere.Radius < minimumRadius) { ++culledSmall; continue; }
            const Vector3& c = item.worldSphere.Center;
            const float r = item.worldSphere.Radius;
            if (settings.shadowCasterCull)
            {
                if (Vector3::Dot(c, travel) - r > wMax) { ++culledBox; continue; }   // beyond the receivers along the light
                const float cu = Vector3::Dot(c, u), cv = Vector3::Dot(c, v);
                if (cu + r < uMin || cu - r > uMax || cv + r < vMin || cv - r > vMax) { ++culledBox; continue; }
            }
            caster->SetUniformMat4("uWorld", &item.world.M11);
            item.mesh->draw(device_);
            ++stats_.shadowDrawCalls;
            ++drawn;
        }
        shadows_->end();
        if (!loggedCascadeCasters_)
            CNA::Logger::Info("cna-room: cascade " + std::to_string(cascade) + ": " + std::to_string(drawn) + " casters drawn, "
                              + std::to_string(culledBox) + " outside the light-space box, " + std::to_string(culledSmall)
                              + " below " + std::to_string(minimumRadius) + " m (slice far " + std::to_string(sliceFar) + " m, radius "
                              + std::to_string(radius) + " m)");
        if (cascade == shadows_->getCascadeCount() - 1) loggedCascadeCasters_ = true;
    }
    if (const char* dump = std::getenv("CNA_ROOM_DUMP_ATLAS"); dump != nullptr && !dumpedAtlasEarly_)
    {
        dumpedAtlasEarly_ = true;
        dumpAtlas(dump);
    }
}

void SceneRenderer::dumpAtlas(const char* path)
{
    try
    {
        const int count = shadows_->getCascadeCount();
        const int atlasW = shadows_->getCascadeSize() * count, atlasH = shadows_->getCascadeSize();
        std::vector<float> atlas(static_cast<std::size_t>(atlasW) * static_cast<std::size_t>(atlasH));
        shadows_->getShadowTexture()->GetData(0, nullptr, atlas.data(), 0, static_cast<int>(atlas.size()));
        std::vector<std::uint8_t> grey(atlas.size() * 4);
        for (std::size_t i = 0; i < atlas.size(); ++i)
        {
            const float v = std::clamp((atlas[i] - 0.5f) / 0.4f, 0.0f, 1.0f);
            const auto g = static_cast<std::uint8_t>(v * 255.0f);
            grey[i * 4] = g; grey[i * 4 + 1] = g; grey[i * 4 + 2] = g; grey[i * 4 + 3] = 255;
        }
        Texture2D png = Texture2D::CreateFromPixels(device_, atlasW, atlasH, grey);
        png.SaveAsPng(path);
        CNA::Logger::Info(std::string("cna-room: shadow atlas written to ") + path + " (" + std::to_string(atlasW) + "x" + std::to_string(atlasH) + ")");
    }
    catch (const std::exception& failure)
    {
        CNA::Logger::Info(std::string("cna-room: shadow atlas dump failed: ") + failure.what());
    }
}

void SceneRenderer::drawPrepass(const Camera& camera, const RenderSettings& settings)
{
    prepassDrawn_ = false;
    if (prepass_ == nullptr || pipeline_ == nullptr || !(settings.ssao || settings.ssr || settings.depthOfField || settings.sunbeams > 0.0f)) return;
    ShaderEffect* prepassEffect = prepass_->getPrepassEffect();
    if (prepassEffect == nullptr || !prepassEffect->IsEffectValid()) return;

    for (int pass = 0; pass < prepass_->getPassCount(); ++pass)
    {
        prepass_->begin(pass, camera.view(), camera.projection(), settings.nearPlane, settings.prepassFarPlane);
        device_.setDepthStencilStateProperty(DepthStencilState::Default);
        device_.setBlendStateProperty(BlendState::Opaque);
        for (std::size_t index : visibleOpaque_)
        {
            const SceneItem& item = items_[index];
            if (!item.material->writesDepth) continue;
            device_.setRasterizerStateProperty(item.material->doubleSided ? RasterizerState::CullNone
                                               : item.material->frontFaceCounterClockwise
                                                   ? RasterizerState::CullClockwise
                                                   : RasterizerState::CullCounterClockwise);
            prepass_->setRoughness(item.material->roughness);
            // The prepass shader reads `uWorld` (its own name, set to the
            // identity by begin); ShaderEffect's World property only feeds a
            // uniform called `World`, so it never reached this program and
            // every placed model was drawn at the origin (R-30). The depth
            // and normals the SSAO and the autofocus read were wrong for them.
            prepassEffect->Apply();
            prepassEffect->SetUniformMat4("uWorld", &item.world.M11);
            item.mesh->draw(device_);
        }
        prepass_->end();
    }
    prepassDrawn_ = true;
    pipeline_->setDepthNormalInputs(prepass_->getDepthTexture(), prepass_->getNormalTexture());
    if (settings.depthOfField) readFocus(settings);
    dumpDepth(camera, settings);
}

void SceneRenderer::readFocus(const RenderSettings& settings)
{
    // Centre-area autofocus over a 128 x 80 block at the frame's centre (read
    // back from the prepass, packed RGBA8 or half-float), pulled toward at
    // focusRate_ per second like a lens motor. The block's depths are split
    // at their largest gap into a near and a far cluster: a near cluster
    // that fills under 60 % of the area is an obstruction (the pendant's
    // cable, a lamp stem, the window mullion in front of the street) and
    // the far cluster's median takes the focus; a subject that fills the
    // centre keeps it. A manual --focus skips the measurement.
    if (settings.dofFocusDistance > 0.0f)
    {
        focusSmoothed_ = settings.dofFocusDistance;
    }
    else if (focusReadable_ && prepass_ != nullptr)
    {
        Texture2D* depth = prepass_->getDepthTexture();
        const int w = depth->getWidthProperty(), h = depth->getHeightProperty();
        const int blockW = std::min(128, w), blockH = std::min(80, h);
        if (blockW >= 8 && blockH >= 8)
        {
            const Rectangle centre(w / 2 - blockW / 2, h / 2 - blockH / 2, blockW, blockH);
            const std::size_t count = static_cast<std::size_t>(blockW) * static_cast<std::size_t>(blockH);
            std::vector<float> samples(count, 0.0f);
            bool ok = true;
            try
            {
                if (prepass_->isDepthPacked())
                {
                    std::vector<Color> packed(count);
                    depth->GetData(0, &centre, packed.data(), 0, static_cast<int>(count));
                    for (std::size_t i = 0; i < count; ++i)
                        samples[i] = (static_cast<float>(packed[i].getAProperty())
                                      + static_cast<float>(packed[i].getBProperty()) / 255.0f
                                      + static_cast<float>(packed[i].getGProperty()) / 65025.0f
                                      + static_cast<float>(packed[i].getRProperty()) / 16581375.0f) / 255.0f;
                }
                else
                {
                    std::vector<Microsoft::Xna::Framework::Graphics::PackedVector::HalfVector4> half(count);
                    depth->GetData(0, &centre, half.data(), 0, static_cast<int>(count));
                    for (std::size_t i = 0; i < count; ++i) samples[i] = half[i].ToVector4().X;
                }
            }
            catch (const std::exception& failure)
            {
                CNA::Logger::Warn(std::string("cna-room: autofocus cannot read the prepass depth: ") + failure.what());
                focusReadable_ = false;
                ok = false;
            }
            if (ok)
            {
                std::sort(samples.begin(), samples.end());
                const auto at = [&samples](std::size_t i) { return samples[std::min(i, samples.size() - 1)]; };
                // The largest relative jump between neighbours in the sorted
                // depths, looked for between a fifth and four fifths of the way.
                std::size_t split = 0;
                float bestRatio = 1.5f;
                for (std::size_t i = count / 5; i < count * 4 / 5; ++i)
                {
                    const float ratio = at(i + 1) / std::max(at(i), 1e-4f);
                    if (ratio > bestRatio) { bestRatio = ratio; split = i + 1; }
                }
                float depth01 = at(count / 2);
                if (split > 0)
                {
                    const float nearShare = static_cast<float>(split) / static_cast<float>(count);
                    depth01 = nearShare < 0.6f ? at(split + (count - split) / 2) : at(split / 2);
                }
                // The sky writes no depth (1.0 after the clear): focus at the far plane then.
                measuredFocus_ = std::clamp(depth01 * settings.prepassFarPlane, settings.nearPlane * 4.0f, settings.prepassFarPlane);
            }
        }
        if (measuredFocus_ > 0.0f)
        {
            const float rate = 1.0f - std::exp(-std::max(frameDt_, 0.0f) * focusRate_);
            focusSmoothed_ = std::exp(std::log(focusSmoothed_) + (std::log(measuredFocus_) - std::log(focusSmoothed_)) * rate);
        }
    }
    if (pipeline_ != nullptr && focusSmoothed_ != appliedFocus_)
    {
        pipeline_->getSettings().setDOFFocusDistance(focusSmoothed_);
        appliedFocus_ = focusSmoothed_;
    }
}

void SceneRenderer::dumpDepth(const Camera& camera, const RenderSettings& settings)
{
    if (!depthDumpPath_.empty() && prepass_ != nullptr)
    {
        // The items the prepass drew whose bounds come within a metre of the
        // camera: what a wrong autofocus reading is most likely looking at.
        for (std::size_t index : visibleOpaque_)
        {
            const SceneItem& item = items_[index];
            if (!item.material->writesDepth) continue;
            const float near = Vector3::Distance(camera.position(), item.worldSphere.Center) - item.worldSphere.Radius;
            if (near < 1.0f)
                CNA::Logger::Info("cna-room: depth dump -- near item '" + item.name + "' (" + item.material->name + ") sphere radius "
                                  + std::to_string(item.worldSphere.Radius) + " within " + std::to_string(std::max(near, 0.0f)) + " m, box "
                                  + std::to_string(item.worldBounds.Min.X) + "," + std::to_string(item.worldBounds.Min.Y) + "," + std::to_string(item.worldBounds.Min.Z) + " .. "
                                  + std::to_string(item.worldBounds.Max.X) + "," + std::to_string(item.worldBounds.Max.Y) + "," + std::to_string(item.worldBounds.Max.Z));
        }
        // The whole depth image decoded to grey, 0..10 m, with the autofocus block marked.
        Texture2D* depth = prepass_->getDepthTexture();
        const int w = depth->getWidthProperty(), h = depth->getHeightProperty();
        const std::size_t count = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
        std::vector<float> metres(count, 0.0f);
        try
        {
            if (prepass_->isDepthPacked())
            {
                std::vector<Color> packed(count);
                depth->GetData(0, nullptr, packed.data(), 0, static_cast<int>(count));
                for (std::size_t i = 0; i < count; ++i)
                    metres[i] = (static_cast<float>(packed[i].getAProperty()) + static_cast<float>(packed[i].getBProperty()) / 255.0f
                                 + static_cast<float>(packed[i].getGProperty()) / 65025.0f
                                 + static_cast<float>(packed[i].getRProperty()) / 16581375.0f) / 255.0f * settings.prepassFarPlane;
            }
            else
            {
                std::vector<Microsoft::Xna::Framework::Graphics::PackedVector::HalfVector4> half(count);
                depth->GetData(0, nullptr, half.data(), 0, static_cast<int>(count));
                for (std::size_t i = 0; i < count; ++i) metres[i] = half[i].ToVector4().X * settings.prepassFarPlane;
            }
            std::vector<std::uint8_t> rgba(count * 4u, 255);
            for (std::size_t i = 0; i < count; ++i)
            {
                const auto grey = static_cast<std::uint8_t>(std::clamp(metres[i] / 10.0f, 0.0f, 1.0f) * 255.0f);
                rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = grey;
            }
            for (int y = h / 2 - 40; y < h / 2 + 40; ++y)
                for (int x = w / 2 - 64; x < w / 2 + 64; ++x)
                    if (y == h / 2 - 40 || y == h / 2 + 39 || x == w / 2 - 64 || x == w / 2 + 63)
                        rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 4u] = 255;
            Texture2D image = Texture2D::CreateFromPixels(device_, w, h, rgba);
            image.SaveAsPng(depthDumpPath_);
            CNA::Logger::Info("cna-room: prepass depth written to " + depthDumpPath_ + " (centre "
                              + std::to_string(metres[(static_cast<std::size_t>(h / 2)) * static_cast<std::size_t>(w) + static_cast<std::size_t>(w / 2)]) + " m)");
        }
        catch (const std::exception& failure)
        {
            CNA::Logger::Warn(std::string("cna-room: depth dump failed: ") + failure.what());
        }
        depthDumpPath_.clear();
    }
}

void SceneRenderer::applyLighting(const RenderSettings& settings)
{
    PbrEffect& effect = *effect_;
    const SkyLighting& lighting = sky_.lighting();
    effect.setLightingEnabledProperty(true);

    auto& sun = effect.getDirectionalLight0Property();
    sun.setEnabledProperty(true);
    sun.setDirectionProperty(lighting.lightDirection);
    sun.setDiffuseColorProperty(lighting.sunColour * lightScale_);
    sun.setSpecularColorProperty(lighting.sunColour * lightScale_);

    auto& moon = effect.getDirectionalLight1Property();
    moon.setEnabledProperty(lighting.moonUp);
    moon.setDirectionProperty(lighting.moonLightDirection);
    moon.setDiffuseColorProperty(lighting.moonColour * lightScale_);
    moon.setSpecularColorProperty(lighting.moonColour * lightScale_);
    // Lightning: a brief blue-white key from the strike's direction, no shadows.
    auto& flash = effect.getDirectionalLight2Property();
    flash.setEnabledProperty(lightning_ > 0.001f);
    flash.setDirectionProperty(lightningDirection_);
    const Vector3 flashColour = Vector3(0.85f, 0.9f, 1.0f) * (lightning_ * 2.5f * lightScale_);
    flash.setDiffuseColorProperty(flashColour);
    flash.setSpecularColorProperty(flashColour);

    effect.setAmbientLightColorProperty(lighting.ambientColour * lightScale_);
    effect.setFogEnabledProperty(false);

    environmentBound_ = false;
    appliedProbe_ = nullptr;
    appliedSpecularClass_ = -1;
    appliedLamp_ = -2;
    applyEnvironment(nullptr, settings, 2);

    if (shadows_ != nullptr && settings.shadows && cascadesFitted_)
    {
        shadows_->applyToReceiver(effect);
        // Under cloud the sun's disc is smeared over the deck and its shadows
        // lose their edge: widen the PCF radius (the receiver clamps at 2).
        const float softness = sky_.lighting().shadowSoftness;
        const int cloudRadius = softness > 0.6f ? 2 : softness > 0.25f ? 1 : 0;
        effect.setShadowFilterRadiusEXT(std::max(effect.getShadowFilterRadiusEXT(), cloudRadius));
        effect.setShadowsEnabledEXT(true);
        effect.setShadowDepthBiasEXT(settings.shadowDepthBias);
    }
    else
    {
        effect.setShadowsEnabledEXT(false);
    }
    appliedMaterial_ = nullptr;
}

void SceneRenderer::applyEnvironment(const InteriorProbe* probe, const RenderSettings& settings, int specularClass)
{
    if (environmentBound_ && probe == appliedProbe_ && specularClass == appliedSpecularClass_) return;
    environmentBound_ = true;
    appliedProbe_ = probe;
    appliedSpecularClass_ = specularClass;
    PbrEffect& effect = *effect_;
    if (!settings.imageBasedLighting || sky_.brdfLut() == nullptr)
    {
        effect.setImageBasedLightEXT(ImageBasedLightEXT{});
        return;
    }
    ImageBasedLightEXT environment;
    environment.BrdfLut = sky_.brdfLut();
    const std::size_t klass = static_cast<std::size_t>(std::clamp(specularClass, 0, static_cast<int>(kSpecularClasses.size()) - 1));
    if (probe != nullptr && probe->prefiltered != nullptr && probe->irradiance != nullptr)
    {
        environment.Irradiance = probe->irradiance.get();
        if (settings.specularClasses && probe->prefilteredClasses.size() == kSpecularClasses.size()
            && probe->prefilteredClasses[klass] != nullptr)
        {
            environment.PrefilteredSpecular = probe->prefilteredClasses[klass].get();
            environment.PrefilteredMipCount = 1;
        }
        else
        {
            environment.PrefilteredSpecular = probe->prefiltered.get();
            environment.PrefilteredMipCount = probe->prefilteredMips;
        }
        environment.Intensity = probe->scale * settings.iblIntensity * lightScale_;
    }
    else if (sky_.hasImageBasedLighting())
    {
        environment.Irradiance = sky_.irradiance();
        if (settings.specularClasses && sky_.prefilteredClass(static_cast<int>(klass)) != nullptr)
        {
            environment.PrefilteredSpecular = sky_.prefilteredClass(static_cast<int>(klass));
            environment.PrefilteredMipCount = 1;
        }
        else
        {
            environment.PrefilteredSpecular = sky_.prefiltered();
            environment.PrefilteredMipCount = sky_.prefilteredMipCount();
        }
        environment.Intensity = sky_.environmentScale() * settings.iblIntensity * lightScale_;
    }
    else
    {
        effect.setImageBasedLightEXT(ImageBasedLightEXT{});
        return;
    }
    effect.setImageBasedLightEXT(environment);
}

void SceneRenderer::applyMaterial(const Material& material, const Matrix& world, const Matrix& view,
                                  const Matrix& projection, const InteriorProbe* probe, int lampIndex)
{
    PbrEffect& effect = *effect_;
    effect.setWorldProperty(world);
    effect.setViewProperty(view);
    effect.setProjectionProperty(projection);
    applyEnvironment(probe, *currentSettings_, specularClassFor(std::clamp(material.roughness * material.roughnessMapMean, 0.0f, 1.0f)));
    applyLamp(lampIndex);

    if (appliedMaterial_ != &material)
    {
        appliedMaterial_ = &material;
        const SkyLighting& lighting = sky_.lighting();
        const Vector3 sunColour = material.sunlit ? lighting.sunColour * lightScale_ : Vector3::Zero;
        auto& sun = effect.getDirectionalLight0Property();
        sun.setDiffuseColorProperty(sunColour);
        sun.setSpecularColorProperty(sunColour);

        effect.setDiffuseColorProperty(material.baseColour);
        effect.setAlphaProperty(material.alpha);
        effect.setMetallicFactorProperty(material.metallic);
        effect.setRoughnessFactorProperty(material.roughness);
        // Probe captures see the lamps' glows dimmed: the punctual lights carry
        // the lamps' energy, and a shade 400x brighter than the walls would set
        // the 8-bit cube's scale so high that the room quantises to speckle.
        effect.setEmissiveFactorProperty(capturing_ ? material.emissiveFactor * kProbeGlowScale : material.emissiveFactor);
        effect.setTextureProperty(material.albedo);
        effect.setNormalMapProperty(material.normal);
        effect.setMetallicRoughnessMapProperty(material.orm);
        effect.setOcclusionMapProperty(material.occlusion != nullptr ? material.occlusion
                                       : material.ormHasOcclusion ? material.orm : nullptr);
        effect.setEmissiveMapProperty(material.emissive);
        effect.setBaseColorTextureIsSrgbEXTProperty(true);
        effect.setEmissiveTextureIsSrgbEXTProperty(true);
        effect.setNormalScaleEXTProperty(material.normalScale);
        effect.setOcclusionStrengthEXTProperty(material.occlusionStrength);
        effect.setIorEXTProperty(material.ior);
        effect.setSpecularFactorEXTProperty(material.specular);
        // The tonemapper reads scene-referred linear values from the HDR target;
        // encoding here would be applied twice (cna-street CNA-F8).
        effect.setEncodeOutputToSrgbEXTProperty(!usingSceneTarget_ && !(capturing_ && captureHdr_) && !mirrorPass_);
        effect.setAlphaModeEXTProperty(material.alphaMode);
        effect.setAlphaCutoffEXTProperty(material.alphaCutoff);
        effect.setDoubleSidedEXTProperty(material.doubleSided);

        if (material.perSlotTransforms)
        {
            for (int slot = 0; slot < 5; ++slot)
            {
                effect.setTextureTransformEXTProperty(slot, material.slotTransforms[static_cast<std::size_t>(slot)]);
                effect.setTextureCoordinateSetEXTProperty(slot, material.slotTexCoords[static_cast<std::size_t>(slot)]);
            }
        }
        else
        {
            TextureTransformEXT transform;
            transform.Scale = material.uvScale;
            transform.Offset = material.uvOffset;
            for (int slot = 0; slot < 5; ++slot)
            {
                effect.setTextureTransformEXTProperty(slot, transform);
                effect.setTextureCoordinateSetEXTProperty(slot, 0);
            }
        }

        // A reflection flips the winding: the mirrored pass culls the other side.
        const bool counterClockwise = material.frontFaceCounterClockwise != mirrorPass_;
        device_.setRasterizerStateProperty(material.doubleSided ? RasterizerState::CullNone
                                           : counterClockwise ? RasterizerState::CullClockwise
                                                              : RasterizerState::CullCounterClockwise);
        device_.setBlendStateProperty(!material.isBlended()   ? BlendState::Opaque
                                      : material.reflectiveBlend ? BlendState::AlphaBlend
                                                                 : BlendState::NonPremultiplied);
        for (int unit = 0; unit < 6; ++unit)
            device_.getSamplerStatesProperty()[unit] = SamplerState::AnisotropicWrap;
    }
    effect.Apply();
}

void SceneRenderer::setSunbeamVolume(const Vector3& min, const Vector3& max)
{
    sunbeamMin_ = min;
    sunbeamMax_ = max;
    sunbeamVolumeSet_ = true;
}

bool SceneRenderer::sunbeamInputs(const Camera& camera, const RenderSettings& settings, Sunbeams::Inputs& in)
{
    // Sunlight scattered by the room's air, from the cascades and the prepass
    // depth: needs both this frame, and a key light worth casting.
    const auto skip = [&](const char* why) {
        if (!loggedSunbeamSkip_)
        {
            loggedSunbeamSkip_ = true;
            CNA::Logger::Info(std::string("cna-room: sunbeams skipped this frame: ") + why);
        }
    };
    if (sunbeams_ == nullptr || !sunbeams_->supported()) return false;
    if (settings.sunbeams <= 0.0f) return false;
    if (!sunbeamVolumeSet_) { skip("no volume"); return false; }
    if (!cascadesFitted_) { skip("cascades not fitted"); return false; }
    if (shadows_ == nullptr || shadows_->getShadowTexture() == nullptr) { skip("no atlas"); return false; }
    if (!prepassDrawn_) { skip("no prepass"); return false; }
    in.inverseViewProjection = Matrix::Invert(camera.view() * camera.projection());
    in.cameraPosition = camera.position();
    in.cameraForward = camera.forward();
    in.prepassFarPlane = settings.prepassFarPlane;
    in.depth = prepass_->getDepthTexture();
    in.depthPacked = prepass_->isDepthPacked();
    in.shadowAtlas = shadows_->getShadowTexture();
    in.cascadeCount = std::min(shadows_->getCascadeCount(), 4);
    for (int i = 0; i < in.cascadeCount; ++i)
    {
        in.cascadeMatrices[static_cast<std::size_t>(i)] = shadows_->getCascadeMatrix(i);
        in.splitDistances[static_cast<std::size_t>(i)] = shadows_->getSplitDistance(i);
    }
    const float size = static_cast<float>(std::max(shadows_->getCascadeSize(), 1));
    in.shadowTexel = Vector2(1.0f / (size * static_cast<float>(in.cascadeCount)), 1.0f / size);
    in.shadowBias = settings.shadowDepthBias;
    in.lightDirection = keyLightDirection_;
    in.lightColour = keyLightColour_ * lightScale_;
    in.density = settings.sunbeams;
    in.anisotropy = settings.sunbeamAnisotropy;
    in.steps = settings.sunbeamSteps;
    in.roomMin = sunbeamMin_;
    in.roomMax = sunbeamMax_;
    in.motes = settings.sunbeamMotes;
    in.moteSize = settings.sunbeamMoteSize;
    in.halfResolution = settings.sunbeamHalfResolution;
    // Motes show only in real sunbeams: their glint fades with the key light's
    // strength against the ambient (a cloud-dimmed sun lights the air but not
    // the specks, which read as snowflakes otherwise).
    {
        const SkyLighting& lighting = sky_.lighting();
        const auto luminance = [](const Vector3& v) { return 0.2126f * v.X + 0.7152f * v.Y + 0.0722f * v.Z; };
        const float ratio = luminance(keyLightColour_) / std::max(luminance(lighting.ambientColour), 1e-4f);
        in.moteBrightness = std::clamp((ratio - 2.5f) / 5.0f, 0.0f, 1.0f);
        if (in.moteBrightness <= 0.0f) in.motes = 0;
    }
    in.time = frameSeconds_;
    in.viewProjection = camera.view() * camera.projection();
    const Matrix& view = camera.view();
    in.cameraRight = Vector3(view.M11, view.M21, view.M31);
    in.cameraUp = Vector3(view.M12, view.M22, view.M32);
    static const int debug = std::getenv("CNA_ROOM_DEBUG_SUNBEAMS") != nullptr ? std::atoi(std::getenv("CNA_ROOM_DEBUG_SUNBEAMS")) : 0;
    in.debug = debug;
    if (debug != 0 && !loggedSunbeams_)
    {
        loggedSunbeams_ = true;
        // Named world points (CNA_ROOM_SUNBEAM_POINTS=x,y,z;...): the compare there, rows bottom-up.
        if (const char* pts = std::getenv("CNA_ROOM_SUNBEAM_POINTS"))
        {
            try
            {
                const int atlasW = shadows_->getCascadeSize() * in.cascadeCount, atlasH = shadows_->getCascadeSize();
                std::vector<float> atlas(static_cast<std::size_t>(atlasW) * static_cast<std::size_t>(atlasH));
                in.shadowAtlas->GetData(0, nullptr, atlas.data(), 0, static_cast<int>(atlas.size()));
                std::string list(pts);
                std::size_t pos = 0;
                while (pos < list.size())
                {
                    const std::size_t semi = list.find(';', pos);
                    const std::string item = list.substr(pos, semi == std::string::npos ? std::string::npos : semi - pos);
                    pos = semi == std::string::npos ? list.size() : semi + 1;
                    float q[3] = {0.0f, 0.0f, 0.0f};
                    std::size_t start = 0;
                    for (int k = 0; k < 3; ++k)
                    {
                        q[k] = std::strtof(item.c_str() + start, nullptr);
                        const std::size_t comma = item.find(',', start);
                        if (comma == std::string::npos) break;
                        start = comma + 1;
                    }
                    const Vector3 w(q[0], q[1], q[2]);
                    const float viewDepth = Vector3::Dot(w - in.cameraPosition, in.cameraForward);
                    int index = in.cascadeCount - 1;
                    for (int i = 0; i < in.cascadeCount; ++i)
                        if (viewDepth <= in.splitDistances[static_cast<std::size_t>(i)]) { index = i; break; }
                    const Microsoft::Xna::Framework::Vector4 a = Microsoft::Xna::Framework::Vector4::Transform(Microsoft::Xna::Framework::Vector4(w.X, w.Y, w.Z, 1.0f), in.cascadeMatrices[static_cast<std::size_t>(index)]);
                    const float ux = a.X / a.W, uy = a.Y / a.W, uz = a.Z / a.W;
                    const int tx = std::clamp(static_cast<int>(ux * static_cast<float>(atlasW)), 0, atlasW - 1);
                    const int ty = atlasH - 1 - std::clamp(static_cast<int>(uy * static_cast<float>(atlasH)), 0, atlasH - 1);
                    const float stored = atlas[static_cast<std::size_t>(ty) * static_cast<std::size_t>(atlasW) + static_cast<std::size_t>(tx)];
                    CNA::Logger::Info("cna-room: sunbeams point (" + std::to_string(w.X) + "," + std::to_string(w.Y) + "," + std::to_string(w.Z) + ") view " + std::to_string(viewDepth)
                                      + " cascade " + std::to_string(index) + " uv " + std::to_string(ux) + "," + std::to_string(uy) + " z " + std::to_string(uz) + " stored "
                                      + std::to_string(stored) + (uz - in.shadowBias <= stored ? " LIT" : " shadowed"));
                }
            }
            catch (const std::exception& failure)
            {
                CNA::Logger::Info(std::string("cna-room: sunbeams point probe failed: ") + failure.what());
            }
        }
    }
    return true;
}

void SceneRenderer::marchSunbeams(const Camera& camera, const RenderSettings& settings)
{
    // The half-size march, before the pipeline opens its scene target.
    Sunbeams::Inputs in;
    if (sunbeamInputs(camera, settings, in)) sunbeams_->march(in, width_, height_);
}

void SceneRenderer::drawSunbeams(const Camera& camera, const RenderSettings& settings)
{
    Sunbeams::Inputs in;
    if (sunbeamInputs(camera, settings, in)) sunbeams_->draw(in, width_, height_);
}

void SceneRenderer::drawOpaque(const Camera& camera, const RenderSettings& settings)
{
    usingSceneTarget_ = pipeline_ != nullptr && pipeline_->isUsingSceneTarget();
    device_.setDepthStencilStateProperty(DepthStencilState::Default);
    device_.setBlendStateProperty(BlendState::Opaque);
    applyLighting(settings);
    const Matrix& view = camera.view();
    const Matrix& projection = camera.projection();
    for (std::size_t index : visibleOpaque_)
    {
        const SceneItem& item = items_[index];
        PlanarReflection* reflection = activeReflection(*item.material);
        if (reflection != nullptr && !item.material->reflectionOverlay)
        {
            reflection->drawSurface(*item.material, item.world, view, projection, camera.position(), *item.mesh, !usingSceneTarget_);
            appliedMaterial_ = nullptr;   // the custom effect changed the device state
            ++stats_.drawCalls;
            stats_.triangles += static_cast<std::size_t>(item.mesh->triangleCount());
            continue;
        }
        applyMaterial(*item.material, item.world, view, projection, item.probe, item.lamp);
        item.mesh->draw(device_);
        ++stats_.drawCalls;
        stats_.triangles += static_cast<std::size_t>(item.mesh->triangleCount());
        if (reflection != nullptr)
        {
            // A wet road: its Fresnel share of the mirrored street added over
            // the plain draw (the tint carries the wetness), depth-read at
            // the same surface.
            static const bool debugOverlay = std::getenv("CNA_ROOM_DEBUG_PUDDLES") != nullptr;
            if (debugOverlay)
            {
                const Vector3 c = item.worldSphere.Center;
                const Vector3 onSurface(c.X, item.worldBounds.Max.Y, c.Z);
                const Vector4 r = Vector4::Transform(Vector4(onSurface.X, onSurface.Y, onSurface.Z, 1.0f), reflection->view() * reflection->projection());
                const Vector4 m = Vector4::Transform(Vector4(onSurface.X, onSurface.Y, onSurface.Z, 1.0f), view * projection);
                CNA::Logger::Info("cna-room: reflection overlay on '" + item.name + "' (" + item.material->name + ") centre "
                                  + std::to_string(onSurface.X) + "," + std::to_string(onSurface.Y) + "," + std::to_string(onSurface.Z)
                                  + " -> capture ndc " + std::to_string(r.X / r.W) + "," + std::to_string(r.Y / r.W) + " w " + std::to_string(r.W)
                                  + " | main ndc " + std::to_string(m.X / m.W) + "," + std::to_string(m.Y / m.W) + " w " + std::to_string(m.W));
            }
            reflection->drawSurface(*item.material, item.world, view, projection, camera.position(), *item.mesh, !usingSceneTarget_, true);
            device_.setDepthStencilStateProperty(DepthStencilState::Default);
            device_.setBlendStateProperty(BlendState::Opaque);
            appliedMaterial_ = nullptr;
            ++stats_.drawCalls;
        }
    }
}

void SceneRenderer::drawReflections(const Camera& camera, const RenderSettings& settings)
{
    stats_.reflectionDrawCalls = 0;
    if (!settings.planarReflections || reflectionPlanes_.empty()) return;
    for (std::size_t p = 0; p < reflectionPlanes_.size(); ++p)
    {
        PlanarReflection& reflection = *reflections_[p];
        if (!reflection.supported()) continue;
        if (!reflectionPlanes_[p].enabled || settings.exposure < reflectionPlanes_[p].minExposure
            || !reflection.prepare(reflectionPlanes_[p], camera))
        {
            if (std::getenv("CNA_ROOM_DEBUG_REFLECTIONS") != nullptr)
                CNA::Logger::Info("cna-room: reflection '" + reflectionPlanes_[p].name + "' skipped (enabled "
                                  + std::to_string(reflectionPlanes_[p].enabled) + ", exposure " + std::to_string(settings.exposure) + ")");
            reflection.invalidate();
            continue;
        }
        if (std::getenv("CNA_ROOM_DEBUG_REFLECTIONS") != nullptr)
            CNA::Logger::Info("cna-room: reflection '" + reflectionPlanes_[p].name + "' captured");
        const bool exteriorOnly = reflectionPlanes_[p].exteriorOnly;
        const float skipBelow = reflectionPlanes_[p].skipBelow;
        const Matrix& view = reflection.view();
        const Matrix& projection = reflection.projection();
        const BoundingFrustum& frustum = reflection.frustum();
        mirrorPass_ = true;
        usingSceneTarget_ = false;   // the capture is linear half-float: no encode
        reflection.beginCapture();
        device_.setDepthStencilStateProperty(DepthStencilState::None);
        device_.setBlendStateProperty(BlendState::Opaque);
        if (settings.sky) sky_.draw(view, projection, reflection.width(), reflection.height(), false, 1.0f);
        device_.setDepthStencilStateProperty(DepthStencilState::Default);
        applyLighting(settings);
        for (const SceneItem& item : items_)
        {
            if (item.mesh == nullptr || item.material == nullptr || item.shadowOnly) continue;
            if (item.material->isBlended() || (exteriorOnly && !item.exterior) || item.worldBounds.Max.Y < skipBelow) continue;
            // Small clutter (cups, books, candles) is below the capture's
            // resolution: a quarter of the draws for nothing visible.
            if (item.worldSphere.Radius < 0.12f) continue;
            if (frustum.Contains(item.worldSphere) == ContainmentType::Disjoint) continue;
            // A mirror in a mirror: the surface draws as its plain material.
            applyMaterial(*item.material, item.world, view, projection, item.probe, item.lamp);
            item.mesh->draw(device_);
            ++stats_.reflectionDrawCalls;
        }
        // Blended items (panes, glass) over the capture, in item order: few
        // enough that the back-to-front sort is not worth its cost here.
        device_.setDepthStencilStateProperty(DepthStencilState::DepthRead);
        for (const SceneItem& item : items_)
        {
            if (item.mesh == nullptr || item.material == nullptr || item.shadowOnly) continue;
            if (!item.material->isBlended() || (exteriorOnly && !item.exterior) || item.worldBounds.Max.Y < skipBelow) continue;
            if (frustum.Contains(item.worldSphere) == ContainmentType::Disjoint) continue;
            applyMaterial(*item.material, item.world, view, projection, item.probe, item.lamp);
            item.mesh->draw(device_);
            ++stats_.reflectionDrawCalls;
        }
        device_.setDepthStencilStateProperty(DepthStencilState::Default);
        device_.setBlendStateProperty(BlendState::Opaque);
        reflection.endCapture();
        mirrorPass_ = false;
        appliedMaterial_ = nullptr;
        environmentBound_ = false;
        if (!probeDumpDirectory_.empty()) dumpReflection(reflection, p);
    }
}

void SceneRenderer::dumpReflection(PlanarReflection& reflection, std::size_t plane)
{
    // The capture as a tonemapped PNG (debugging aid, with --dump-probes).
    try
    {
        RenderTarget2D* target = reflection.target();
        if (target == nullptr || target->getFormatProperty() != SurfaceFormat::HalfVector4) return;
        const int w = reflection.width(), h = reflection.height();
        std::vector<PackedVector::HalfVector4> pixels(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
        target->GetData(pixels.data(), static_cast<int>(pixels.size()));
        double sum = 0.0;
        for (const auto& p : pixels)
        {
            const Vector4 v = p.ToVector4();
            sum += 0.2126 * v.X + 0.7152 * v.Y + 0.0722 * v.Z;
        }
        const float exposure = 0.18f / std::max(static_cast<float>(sum / static_cast<double>(pixels.size())), 1e-6f);
        std::vector<std::uint8_t> rgba(pixels.size() * 4u, 255);
        const auto encode = [](float value) {
            value = std::clamp(value, 0.0f, 1.0f);
            value = value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
            return static_cast<std::uint8_t>(std::lround(value * 255.0f));
        };
        for (std::size_t i = 0; i < pixels.size(); ++i)
        {
            const Vector4 v = pixels[i].ToVector4();
            const auto tone = [&](float x) { x *= exposure; return x / (1.0f + x); };
            rgba[i * 4] = encode(tone(v.X));
            rgba[i * 4 + 1] = encode(tone(v.Y));
            rgba[i * 4 + 2] = encode(tone(v.Z));
        }
        std::filesystem::create_directories(probeDumpDirectory_);
        Texture2D image = Texture2D::CreateFromPixels(device_, w, h, rgba);
        image.SaveAsPng(probeDumpDirectory_ + "/reflection" + std::to_string(plane) + "-" + std::to_string(reflectionDumpCount_++) + ".png");
    }
    catch (const std::exception& error)
    {
        CNA::Logger::Warn(std::string("cna-room: reflection dump failed: ") + error.what());
    }
}

void SceneRenderer::setLightning(float strength, const Vector3& direction)
{
    lightning_ = std::max(0.0f, strength);
    const float l = direction.Length();
    if (l > 1e-4f) lightningDirection_ = direction * (1.0f / l);
}

void SceneRenderer::setPrecipitation(const Precipitation::Params& params) { precipitationParams_ = params; }

void SceneRenderer::drawTransparent(const Camera& camera, const RenderSettings& settings)
{
    (void)settings;
    if (precipitation_ != nullptr && !capturing_)
        precipitation_->draw(precipitationParams_, camera.view(), camera.projection(), camera.position());
    if (visibleTransparent_.empty()) { drawSteam(camera, settings); return; }
    // Sort back to front by the nearest point of the bounds to the camera.
    const Vector3 eye = camera.position();
    std::vector<std::pair<float, std::size_t>> order;
    order.reserve(visibleTransparent_.size());
    for (std::size_t index : visibleTransparent_)
    {
        const SceneItem& item = items_[index];
        const Vector3 nearest = Vector3::Clamp(eye, item.worldBounds.Min, item.worldBounds.Max);
        order.emplace_back(-Vector3::DistanceSquared(eye, nearest), index);
    }
    std::sort(order.begin(), order.end());
    device_.setDepthStencilStateProperty(DepthStencilState::DepthRead);
    for (const auto& [key, index] : order)
    {
        const SceneItem& item = items_[index];
        if (PlanarReflection* reflection = activeReflection(*item.material))
        {
            // A pane: its Fresnel share of the mirrored room added over the
            // exterior already in the frame (the plain draw's own probe
            // reflection and 4 % tint are dropped for it).
            reflection->drawSurface(*item.material, item.world, camera.view(), camera.projection(), camera.position(), *item.mesh,
                                    !usingSceneTarget_, true);
            appliedMaterial_ = nullptr;
            device_.setDepthStencilStateProperty(DepthStencilState::DepthRead);
            ++stats_.drawCalls;
            stats_.triangles += static_cast<std::size_t>(item.mesh->triangleCount());
            continue;
        }
        applyMaterial(*item.material, item.world, camera.view(), camera.projection(), item.probe, item.lamp);
        item.mesh->draw(device_);
        ++stats_.drawCalls;
        stats_.triangles += static_cast<std::size_t>(item.mesh->triangleCount());
    }
    drawSteam(camera, settings);
    device_.setDepthStencilStateProperty(DepthStencilState::Default);
    device_.setBlendStateProperty(BlendState::Opaque);
}

void SceneRenderer::drawSteam(const Camera& camera, const RenderSettings& settings)
{
    // The plume over the cup, last of the transparents (nothing of the room's
    // glass stands between it and the views), lit by the light at the cup.
    if (steam_ == nullptr || !steam_->supported() || !settings.steam || capturing_) return;
    const Matrix& view = camera.view();
    // The chimneys' smoke: grey soot lit by the sky, leaning with the wind,
    // puffs a quarter of a metre growing to a metre over six seconds.
    if (!smokePlumes_.empty() && smoke_ != nullptr && smoke_->supported())
    {
        const SkyLighting& lighting = sky_.lighting();
        Steam::Params smoke;
        smoke.radius = 0.30f;
        smoke.rise = 3.2f;
        smoke.life = 7.0f;
        smoke.size0 = 0.35f;
        smoke.size1 = 1.20f;
        smoke.opacity = 1.0f;
        smoke.radiance = lighting.ambientColour * 0.6f + lighting.sunColour * 0.15f * lighting.daylight;   // grey against the sky, warm under a low sun
        static const bool debugSmoke = std::getenv("CNA_ROOM_DEBUG_STEAM") != nullptr;
        if (debugSmoke) smoke.radiance = Vector3(2.0f, 0.0f, 2.0f);
        if (!loggedSmoke_)
        {
            loggedSmoke_ = true;
            const SmokePlume& first = smokePlumes_.front();
            CNA::Logger::Info("cna-room: smoke plume 0 at " + std::to_string(first.origin.X) + "," + std::to_string(first.origin.Y) + "," + std::to_string(first.origin.Z)
                              + " drift " + std::to_string(first.drift.X) + "," + std::to_string(first.drift.Z) + " radiance " + std::to_string(smoke.radiance.X) + ","
                              + std::to_string(smoke.radiance.Y) + "," + std::to_string(smoke.radiance.Z));
        }
        smoke.time = frameSeconds_;
        smoke.viewProjection = camera.view() * camera.projection();
        smoke.cameraRight = Vector3(view.M11, view.M21, view.M31);
        smoke.cameraUp = Vector3(view.M12, view.M22, view.M32);
        for (const SmokePlume& plume : smokePlumes_)
        {
            if (plume.strength <= 0.01f) continue;
            smoke.origin = plume.origin;
            smoke.drift = plume.drift;
            smoke.strength = plume.strength;
            smoke_->draw(smoke);
            ++stats_.drawCalls;
        }
        appliedMaterial_ = nullptr;
    }
    if (!steamSet_) return;
    Steam::Params p;
    p.origin = steamOrigin_;
    p.radius = steamRadius_;
    p.radiance = irradianceAt(steamOrigin_) * 1.2f;   // a white puff, scattering forward: a little over a white surface
    p.strength = 1.0f;
    static const bool debugSteam = std::getenv("CNA_ROOM_DEBUG_STEAM") != nullptr;
    if (debugSteam) p.radiance = Vector3(2.0f, 0.0f, 2.0f);   // magenta, to see the puffs whatever the light
    if (!loggedSteam_ && !probes_.empty())
    {
        loggedSteam_ = true;
        CNA::Logger::Info("cna-room: steam radiance " + std::to_string(p.radiance.X) + "," + std::to_string(p.radiance.Y) + "," + std::to_string(p.radiance.Z)
                          + " at " + std::to_string(steamOrigin_.X) + "," + std::to_string(steamOrigin_.Y) + "," + std::to_string(steamOrigin_.Z));
    }
    p.time = frameSeconds_;
    p.viewProjection = camera.view() * camera.projection();
    p.cameraRight = Vector3(view.M11, view.M21, view.M31);
    p.cameraUp = Vector3(view.M12, view.M22, view.M32);
    steam_->draw(p);
    appliedMaterial_ = nullptr;
    ++stats_.drawCalls;
}

Vector3 SceneRenderer::irradianceAt(const Vector3& position) const
{
    const InteriorProbe* nearest = nullptr;
    float nearestDistance = 0.0f;
    for (const auto& probe : probes_)
    {
        if (probe == nullptr) continue;
        const float d = Vector3::DistanceSquared(probe->position, position);
        if (nearest == nullptr || d < nearestDistance) { nearest = probe.get(); nearestDistance = d; }
    }
    return nearest != nullptr ? nearest->meanIrradiance : Vector3(0.02f, 0.02f, 0.02f);
}

}  // namespace CnaRoom
